import type {
  Arc,
  BoardDocument,
  Component,
  Net,
  OverlayAnnotation,
  OverlayDocument,
  Pin,
  Track,
  Via,
} from '../types/board';
import { isGroundNet, isNotConnectedPin, isTestPadPin } from './netKinds';
import {
  buildNetPropagatedValues,
  resolvePinValue,
  type PinValueMode,
} from './pinValues';
import { boardToScreen, type ViewState } from './transform';

export interface DrawHighlight {
  pinIds?: ReadonlySet<string>;
  partNames?: ReadonlySet<string>;
  selectedPinId?: string | null;
  /** Pin-selection same-net (desktop m_pinSelected). */
  selectedNetId?: number | null;
  /** Search / multi-net highlight — copper always drawn for these nets. */
  highlightNetIds?: ReadonlySet<number>;
}

export interface DrawColors {
  background: string;
  boardFill: string;
  outline: string;
  track: string;
  via: string;
  arc: string;
  partOutline: string;
  partFill: string;
  pin: string;
  pinSelected: string;
  pinHighlight: string;
  pinSameNet: string;
  /** Desktop pinGroundColor — solid fill for GND/GROUND. */
  pinGround: string;
  /** Desktop pinNotConnectedColor — solid fill for true NC only. */
  pinUnconnected: string;
  /** Desktop pinTestPadColor / pinTestPadFillColor. */
  pinTestPad: string;
  pinTestPadFill: string;
  /** Desktop pinA1PadColor — IC orientation marker (A1 / pin 1). */
  pinA1: string;
  partHighlight: string;
  partText: string;
  pinText: string;
  pinTextDark: string;
  netText: string;
  /** diode/voltage/ohm pad values — high contrast on cyan/ground fills. */
  pinValue: string;
  pinValueStroke: string;
  netWeb: string;
  trackSelected: string;
  annotation: string;
}

export const DEFAULT_COLORS: DrawColors = {
  background: '#1a1d24',
  boardFill: '#2a3140',
  outline: '#c5cad3',
  track: '#5a7a9a',
  // Copper via — must not look like NC gray (#9e9e9e).
  via: '#d4a017',
  arc: '#5a7a9a',
  partOutline: '#8b93a7',
  partFill: 'rgba(80, 90, 110, 0.35)',
  pin: '#6ec6ff',
  pinSelected: '#ffb74d',
  pinHighlight: '#ff8a80',
  // Must contrast with default pin cyan — desktop pinSameNet is vivid.
  pinSameNet: '#ff66cc',
  // Dark theme: deep blue for ground, muted gray for NC (desktop dark scheme).
  pinGround: '#3030c3',
  pinUnconnected: '#9e9e9e',
  pinTestPad: '#888888',
  pinTestPadFill: '#6c5b1f',
  // Desktop pinA1PadColor 0xdd0000 — solid red for orientation pin.
  pinA1: '#dd0000',
  partHighlight: '#ffe082',
  partText: '#d5dbe6',
  pinText: '#e8eaf0',
  pinTextDark: '#1a1d24',
  netText: '#8ba3c7',
  // Bright amber value text + dark halo for legibility on cyan/blue pads.
  pinValue: '#ffe566',
  pinValueStroke: 'rgba(10, 12, 18, 0.92)',
  // Stronger than previous 0.55 amber so spokes read clearly.
  netWeb: 'rgba(255, 230, 80, 0.9)',
  trackSelected: '#ffcc80',
  annotation: '#ce93d8',
};

function sideVisible(elSide: string, viewSide: string): boolean {
  if (elSide === 'both' || elSide === 'unknown' || !elSide) return true;
  return elSide === viewSide;
}

/**
 * Copper visibility: optional layer filter (user menu).
 * When `enabledLayers` is null/undefined, all copper layers draw.
 * `both` / empty sides always pass.
 * Desktop track-mode boards place different layers in different XY regions;
 * default is all-on so multi-layer routing is visible; user can hide inner layers.
 */
export function copperVisible(
  elSide: string,
  enabledLayers?: ReadonlySet<string> | null,
): boolean {
  if (!enabledLayers) return true;
  if (!elSide || elSide === 'both' || elSide === 'unknown') return true;
  return enabledLayers.has(elSide);
}

/**
 * Stack order for copper layers (desktop EBoardSide ranking).
 * top=1 … sN=N … bottom=last. Used for via span tests (source→target inclusive).
 */
export function layerStackIndex(side: string): number {
  if (!side) return -1;
  const s = side.toLowerCase();
  if (s === 'top' || s === 's1') return 1;
  if (s === 'bottom') return 1000;
  if (s === 'both' || s === 'unknown') return -1;
  const m = /^s(\d+)$/i.exec(s);
  if (m) return Number(m[1]);
  return -1;
}

/**
 * Via visibility vs Layers menu: show if any enabled layer lies within the
 * closed span [source, target] in stack order (e.g. top↔s3 shows when s2 is on).
 * Matches desktop BoardElementIsVisible for vias (minLayer..maxLayer).
 */
export function viaTouchesEnabledLayers(
  sourceSide: string,
  targetSide: string,
  enabledLayers?: ReadonlySet<string> | null,
): boolean {
  if (!enabledLayers) return true;
  // No explicit filter / empty set → none visible.
  if (enabledLayers.size === 0) return false;

  const a = layerStackIndex(sourceSide);
  const b = layerStackIndex(targetSide);
  // Unknown / both endpoints: fall back to endpoint membership only.
  if (a < 0 || b < 0) {
    return copperVisible(sourceSide, enabledLayers) || copperVisible(targetSide, enabledLayers);
  }
  const lo = Math.min(a, b);
  const hi = Math.max(a, b);
  for (const layer of enabledLayers) {
    const i = layerStackIndex(layer);
    if (i >= 0 && i >= lo && i <= hi) return true;
  }
  return false;
}

/** True when this net is selected or search-highlighted (bypasses layer filter). */
function netForcedVisible(
  netId: number | null | undefined,
  highlight: DrawHighlight,
): boolean {
  if (netId == null) return false;
  if (highlight.selectedNetId != null && netId === highlight.selectedNetId) return true;
  if (highlight.highlightNetIds?.has(netId)) return true;
  return false;
}

/** Layers that have copper on this board (tracks/arcs/via endpoints). */
export function collectCopperLayers(board: BoardDocument): string[] {
  const set = new Set<string>();
  for (const t of board.tracks ?? []) {
    if (t.side && t.side !== 'both' && t.side !== 'unknown') set.add(t.side);
  }
  for (const a of board.arcs ?? []) {
    if (a.side && a.side !== 'both' && a.side !== 'unknown') set.add(a.side);
  }
  for (const v of board.vias ?? []) {
    if (v.side && v.side !== 'both' && v.side !== 'unknown') set.add(v.side);
    if (v.targetSide && v.targetSide !== 'both' && v.targetSide !== 'unknown') {
      set.add(v.targetSide);
    }
  }
  // Prefer board.sides order when present, else sort top/bottom/sN.
  const order = board.sides?.length
    ? board.sides.filter((s) => set.has(s))
    : [];
  const rest = [...set].filter((s) => !order.includes(s)).sort((a, b) => {
    const rank = (s: string) => {
      if (s === 'top') return 0;
      if (s === 'bottom') return 100;
      const m = /^s(\d+)$/i.exec(s);
      return m ? Number(m[1]) : 50;
    };
    return rank(a) - rank(b) || a.localeCompare(b);
  });
  return order.length ? [...order, ...rest.filter((s) => !order.includes(s))] : rest;
}

/** Per-layer copper stroke colors (desktop uses layerColor[][]). */
export const LAYER_COPPER: Record<string, string> = {
  top: '#5aa8e0',
  bottom: '#e0a85a',
  both: '#8ba3c7',
  s2: '#6bcb77',
  s3: '#c77dff',
  s4: '#ff6b8a',
  s5: '#4ecdc4',
  s6: '#ffe066',
  s7: '#74c0fc',
  s8: '#b197fc',
};

export function layerCopperColor(side: string, fallback: string): string {
  if (!side) return fallback;
  return LAYER_COPPER[side] ?? fallback;
}

/** Layers: fill → outline → tracks/arcs/vias → parts → pins → highlights → text labels → annotations. */
export function drawBoard(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight = {},
  colors: DrawColors = DEFAULT_COLORS,
  annotations: readonly OverlayAnnotation[] = [],
  overlay: OverlayDocument | null = null,
  enabledLayers: ReadonlySet<string> | null = null,
  pinValueMode: PinValueMode = 'diode',
): void {
  ctx.save();
  ctx.clearRect(0, 0, cssW, cssH);
  ctx.fillStyle = colors.background;
  ctx.fillRect(0, 0, cssW, cssH);

  drawBoardFill(ctx, board, view, cssW, cssH, colors);
  drawOutline(ctx, board, view, cssW, cssH, colors);
  drawTracks(ctx, board, view, cssW, cssH, colors, highlight, enabledLayers);
  drawArcs(ctx, board, view, cssW, cssH, colors, highlight, enabledLayers);
  drawVias(ctx, board, view, cssW, cssH, colors, highlight, enabledLayers);
  drawParts(ctx, board, view, cssW, cssH, highlight, colors);
  drawPins(ctx, board, view, cssW, cssH, highlight, colors, overlay);
  drawNetWeb(ctx, board, view, cssW, cssH, highlight, colors);
  drawHighlights(ctx, board, view, cssW, cssH, highlight, colors);
  drawPartLabels(ctx, board, view, cssW, cssH, colors);
  drawPinLabels(ctx, board, view, cssW, cssH, highlight, colors, overlay, pinValueMode);
  drawAnnotations(ctx, annotations, view, cssW, cssH, colors);

  ctx.restore();
}

function drawBoardFill(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
): void {
  const pts = board.outline?.points ?? [];
  if (pts.length >= 3) {
    ctx.beginPath();
    for (let i = 0; i < pts.length; i++) {
      const s = boardToScreen(view, pts[i].x, pts[i].y, cssW, cssH);
      if (i === 0) ctx.moveTo(s.x, s.y);
      else ctx.lineTo(s.x, s.y);
    }
    ctx.closePath();
    ctx.fillStyle = colors.boardFill;
    ctx.fill();
    return;
  }

  const segs = board.outline?.segments ?? [];
  if (segs.length === 0) return;
  // Fallback: fill bounds rect when only segments exist
  const b = board.bounds;
  const a = boardToScreen(view, b.minX, b.minY, cssW, cssH);
  const c = boardToScreen(view, b.maxX, b.maxY, cssW, cssH);
  const x = Math.min(a.x, c.x);
  const y = Math.min(a.y, c.y);
  const w = Math.abs(c.x - a.x);
  const h = Math.abs(c.y - a.y);
  ctx.fillStyle = colors.boardFill;
  ctx.fillRect(x, y, w, h);
}

function drawOutline(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
): void {
  ctx.strokeStyle = colors.outline;
  ctx.lineWidth = 1.5;
  ctx.lineJoin = 'round';
  ctx.lineCap = 'round';

  const segs = board.outline?.segments ?? [];
  if (segs.length > 0) {
    ctx.beginPath();
    for (const seg of segs) {
      const a = boardToScreen(view, seg.x1, seg.y1, cssW, cssH);
      const b = boardToScreen(view, seg.x2, seg.y2, cssW, cssH);
      ctx.moveTo(a.x, a.y);
      ctx.lineTo(b.x, b.y);
    }
    ctx.stroke();
    return;
  }

  const pts = board.outline?.points ?? [];
  if (pts.length < 2) return;
  ctx.beginPath();
  for (let i = 0; i < pts.length; i++) {
    const s = boardToScreen(view, pts[i].x, pts[i].y, cssW, cssH);
    if (i === 0) ctx.moveTo(s.x, s.y);
    else ctx.lineTo(s.x, s.y);
  }
  ctx.stroke();
}

function drawTracks(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
  highlight: DrawHighlight = {},
  enabledLayers: ReadonlySet<string> | null = null,
): void {
  const tracks: readonly Track[] = board.tracks ?? [];
  if (!tracks.length) return;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  for (const t of tracks) {
    const onNet = netForcedVisible(t.netId, highlight);
    // Highlighted net copper always draws, even if its layer is unchecked.
    if (!onNet && !copperVisible(t.side, enabledLayers)) continue;
    const a = boardToScreen(view, t.start.x, t.start.y, cssW, cssH);
    const b = boardToScreen(view, t.end.x, t.end.y, cssW, cssH);
    const w = Math.max((t.width > 0 ? t.width : 1) * view.scale, 0.75);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    if (onNet) {
      ctx.strokeStyle = colors.trackSelected;
      ctx.lineWidth = w * 2;
    } else {
      ctx.strokeStyle = layerCopperColor(t.side, colors.track);
      ctx.lineWidth = w;
    }
    ctx.stroke();
  }
}

/**
 * Desktop BoardView::DrawArc: sample angle from start→end (may be negative sweep)
 * and plot x=cos(θ)·r, y=-sin(θ)·r so board angles (Y-up) map to screen (Y-down).
 * Canvas ctx.arc would take the long way when endAngle < startAngle — wrong here.
 */
function strokeBoardArc(
  ctx: CanvasRenderingContext2D,
  cx: number,
  cy: number,
  radius: number,
  startAngle: number,
  endAngle: number,
  segments = 50,
): void {
  if (!(radius > 0) || !Number.isFinite(startAngle) || !Number.isFinite(endAngle)) return;
  const sweep = endAngle - startAngle;
  if (sweep === 0) return;
  const n = Math.max(2, Math.min(segments, Math.ceil((Math.abs(sweep) / (Math.PI * 2)) * segments) || segments));
  const slice = sweep / n;
  ctx.beginPath();
  for (let i = 0; i <= n; i++) {
    const angle = startAngle + slice * i;
    const x = cx + Math.cos(angle) * radius;
    const y = cy - Math.sin(angle) * radius; // desktop DrawArc Y flip
    if (i === 0) ctx.moveTo(x, y);
    else ctx.lineTo(x, y);
  }
  ctx.stroke();
}

function drawArcs(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
  highlight: DrawHighlight = {},
  enabledLayers: ReadonlySet<string> | null = null,
): void {
  const arcs: readonly Arc[] = board.arcs ?? [];
  if (!arcs.length) return;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  // Desktop: startAngle/endAngle -= PI/2 * m_rotation (quarters CW).
  const rotOffset = (-Math.PI / 2) * (view.rotation ?? 0);
  for (const arc of arcs) {
    const onNet = netForcedVisible(arc.netId, highlight);
    if (!onNet && !copperVisible(arc.side, enabledLayers)) continue;
    const c = boardToScreen(view, arc.pos.x, arc.pos.y, cssW, cssH);
    const r = Math.max((arc.radius > 0 ? arc.radius : 1) * view.scale, 0.5);
    const w = Math.max((arc.width > 0 ? arc.width : 1) * view.scale, 0.75);
    const sa = arc.startAngle + rotOffset;
    const ea = arc.endAngle + rotOffset;
    ctx.strokeStyle = onNet ? colors.trackSelected : layerCopperColor(arc.side, colors.arc);
    ctx.lineWidth = onNet ? w * 1.5 : w;
    strokeBoardArc(ctx, c.x, c.y, r, sa, ea);
  }
}

/** Desktop DrawFilledSemiCircle: left half = source side, right half = target. */
function fillSemiCircle(
  ctx: CanvasRenderingContext2D,
  cx: number,
  cy: number,
  radius: number,
  rightHalf: boolean,
  color: string,
): void {
  // Desktop: left half starts at π/2 → 3π/2; right at 3π/2 → 5π/2.
  // Canvas arc angles are clockwise-from-east by default with the usual
  // math angles (y down flips visual), but we match desktop cos/sin path.
  const start = rightHalf ? (3 * Math.PI) / 2 : Math.PI / 2;
  const end = rightHalf ? (5 * Math.PI) / 2 : (3 * Math.PI) / 2;
  const n = 24;
  ctx.beginPath();
  ctx.moveTo(cx, cy);
  for (let i = 0; i <= n; i++) {
    const a = start + ((end - start) * i) / n;
    ctx.lineTo(cx + Math.cos(a) * radius, cy + Math.sin(a) * radius);
  }
  ctx.closePath();
  ctx.fillStyle = color;
  ctx.fill();
}

function shortLayerLabel(side: string): string {
  if (!side) return '?';
  if (side === 'top') return 'T';
  if (side === 'bottom') return 'B';
  if (side === 'both') return 'A';
  const m = /^s(\d+)$/i.exec(side);
  if (m) return m[1];
  return side.slice(0, 2).toUpperCase();
}

function drawVias(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
  highlight: DrawHighlight = {},
  enabledLayers: ReadonlySet<string> | null = null,
): void {
  const vias: readonly Via[] = board.vias ?? [];
  if (!vias.length) return;
  for (const v of vias) {
    const onNet = netForcedVisible(v.netId, highlight);
    // Span test: any enabled layer between source and target (inclusive) → show.
    // Highlighted-net vias always draw (same as tracks/arcs).
    if (!onNet && !viaTouchesEnabledLayers(v.side, v.targetSide, enabledLayers)) {
      continue;
    }

    const s = boardToScreen(view, v.pos.x, v.pos.y, cssW, cssH);
    const r = Math.max((v.size > 0 ? v.size : 4) * view.scale * 0.5, 1.25);

    // Desktop DrawVies: full disc + dual-layer semicircles (always both ends).
    ctx.beginPath();
    ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
    ctx.fillStyle = onNet ? colors.pinSelected : colors.via;
    ctx.fill();

    if (r > 3) {
      const hr = r * 0.8;
      const srcColor = onNet
        ? colors.pinSelected
        : layerCopperColor(v.side, colors.via);
      const dstColor = onNet
        ? colors.pinSelected
        : layerCopperColor(v.targetSide, colors.via);
      // Left half = source (board_side), right half = target_side.
      fillSemiCircle(ctx, s.x, s.y, hr, false, srcColor);
      fillSemiCircle(ctx, s.x, s.y, hr, true, dstColor);

      const fontPx = Math.min(r * 0.95, 11);
      if (fontPx >= 5) {
        ctx.font = `600 ${fontPx}px sans-serif`;
        ctx.textBaseline = 'middle';
        ctx.fillStyle = '#ffffff';
        ctx.strokeStyle = 'rgba(0,0,0,0.55)';
        ctx.lineWidth = Math.max(1.5, fontPx * 0.18);
        ctx.textAlign = 'center';
        const left = shortLayerLabel(v.side);
        const right = shortLayerLabel(v.targetSide);
        const lx = s.x - r * 0.35;
        const rx = s.x + r * 0.35;
        ctx.strokeText(left, lx, s.y);
        ctx.fillText(left, lx, s.y);
        ctx.strokeText(right, rx, s.y);
        ctx.fillText(right, rx, s.y);
      }
    }
  }
}

function drawParts(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight,
  colors: DrawColors,
): void {
  const parts = board.components ?? [];
  for (const part of parts) {
    if (!sideVisible(part.side, view.side)) continue;
    const outline = part.outline;
    if (!outline || outline.length < 2) continue;

    const hi = highlight.partNames?.has(part.name) ?? false;
    ctx.beginPath();
    for (let i = 0; i < outline.length; i++) {
      const s = boardToScreen(view, outline[i].x, outline[i].y, cssW, cssH);
      if (i === 0) ctx.moveTo(s.x, s.y);
      else ctx.lineTo(s.x, s.y);
    }
    ctx.closePath();
    ctx.fillStyle = colors.partFill;
    ctx.fill();
    ctx.strokeStyle = hi ? colors.partHighlight : colors.partOutline;
    ctx.lineWidth = hi ? 2 : 1;
    ctx.stroke();
  }
}

/** Board-space pin radius in px. `pin.diameter` is misnamed — desktop stores radius (AddCircle). */
function pinRadiusPx(pin: Pin, scale: number): number {
  // Default 7 matches BoardView when pin->diameter <= 0.
  const r = pin.diameter > 0 ? pin.diameter : 7;
  return Math.max(r * scale, 1.5);
}

/**
 * Desktop DrawPins size: circle uses diameter as radius; rect uses size.x/y half-extents.
 * Angle 90/270 swaps w/h; view rotation 1/3 also swaps.
 */
function pinHalfExtentsPx(pin: Pin, view: ViewState): { w: number; h: number; r: number } {
  const r = pinRadiusPx(pin, view.scale);
  let w = r;
  let h = r;
  if ((pin.shape || '').toLowerCase() === 'rect') {
    const sx = pin.size?.x ?? 0;
    const sy = pin.size?.y ?? 0;
    w = Math.max(((sx > 0 ? sx : pin.diameter * 2) * view.scale) / 2, 1.5);
    h = Math.max(((sy > 0 ? sy : pin.diameter * 2) * view.scale) / 2, 1.5);
  }
  const ang = ((pin.angle % 360) + 360) % 360;
  if (ang === 90 || ang === 270) {
    const t = w;
    w = h;
    h = t;
  }
  if (view.rotation === 1 || view.rotation === 3) {
    const t = w;
    w = h;
    h = t;
  }
  return { w, h, r };
}

function pathPinShape(
  ctx: CanvasRenderingContext2D,
  pin: Pin,
  view: ViewState,
  cx: number,
  cy: number,
): void {
  const { w, h, r } = pinHalfExtentsPx(pin, view);
  ctx.beginPath();
  if ((pin.shape || '').toLowerCase() === 'rect') {
    ctx.rect(cx - w, cy - h, w * 2, h * 2);
  } else {
    ctx.arc(cx, cy, r, 0, Math.PI * 2);
  }
}

/** Desktop default pinA1threshold — packages with this many pins show number "1". */
const PIN_A1_THRESHOLD = 3;

/**
 * IC orientation marker (desktop DrawPins):
 * - display label == "A1" (overlay show_name > board show_name > name) always
 * - number == "1" when part has >= pinA1threshold pins (default 3)
 */
function isOrientationPin(
  pin: Pin,
  partPinCount: number,
  overlay: OverlayDocument | null | undefined,
  threshold = PIN_A1_THRESHOLD,
): boolean {
  const show = pinDisplayName(pin, overlay).toUpperCase();
  if (show === 'A1') return true;
  if (String(pin.number || '').trim() === '1' && partPinCount >= threshold) return true;
  return false;
}

function drawPins(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight,
  colors: DrawColors,
  overlay: OverlayDocument | null = null,
): void {
  const pins = board.pins ?? [];
  const netById = new Map<number, Net>();
  for (const n of board.nets ?? []) netById.set(n.id, n);
  // component → pin count for pin-1 threshold (desktop component->pins.size()).
  const partPinCount = new Map<string, number>();
  for (const c of board.components ?? []) {
    partPinCount.set(c.name, c.pins?.length ?? 0);
  }

  for (const pin of pins) {
    if (!sideVisible(pin.side, view.side)) continue;
    const s = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
    const selected = highlight.selectedPinId === pin.id;
    const hi = highlight.pinIds?.has(pin.id) ?? false;
    const sameNet = !selected && netForcedVisible(pin.netId, highlight);
    const net = pin.netId != null ? netById.get(pin.netId) : undefined;
    const ground = isGroundNet(net);
    // Prefer pin.type (export) — missing PIN_NET must not look like NC.
    const notConnected = isNotConnectedPin(pin, net);
    const testPad = isTestPadPin(pin);
    const pinCount = pin.component ? (partPinCount.get(pin.component) ?? 0) : 0;
    const orient = isOrientationPin(pin, pinCount, overlay);

    pathPinShape(ctx, pin, view, s.x, s.y);
    if (selected) {
      ctx.fillStyle = colors.pinSelected;
      ctx.fill();
      ctx.strokeStyle = '#fff3e0';
      ctx.lineWidth = 1.5;
      ctx.stroke();
    } else if (sameNet) {
      // Same-net peers of the selected pin — vivid fill + ring so they don't
      // blend into default cyan pads.
      ctx.fillStyle = colors.pinSameNet;
      ctx.fill();
      ctx.strokeStyle = '#ffe066';
      ctx.lineWidth = 1.75;
      ctx.stroke();
    } else if (hi) {
      ctx.fillStyle = colors.pinHighlight;
      ctx.fill();
    } else if (ground) {
      // Desktop: solid pinGroundColor for is_ground nets.
      // A1/1 still wins after ground in desktop — apply orientation next.
      if (orient) {
        ctx.fillStyle = colors.pinA1;
        ctx.fill();
      } else {
        ctx.fillStyle = colors.pinGround;
        ctx.fill();
      }
    } else if (notConnected) {
      // Desktop: solid pinNotConnectedColor only for kPinTypeNotConnected.
      ctx.fillStyle = colors.pinUnconnected;
      ctx.fill();
    } else if (orient) {
      // Desktop pinA1PadColor: solid red, no ring — IC pin-1 / A1 locator.
      ctx.fillStyle = colors.pinA1;
      ctx.fill();
    } else if (testPad) {
      // Desktop test pad: brown fill + gray ring (not NC gray).
      ctx.fillStyle = colors.pinTestPadFill;
      ctx.fill();
      ctx.strokeStyle = colors.pinTestPad;
      ctx.lineWidth = 1;
      ctx.stroke();
    } else {
      // Signal pin: solid cyan fill so it never looks like hollow NC gray.
      // Desktop only rings by default; web needs fill to stay distinct from via/NC.
      ctx.fillStyle = colors.pin;
      ctx.fill();
      ctx.strokeStyle = '#b3e5fc';
      ctx.lineWidth = 1;
      ctx.stroke();
    }
  }
}

/**
 * Desktop DrawNetWeb: spokes from selected pin to other same-net pins.
 * Skips ground nets (BoardView::DrawNetWeb early-out).
 */
function drawNetWeb(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight,
  colors: DrawColors,
): void {
  const selId = highlight.selectedPinId;
  const netId = highlight.selectedNetId;
  if (!selId || netId == null) return;
  const sel = board.pins.find((p) => p.id === selId);
  if (!sel || sel.netId !== netId) return;
  const net = board.nets.find((n) => n.id === netId);
  if (net?.isGround) return;

  const origin = boardToScreen(view, sel.pos.x, sel.pos.y, cssW, cssH);
  ctx.strokeStyle = colors.netWeb;
  ctx.lineWidth = Math.max(1.5, 2 * Math.min(view.scale, 3));
  ctx.lineCap = 'round';
  for (const pin of board.pins) {
    if (pin.id === selId || pin.netId !== netId) continue;
    // Still draw spokes to off-side pads so net membership is obvious.
    const t = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
    ctx.beginPath();
    ctx.moveTo(origin.x, origin.y);
    ctx.lineTo(t.x, t.y);
    ctx.stroke();
  }
}

function drawHighlights(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight,
  colors: DrawColors,
): void {
  if (!highlight.selectedPinId) return;
  const pin = board.pins.find((p) => p.id === highlight.selectedPinId);
  if (!pin) return;
  if (!sideVisible(pin.side, view.side)) return;
  const s = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
  const r = pinRadiusPx(pin, view.scale) + 3;
  ctx.beginPath();
  ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
  ctx.strokeStyle = colors.pinSelected;
  ctx.lineWidth = 2;
  ctx.stroke();

  if (pin.component) {
    const part: Component | undefined = board.components.find((c) => c.name === pin.component);
    if (part?.outline && part.outline.length >= 2 && sideVisible(part.side, view.side)) {
      ctx.beginPath();
      for (let i = 0; i < part.outline.length; i++) {
        const p = boardToScreen(view, part.outline[i].x, part.outline[i].y, cssW, cssH);
        if (i === 0) ctx.moveTo(p.x, p.y);
        else ctx.lineTo(p.x, p.y);
      }
      ctx.closePath();
      ctx.strokeStyle = colors.partHighlight;
      ctx.lineWidth = 2;
      ctx.stroke();
    }
  }
}

/**
 * Part name centered in the part's screen bbox — port of DrawParts showPartName.
 * Desktop skips dummy parts (`is_dummy`) and single-pin devices don't show pad
 * text; we also skip part names for those (nails / test pads clutter the view).
 */
function drawPartLabels(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
): void {
  const parts = board.components ?? [];
  if (!parts.length) return;
  ctx.textAlign = 'center';
  ctx.textBaseline = 'middle';
  ctx.fillStyle = colors.partText;
  for (const part of parts) {
    if (!part.name) continue;
    if (!sideVisible(part.side, view.side)) continue;
    // Desktop DrawParts: if (part->is_dummy()) continue;
    // Single-pin parts (test pads / nails) — no part name label.
    const pinCount = part.pins?.length ?? 0;
    if (pinCount <= 1) continue;
    const ptype = (part.type || '').toLowerCase();
    if (ptype === 'dummy' || ptype === 'board') continue;
    const outline = part.outline;
    if (!outline || outline.length < 2) continue;

    let minX = Infinity;
    let minY = Infinity;
    let maxX = -Infinity;
    let maxY = -Infinity;
    for (const p of outline) {
      const s = boardToScreen(view, p.x, p.y, cssW, cssH);
      if (s.x < minX) minX = s.x;
      if (s.x > maxX) maxX = s.x;
      if (s.y < minY) minY = s.y;
      if (s.y > maxY) maxY = s.y;
    }
    const bw = maxX - minX;
    const bh = maxY - minY;
    // Zoom gate: at fit zoom only large parts pass; also cull offscreen parts.
    if (bw < 30) continue;
    if (maxX < 0 || minX > cssW || maxY < 0 || minY > cssH) continue;

    let size = Math.min(14, Math.max(9, bh * 0.6));
    ctx.font = `${size}px sans-serif`;
    let tw = ctx.measureText(part.name).width;
    if (tw > bw * 1.2) {
      // Shrink once toward the 9px floor; skip if it still doesn't fit.
      const shrunk = Math.max(9, (size * (bw * 1.2)) / tw);
      if (shrunk >= size) continue;
      size = shrunk;
      ctx.font = `${size}px sans-serif`;
      tw = ctx.measureText(part.name).width;
      if (tw > bw * 1.2) continue;
    }
    ctx.fillText(part.name, minX + bw / 2, minY + bh / 2);
  }
}

/**
 * Pin pad text:
 * - top: pin show_name / name / number (not net name)
 * - bottom: overlay field for current mode (diode|voltage|ohm|ohm_black),
 *   local first, else first non-empty value on the same net.
 */
function pinDisplayName(pin: Pin, overlay: OverlayDocument | null | undefined): string {
  if (overlay && pin.component) {
    const key = pin.name || pin.number || '';
    const ov = key ? overlay.partInfos[pin.component]?.pins?.[key]?.show_name : undefined;
    if (ov && ov.trim()) return ov.trim();
  }
  if (pin.show_name && pin.show_name.trim()) return pin.show_name.trim();
  if (pin.name && pin.name.trim()) return pin.name.trim();
  return (pin.number || '').trim();
}

function drawPinLabels(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight,
  colors: DrawColors,
  overlay: OverlayDocument | null = null,
  pinValueMode: PinValueMode = 'diode',
): void {
  const pins = board.pins ?? [];
  if (!pins.length) return;

  const netValues = buildNetPropagatedValues(board, overlay, pinValueMode);

  ctx.textAlign = 'center';
  for (const pin of pins) {
    if (!sideVisible(pin.side, view.side)) continue;
    const r = pinRadiusPx(pin, view.scale);
    const selected = highlight.selectedPinId === pin.id;
    const hi = highlight.pinIds?.has(pin.id) ?? false;
    const partHi = !!(pin.component && highlight.partNames?.has(pin.component));
    const sameNet = !selected && netForcedVisible(pin.netId, highlight);
    const forceText = selected || hi || partHi || sameNet;
    if (!forceText && r < 3.5) continue;
    const s = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
    if (s.x < -r || s.x > cssW + r || s.y < -r || s.y > cssH + r) continue;

    const nameLabel = pinDisplayName(pin, overlay);
    const valueLabel = resolvePinValue(pin, overlay, pinValueMode, netValues);
    const filled = selected || hi || sameNet;
    const showValue = !!(valueLabel && (r >= 6 || forceText || !nameLabel));

    if (nameLabel) {
      let size = Math.min(2 * Math.max(r, forceText ? 6 : r) * 0.75, 14);
      if (forceText) size = Math.max(size, 9);
      ctx.font = `${size}px sans-serif`;
      ctx.fillStyle = filled ? colors.pinTextDark : colors.pinText;
      if (!showValue) {
        ctx.textBaseline = 'middle';
        ctx.fillText(nameLabel, s.x, s.y);
      } else {
        ctx.textBaseline = 'bottom';
        ctx.fillText(nameLabel, s.x, s.y - 1);
      }
    }

    if (showValue) {
      // Slightly larger than before; amber + dark outline reads on cyan/ground pads.
      const size = Math.min(13, Math.max(9, r * 0.65));
      ctx.font = `600 ${size}px sans-serif`;
      ctx.textBaseline = nameLabel ? 'top' : 'middle';
      const y = nameLabel ? s.y + 1 : s.y;
      ctx.lineWidth = Math.max(2.5, size * 0.22);
      ctx.lineJoin = 'round';
      ctx.strokeStyle = colors.pinValueStroke;
      ctx.strokeText(valueLabel, s.x, y);
      ctx.fillStyle = colors.pinValue;
      ctx.fillText(valueLabel, s.x, y);
    }
  }
}

/** Freeform annotation markers (desktop side: 0=top, 1=bottom). */
function drawAnnotations(
  ctx: CanvasRenderingContext2D,
  annotations: readonly OverlayAnnotation[],
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
): void {
  if (!annotations.length) return;
  const viewBottom = view.side === 'bottom';
  for (const ann of annotations) {
    if (!ann.visible) continue;
    const annBottom = ann.side !== 0;
    if (annBottom !== viewBottom) continue;
    const s = boardToScreen(view, ann.x, ann.y, cssW, cssH);
    const r = 5;
    ctx.beginPath();
    ctx.moveTo(s.x, s.y - r);
    ctx.lineTo(s.x + r, s.y + r * 0.7);
    ctx.lineTo(s.x - r, s.y + r * 0.7);
    ctx.closePath();
    ctx.fillStyle = colors.annotation;
    ctx.fill();
    ctx.strokeStyle = '#f3e5f5';
    ctx.lineWidth = 1;
    ctx.stroke();
  }
}
