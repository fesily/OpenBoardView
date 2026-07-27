import type { Bounds } from '../types/board';

export type Rotation = 0 | 1 | 2 | 3;

export interface ViewState {
  scale: number;
  mx: number; // board center (look-at) x
  my: number;
  rotation: Rotation; // quarters clockwise
  side: 'top' | 'bottom' | string;
  flipY: boolean;
  mirror: boolean;
}

export interface Point2 {
  x: number;
  y: number;
}

const DEFAULT_FLIP_Y = true;

/**
 * Port of BoardView::CoordToScreen axis conventions:
 * - side bottom → mirror X relative to look-at
 * - flipY (desktop default) → negate Y before rotation (board Y up → screen Y down)
 * - rotation 0..3 clockwise quarters
 * - look-at (mx,my) maps to canvas center
 */
export function boardToScreen(
  v: ViewState,
  x: number,
  y: number,
  cssW: number,
  cssH: number,
): Point2 {
  let dx = x - v.mx;
  let dy = y - v.my;

  if (v.side === 'bottom') dx = -dx;
  if (v.mirror) dx = -dx;

  let sx = dx * v.scale;
  let sy = (v.flipY ? -dy : dy) * v.scale;

  switch (v.rotation) {
    case 0:
      break;
    case 1: {
      const t = sx;
      sx = -sy;
      sy = t;
      break;
    }
    case 2:
      sx = -sx;
      sy = -sy;
      break;
    default: {
      // 3
      const t = sx;
      sx = sy;
      sy = -t;
      break;
    }
  }

  return { x: sx + cssW / 2, y: sy + cssH / 2 };
}

/** Inverse of boardToScreen — port of BoardView::ScreenToCoord (w=1 path). */
export function screenToBoard(
  v: ViewState,
  sx: number,
  sy: number,
  cssW: number,
  cssH: number,
): Point2 {
  let x = sx - cssW / 2;
  let y = sy - cssH / 2;

  switch (v.rotation) {
    case 0:
      break;
    case 1: {
      // inverse of (sx,sy)=(-ty,tx)
      const t = x;
      x = y;
      y = -t;
      break;
    }
    case 2:
      x = -x;
      y = -y;
      break;
    default: {
      // 3 inverse of (sx,sy)=(ty,-tx)
      const t = x;
      x = -y;
      y = t;
      break;
    }
  }

  const inv = v.scale !== 0 ? 1 / v.scale : 0;
  x *= inv;
  y *= inv;

  if (v.flipY) y = -y;
  if (v.side === 'bottom') x = -x;
  if (v.mirror) x = -x;

  return { x: x + v.mx, y: y + v.my };
}

/** Port of CenterView / LoadBoard fit: 10% padding, min(sx,sy). */
export function centerOnBounds(bounds: Bounds, cssW: number, cssH: number): ViewState {
  const width = Math.max(bounds.maxX - bounds.minX, 1);
  const height = Math.max(bounds.maxY - bounds.minY, 1);
  const dx = 1.1 * width;
  const dy = 1.1 * height;
  const sx = cssW > 0 && dx > 0 ? cssW / dx : 1;
  const sy = cssH > 0 && dy > 0 ? cssH / dy : 1;
  const scale = Math.min(sx, sy) || 1;

  return {
    scale,
    mx: (bounds.minX + bounds.maxX) / 2,
    my: (bounds.minY + bounds.maxY) / 2,
    rotation: 0,
    side: 'top',
    flipY: DEFAULT_FLIP_Y,
    mirror: false,
  };
}

export function defaultView(): ViewState {
  return {
    scale: 1,
    mx: 0,
    my: 0,
    rotation: 0,
    side: 'top',
    flipY: DEFAULT_FLIP_Y,
    mirror: false,
  };
}

/** Rotate view by `quarters` (positive = CW). Keeps look-at fixed (screen center). */
export function rotateView(v: ViewState, quarters: number): ViewState {
  let r = v.rotation;
  let q = quarters;
  while (q > 0) {
    r = ((r + 1) & 3) as Rotation;
    q--;
  }
  while (q < 0) {
    r = ((r - 1) & 3) as Rotation;
    q++;
  }
  return { ...v, rotation: r };
}

/**
 * Flip side. Matches FlipBoard when flipVertically: toggle side and rotate 180°.
 * Geometry mirror (desktop Mirror()) is separate — use `mirror` flag for X mirror.
 */
export function flipBoard(v: ViewState): ViewState {
  const side = v.side === 'bottom' ? 'top' : 'bottom';
  if (v.flipY) {
    return { ...v, side, rotation: ((v.rotation + 2) & 3) as Rotation };
  }
  return { ...v, side };
}

/** Zoom by factor (e.g. 2^wheel) keeping board point under (sx,sy) fixed. */
export function zoomAt(
  v: ViewState,
  sx: number,
  sy: number,
  cssW: number,
  cssH: number,
  factor: number,
): ViewState {
  if (factor <= 0 || !Number.isFinite(factor)) return v;
  const before = screenToBoard(v, sx, sy, cssW, cssH);
  const next: ViewState = {
    ...v,
    scale: clampScale(v.scale * factor),
  };
  const after = screenToBoard(next, sx, sy, cssW, cssH);
  return {
    ...next,
    mx: next.mx + (before.x - after.x),
    my: next.my + (before.y - after.y),
  };
}

/**
 * Pan so content follows screen delta (dsx, dsy) — same idea as
 * m_dx += ScreenToCoord(delta, w=0) with look-at expressed as mx/my.
 */
export function panByScreen(
  v: ViewState,
  dsx: number,
  dsy: number,
  cssW: number,
  cssH: number,
): ViewState {
  // Board point that should move to previous center: screenToBoard of (center - delta)
  // Equivalent: subtract board-space screen delta from look-at.
  const a = screenToBoard(v, cssW / 2, cssH / 2, cssW, cssH);
  const b = screenToBoard(v, cssW / 2 - dsx, cssH / 2 - dsy, cssW, cssH);
  return {
    ...v,
    mx: v.mx + (b.x - a.x),
    my: v.my + (b.y - a.y),
  };
}

export function clampScale(scale: number): number {
  if (!Number.isFinite(scale) || scale <= 0) return 1e-6;
  return Math.min(Math.max(scale, 1e-6), 1e6);
}
