import type { BoardDocument, Pin } from '../types/board';
import { screenToBoard, type ViewState } from './transform';

/** Desktop default pin diameter (thou) used as minimum pick radius. */
export const DEFAULT_PIN_DIAMETER = 20;

function sideVisible(elSide: string, viewSide: string): boolean {
  if (elSide === 'both' || elSide === 'unknown' || !elSide) return true;
  return elSide === viewSide;
}

/**
 * Nearest pin within threshold in board space.
 * Threshold: max(pin.diameter/2, DEFAULT_PIN_DIAMETER/2) — matches BoardView click select.
 */
export function hitTestNearestPin(
  board: BoardDocument,
  view: ViewState,
  sx: number,
  sy: number,
  cssW: number,
  cssH: number,
  opts?: { minDiameter?: number },
): Pin | null {
  const pos = screenToBoard(view, sx, sy, cssW, cssH);
  const minD = opts?.minDiameter ?? DEFAULT_PIN_DIAMETER;
  let best: Pin | null = null;
  let bestDist = Infinity;

  for (const pin of board.pins) {
    if (!sideVisible(pin.side, view.side)) continue;
    const dx = pin.pos.x - pos.x;
    const dy = pin.pos.y - pos.y;
    const dist2 = dx * dx + dy * dy;
    const diam = pin.diameter > 0 ? pin.diameter : minD;
    const thresh = Math.max(diam * 0.5, minD * 0.5);
    const thresh2 = thresh * thresh;
    if (dist2 <= thresh2 && dist2 < bestDist) {
      bestDist = dist2;
      best = pin;
    }
  }
  return best;
}
