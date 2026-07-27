import type { BoardDocument, Pin } from '../types/board';
import { screenToBoard, type ViewState } from './transform';

/**
 * Default board-space pin radius when `pin.diameter` is missing/≤0.
 * Matches BoardView (`if (p->diameter <= 0) p->diameter = 7`).
 * Note: JSON field is named `diameter` but holds desktop radius (brd_pin.radius / AddCircle).
 */
export const DEFAULT_PIN_RADIUS = 7;

function sideVisible(elSide: string, viewSide: string): boolean {
  if (elSide === 'both' || elSide === 'unknown' || !elSide) return true;
  return elSide === viewSide;
}

/** Board-space pick radius for a pin (`diameter` field is actually radius). */
export function pinPickRadius(pin: Pick<Pin, 'diameter'>, fallback = DEFAULT_PIN_RADIUS): number {
  return pin.diameter > 0 ? pin.diameter : fallback;
}

/**
 * Nearest pin within threshold in board space.
 * Threshold: pin.diameter when > 0 (radius), else DEFAULT_PIN_RADIUS.
 */
export function hitTestNearestPin(
  board: BoardDocument,
  view: ViewState,
  sx: number,
  sy: number,
  cssW: number,
  cssH: number,
  opts?: { minRadius?: number },
): Pin | null {
  const pos = screenToBoard(view, sx, sy, cssW, cssH);
  const fallback = opts?.minRadius ?? DEFAULT_PIN_RADIUS;
  let best: Pin | null = null;
  let bestDist = Infinity;

  for (const pin of board.pins) {
    if (!sideVisible(pin.side, view.side)) continue;
    const dx = pin.pos.x - pos.x;
    const dy = pin.pos.y - pos.y;
    const dist2 = dx * dx + dy * dy;
    const r = pinPickRadius(pin, fallback);
    const thresh2 = r * r;
    if (dist2 <= thresh2 && dist2 < bestDist) {
      bestDist = dist2;
      best = pin;
    }
  }
  return best;
}
