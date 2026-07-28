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
  partHighlight: string;
  partText: string;
  pinText: string;
  pinTextDark: string;
  netText: string;
  netWeb: string;
  trackSelected: string;
  annotation: string;
}

export const DEFAULT_COLORS: DrawColors = {
  background: '#1a1d24',
  boardFill: '#2a3140',
  outline: '#c5cad3',
  track: '#5a7a9a',
  via: '#90a4ae',
  arc: '#5a7a9a',
  partOutline: '#8b93a7',
  partFill: 'rgba(80, 90, 110, 0.35)',
  pin: '#6ec6ff',
  pinSelected: '#ffb74d',
  pinHighlight: '#ff8a80',
  pinSameNet: '#4fc3f7',
  partHighlight: '#ffe082',
  partText: '#d5dbe6',
  pinText: '#e8eaf0',
  pinTextDark: '#1a1d24',
  netText: '#8ba3c7',
  netWeb: 'rgba(255, 183, 77, 0.55)',
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
  drawPins(ctx, board, view, cssW, cssH, highlight, colors);
  drawNetWeb(ctx, board, view, cssW, cssH, highlight, colors);
  drawHighlights(ctx, board, view, cssW, cssH, highlight, colors);
  drawPartLabels(ctx, board, view, cssW, cssH, colors);
  drawPinLabels(ctx, board, view, cssW, cssH, highlight, colors, overlay);
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
    // Highlighted net vias always draw regardless of layer menu.
    if (
      !onNet &&
      !copperVisible(v.side, enabledLayers) &&
      !copperVisible(v.targetSide, enabledLayers)
    ) {
      continue;
    }
    const s = boardToScreen(view, v.pos.x, v.pos.y, cssW, cssH);
    const r = Math.max((v.size > 0 ? v.size : 4) * view.scale * 0.5, 1.25);
    ctx.beginPath();
    ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
    ctx.fillStyle = onNet ? colors.pinSelected : colors.via;
    ctx.fill();
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

function drawPins(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight,
  colors: DrawColors,
): void {
  const pins = board.pins ?? [];
  for (const pin of pins) {
    if (!sideVisible(pin.side, view.side)) continue;
    const s = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
    const r = pinRadiusPx(pin, view.scale);
    const selected = highlight.selectedPinId === pin.id;
    const hi = highlight.pinIds?.has(pin.id) ?? false;
    const sameNet = !selected && netForcedVisible(pin.netId, highlight);

    ctx.beginPath();
    ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
    if (selected) {
      ctx.fillStyle = colors.pinSelected;
      ctx.fill();
      ctx.strokeStyle = '#fff3e0';
      ctx.lineWidth = 1.5;
      ctx.stroke();
    } else if (sameNet) {
      ctx.fillStyle = colors.pinSameNet;
      ctx.fill();
    } else if (hi) {
      ctx.fillStyle = colors.pinHighlight;
      ctx.fill();
    } else {
      ctx.strokeStyle = colors.pin;
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
  ctx.lineWidth = Math.max(1, 1.25 * Math.min(view.scale, 2));
  ctx.lineCap = 'round';
  for (const pin of board.pins) {
    if (pin.id === selId || pin.netId !== netId) continue;
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
 * Part name centered in the part's screen bbox — port of DrawParts showPartName:
 * font fitted to bbox, skipped when too small (desktop skips at maxfontsize <= 1).
 * Zoom-gated: nothing renders until the part is ~30px wide on screen.
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
 * Desktop DrawPins text: pin->show_name above net->show_name.
 * show_name priority: overlay pinInfo.show_name → pin.show_name → pin.name → pin.number
 * net label priority: overlay netInfos[name].showname → net.showName → net.name
 * Ground nets suppress net label (desktop).
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

function netDisplayName(net: Net, overlay: OverlayDocument | null | undefined): string {
  if (overlay) {
    const ov = overlay.netInfos[net.name]?.showname;
    if (ov && ov.trim()) return ov.trim();
  }
  if (net.showName && net.showName.trim()) return net.showName.trim();
  return (net.name || '').trim();
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
): void {
  const pins = board.pins ?? [];
  if (!pins.length) return;
  // netId→Net Map built lazily — only paid once the first pin clears the
  // net-name zoom gate, so fit-zoom frames on dense boards skip it entirely.
  const nets = board.nets ?? [];
  let netById: Map<number, Net> | null = null;

  ctx.textAlign = 'center';
  for (const pin of pins) {
    if (!sideVisible(pin.side, view.side)) continue;
    const r = pinRadiusPx(pin, view.scale);
    // Desktop: show_text for selection, search hits, and same-net pads.
    const selected = highlight.selectedPinId === pin.id;
    const hi = highlight.pinIds?.has(pin.id) ?? false;
    const sameNet = !selected && netForcedVisible(pin.netId, highlight);
    const forceText = selected || hi || sameNet;
    if (!forceText && r < 3.5) continue;
    const s = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
    if (s.x < -r || s.x > cssW + r || s.y < -r || s.y > cssH + r) continue;

    const label = pinDisplayName(pin, overlay);
    const filled = selected || hi || sameNet;
    if (label) {
      // Font scales with pad; ensure readable when forced (selection).
      let size = Math.min(2 * Math.max(r, forceText ? 6 : r) * 0.75, 14);
      if (forceText) size = Math.max(size, 9);
      ctx.font = `${size}px sans-serif`;
      // Pin name sits slightly above center; net name below (desktop layout).
      ctx.textBaseline = 'bottom';
      ctx.fillStyle = filled ? colors.pinTextDark : colors.pinText;
      // When only pin name (no net), center it in the pad.
      const showNet = r >= 8 || forceText;
      if (!showNet) {
        ctx.textBaseline = 'middle';
        ctx.fillText(label, s.x, s.y);
      } else {
        ctx.fillText(label, s.x, s.y - 1);
      }
    }

    if ((r >= 8 || forceText) && pin.netId != null) {
      if (netById === null) {
        netById = new Map();
        for (const n of nets) netById.set(n.id, n);
      }
      const net = netById.get(pin.netId);
      // Desktop suppresses net names on ground pins.
      if (!net || net.isGround) continue;
      const netLabel = netDisplayName(net, overlay);
      if (!netLabel) continue;
      const size = Math.min(11, Math.max(8, r * 0.55));
      ctx.font = `${size}px sans-serif`;
      ctx.textBaseline = 'top';
      ctx.fillStyle = colors.netText;
      ctx.fillText(netLabel, s.x, s.y + 1);
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
