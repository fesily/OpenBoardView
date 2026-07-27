import type { BoardDocument, Component, OverlayAnnotation, Pin } from '../types/board';
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
  partOutline: string;
  partFill: string;
  pin: string;
  pinSelected: string;
  pinHighlight: string;
  partHighlight: string;
  annotation: string;
}

export const DEFAULT_COLORS: DrawColors = {
  background: '#1a1d24',
  boardFill: '#2a3140',
  outline: '#c5cad3',
  partOutline: '#8b93a7',
  partFill: 'rgba(80, 90, 110, 0.35)',
  pin: '#6ec6ff',
  pinSelected: '#ffb74d',
  pinHighlight: '#ff8a80',
  partHighlight: '#ffe082',
  annotation: '#ce93d8',
};

function sideVisible(elSide: string, viewSide: string): boolean {
  if (elSide === 'both' || elSide === 'unknown' || !elSide) return true;
  return elSide === viewSide;
}

/** Layers: fill → outline → parts → pins → highlights → annotations (spec §8.3 subset). */
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
  drawParts(ctx, board, view, cssW, cssH, highlight, colors);
  drawPins(ctx, board, view, cssW, cssH, highlight, colors);
  drawHighlights(ctx, board, view, cssW, cssH, highlight, colors);
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
