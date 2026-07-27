import type { Arc, BoardDocument, Component, Net, OverlayAnnotation, Pin, Track, Via } from '../types/board';
import { boardToScreen, type ViewState } from './transform';

export interface DrawHighlight {
  pinIds?: ReadonlySet<string>;
  partNames?: ReadonlySet<string>;
  selectedPinId?: string | null;
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
  partHighlight: string;
  partText: string;
  pinText: string;
  pinTextDark: string;
  netText: string;
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
  partHighlight: '#ffe082',
  partText: '#d5dbe6',
  pinText: '#e8eaf0',
  pinTextDark: '#1a1d24',
  netText: '#8ba3c7',
  annotation: '#ce93d8',
};

function sideVisible(elSide: string, viewSide: string): boolean {
  if (elSide === 'both' || elSide === 'unknown' || !elSide) return true;
  return elSide === viewSide;
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
): void {
  ctx.save();
  ctx.clearRect(0, 0, cssW, cssH);
  ctx.fillStyle = colors.background;
  ctx.fillRect(0, 0, cssW, cssH);

  drawBoardFill(ctx, board, view, cssW, cssH, colors);
  drawOutline(ctx, board, view, cssW, cssH, colors);
  drawTracks(ctx, board, view, cssW, cssH, colors);
  drawArcs(ctx, board, view, cssW, cssH, colors);
  drawVias(ctx, board, view, cssW, cssH, colors);
  drawParts(ctx, board, view, cssW, cssH, highlight, colors);
  drawPins(ctx, board, view, cssW, cssH, highlight, colors);
  drawHighlights(ctx, board, view, cssW, cssH, highlight, colors);
  drawPartLabels(ctx, board, view, cssW, cssH, colors);
  drawPinLabels(ctx, board, view, cssW, cssH, highlight, colors);
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
): void {
  const tracks: readonly Track[] = board.tracks ?? [];
  if (!tracks.length) return;
  ctx.strokeStyle = colors.track;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';
  for (const t of tracks) {
    if (!sideVisible(t.side, view.side)) continue;
    const a = boardToScreen(view, t.start.x, t.start.y, cssW, cssH);
    const b = boardToScreen(view, t.end.x, t.end.y, cssW, cssH);
    const w = Math.max((t.width > 0 ? t.width : 1) * view.scale, 0.75);
    ctx.beginPath();
    ctx.moveTo(a.x, a.y);
    ctx.lineTo(b.x, b.y);
    ctx.lineWidth = w;
    ctx.stroke();
  }
}

function drawArcs(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
): void {
  const arcs: readonly Arc[] = board.arcs ?? [];
  if (!arcs.length) return;
  ctx.strokeStyle = colors.arc;
  ctx.lineCap = 'round';
  for (const arc of arcs) {
    if (!sideVisible(arc.side, view.side)) continue;
    const c = boardToScreen(view, arc.pos.x, arc.pos.y, cssW, cssH);
    const r = Math.max((arc.radius > 0 ? arc.radius : 1) * view.scale, 0.5);
    const w = Math.max((arc.width > 0 ? arc.width : 1) * view.scale, 0.75);
    // Board angles are radians (desktop BRD arc convention).
    ctx.beginPath();
    ctx.arc(c.x, c.y, r, arc.startAngle, arc.endAngle);
    ctx.lineWidth = w;
    ctx.stroke();
  }
}

function drawVias(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  colors: DrawColors,
): void {
  const vias: readonly Via[] = board.vias ?? [];
  if (!vias.length) return;
  ctx.fillStyle = colors.via;
  ctx.strokeStyle = colors.via;
  for (const v of vias) {
    // Vias are inter-layer; show when either face is visible or side is both/unknown.
    const visible =
      sideVisible(v.side, view.side) ||
      sideVisible(v.targetSide, view.side) ||
      v.side === 'both' ||
      v.targetSide === 'both';
    if (!visible) continue;
    const s = boardToScreen(view, v.pos.x, v.pos.y, cssW, cssH);
    const r = Math.max((v.size > 0 ? v.size : 4) * view.scale * 0.5, 1.25);
    ctx.beginPath();
    ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
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

    ctx.beginPath();
    ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
    if (selected) {
      ctx.fillStyle = colors.pinSelected;
      ctx.fill();
      ctx.strokeStyle = '#fff3e0';
      ctx.lineWidth = 1.5;
      ctx.stroke();
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
  // Same side filter as drawPins — hide selection halo on the other side.
  if (!sideVisible(pin.side, view.side)) return;
  const s = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
  const r = pinRadiusPx(pin, view.scale) + 3;
  ctx.beginPath();
  ctx.arc(s.x, s.y, r, 0, Math.PI * 2);
  ctx.strokeStyle = colors.pinSelected;
  ctx.lineWidth = 2;
  ctx.stroke();

  // Halo on parent part if present
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
 * Pin number inside the pad and net name below it — port of DrawPins text:
 * desktop shows pin text when psz clears the size threshold, net name at
 * larger sizes and never for ground nets.
 */
function drawPinLabels(
  ctx: CanvasRenderingContext2D,
  board: BoardDocument,
  view: ViewState,
  cssW: number,
  cssH: number,
  highlight: DrawHighlight,
  colors: DrawColors,
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
    if (r < 6) continue; // zoom gate — matches desktop psz threshold intent
    const s = boardToScreen(view, pin.pos.x, pin.pos.y, cssW, cssH);
    if (s.x < -r || s.x > cssW + r || s.y < -r || s.y > cssH + r) continue;

    const label = pin.number || pin.name;
    const selected = highlight.selectedPinId === pin.id;
    const filled = selected || (highlight.pinIds?.has(pin.id) ?? false);
    if (label) {
      const size = Math.min(2 * r * 0.8, 12);
      ctx.font = `${size}px sans-serif`;
      ctx.textBaseline = 'middle';
      // Dark text on filled (selected/highlighted) pads, light on outlined pads.
      ctx.fillStyle = filled ? colors.pinTextDark : colors.pinText;
      ctx.fillText(label, s.x, s.y);
    }

    if (r >= 12 && pin.netId != null) {
      if (netById === null) {
        netById = new Map();
        for (const n of nets) netById.set(n.id, n);
      }
      const net = netById.get(pin.netId);
      // Desktop suppresses net names on ground pins (they'd all read "GND").
      if (!net || !net.name || net.isGround) continue;
      const size = Math.min(10, Math.max(8, r * 0.5));
      ctx.font = `${size}px sans-serif`;
      ctx.textBaseline = 'top';
      ctx.fillStyle = colors.netText;
      ctx.fillText(net.name, s.x, s.y + r + 2);
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
