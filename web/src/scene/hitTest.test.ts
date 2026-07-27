import { describe, expect, test } from 'vitest';
import type { BoardDocument, Pin } from '../types/board';
import { DEFAULT_PIN_RADIUS, hitTestNearestPin, pinPickRadius } from './hitTest';
import { centerOnBounds, type ViewState } from './transform';

function makePin(partial: Partial<Pin> & Pick<Pin, 'id' | 'pos' | 'diameter'>): Pin {
  return {
    component: null,
    number: '1',
    name: partial.id,
    netId: null,
    side: 'top',
    shape: 'circle',
    size: { x: 0, y: 0 },
    angle: 0,
    ...partial,
  };
}

function boardWith(pins: Pin[]): BoardDocument {
  return {
    boardSchemaVersion: 1,
    boardId: 'test',
    sourceName: 't',
    sides: ['top', 'bottom'],
    outline: { points: [], segments: [] },
    components: [],
    nets: [],
    pins,
    tracks: [],
    vias: [],
    arcs: [],
    bounds: { minX: -50, minY: -50, maxX: 50, maxY: 50 },
  };
}
describe('pinPickRadius', () => {
  test('uses pin.diameter as radius when > 0 (no half)', () => {
    expect(pinPickRadius({ diameter: 14 })).toBe(14);
    expect(pinPickRadius({ diameter: 7 })).toBe(7);
  });

  test('falls back to DEFAULT_PIN_RADIUS when missing/≤0', () => {
    expect(pinPickRadius({ diameter: 0 })).toBe(DEFAULT_PIN_RADIUS);
    expect(pinPickRadius({ diameter: -1 })).toBe(DEFAULT_PIN_RADIUS);
    expect(DEFAULT_PIN_RADIUS).toBe(7);
  });
});

describe('hitTestNearestPin', () => {
  const view: ViewState = centerOnBounds({ minX: -50, minY: -50, maxX: 50, maxY: 50 }, 200, 200);

  test('hits within exported radius, misses beyond', () => {
    const pin = makePin({ id: 'p1', pos: { x: 0, y: 0 }, diameter: 10 });
    const board = boardWith([pin]);
    // board (0,0) maps near canvas center; use screenToBoard inverse via boardToScreen offset
    // Center-on-bounds puts (0,0) at canvas center (100,100) with scale ~2.
    // Hit at board distance 9 (inside r=10) and 11 (outside).
    // With flipY and scale, board delta y flips; use same-axis offset via known transform:
    // screenToBoard at canvas center is board (0,0). Move sx by r*scale.
    const scale = view.scale;
    const hit = hitTestNearestPin(board, view, 100 + 9 * scale, 100, 200, 200);
    const miss = hitTestNearestPin(board, view, 100 + 11 * scale, 100, 200, 200);
    expect(hit?.id).toBe('p1');
    expect(miss).toBeNull();
  });

  test('default radius 7 when diameter ≤ 0', () => {
    const pin = makePin({ id: 'p0', pos: { x: 0, y: 0 }, diameter: 0 });
    const board = boardWith([pin]);
    const scale = view.scale;
    const hit = hitTestNearestPin(board, view, 100 + 6 * scale, 100, 200, 200);
    const miss = hitTestNearestPin(board, view, 100 + 8 * scale, 100, 200, 200);
    expect(hit?.id).toBe('p0');
    expect(miss).toBeNull();
  });
});
