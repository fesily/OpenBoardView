import type { BoardDocument, Component, Pin } from '../types/board';
import { isNonSelectablePin, netByIdMap } from './netKinds';
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
 * GND and true NC (pin.type not_connected) are not selectable.
 */
export function hitTestNearestPin(
  board: BoardDocument,
  view: ViewState,
  sx: number,
  sy: number,
  cssW: number,
  cssH: number,
  opts?: { minRadius?: number; allowNonSelectable?: boolean },
): Pin | null {
  const pos = screenToBoard(view, sx, sy, cssW, cssH);
  const fallback = opts?.minRadius ?? DEFAULT_PIN_RADIUS;
  const nets = opts?.allowNonSelectable ? null : netByIdMap(board.nets);
  let best: Pin | null = null;
  let bestDist = Infinity;

  for (const pin of board.pins) {
    if (!sideVisible(pin.side, view.side)) continue;
    if (nets) {
      const net = pin.netId != null ? nets.get(pin.netId) : undefined;
      if (isNonSelectablePin(pin, net)) continue;
    }
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

/**
 * Topmost component hull containing the screen point.
 * Port of desktop BoardView hull pick (ray casting); parts not on the visible
 * side and `board` / `dummy` pseudo-parts are skipped. Later components win
 * because drawParts paints in array order (last drawn = on top).
 */
export function hitTestComponent(
  board: BoardDocument,
  view: ViewState,
  sx: number,
  sy: number,
  cssW: number,
  cssH: number,
): Component | null {
  const pos = screenToBoard(view, sx, sy, cssW, cssH);
  let best: Component | null = null;

  for (const part of board.components ?? []) {
    if (!sideVisible(part.side, view.side)) continue;
    const type = (part.type || '').toLowerCase();
    if (type === 'board' || type === 'dummy') continue;
    const poly = part.outline;
    if (!poly || poly.length < 3) continue;

    let inside = false;
    for (let a = 0, b = poly.length - 1; a < poly.length; b = a++) {
      if (
        (poly[a].y > pos.y) !== (poly[b].y > pos.y) &&
        pos.x <
          ((poly[b].x - poly[a].x) * (pos.y - poly[a].y)) / (poly[b].y - poly[a].y) +
            poly[a].x
      ) {
        inside = !inside;
      }
    }
    if (inside) best = part;
  }

  return best;
}
