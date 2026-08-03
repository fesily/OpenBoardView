import { describe, expect, test } from 'vitest';
import type { BoardDocument, Net, Pin } from '../types/board';
import { DEFAULT_PIN_RADIUS, hitTestNearestPin, pinPickRadius } from './hitTest';
import { centerOnBounds, type ViewState } from './transform';

function makePin(partial: Partial<Pin> & Pick<Pin, 'id' | 'pos' | 'diameter'>): Pin {
  return {
    component: null,
    number: '1',
    name: partial.id,
    netId: 1,
    side: 'top',
    shape: 'circle',
    size: { x: 0, y: 0 },
    angle: 0,
    ...partial,
  };
}

function boardWith(pins: Pin[], nets?: Net[]): BoardDocument {
  return {
    boardSchemaVersion: 1,
    boardId: 'test',
    sourceName: 't',
    sides: ['top', 'bottom'],
    outline: { points: [], segments: [] },
    components: [],
    nets: nets ?? [{ id: 1, name: 'NET1', isGround: false }],
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

  test('skips GND and UNCONNECTED pins; keeps signal pads', () => {
    const nets: Net[] = [
      { id: 1, name: 'VCC', isGround: false },
      { id: 2, name: 'GND', isGround: true },
      { id: 3, name: 'GROUND', isGround: true },
      { id: 4, name: 'UNCONNECTED', isGround: false },
    ];
    const board = boardWith(
      [
        makePin({ id: 'gnd', pos: { x: 0, y: 0 }, diameter: 20, netId: 2, type: 'component' }),
        makePin({ id: 'gnd2', pos: { x: 0, y: 0 }, diameter: 20, netId: 3, type: 'component' }),
        makePin({ id: 'nc', pos: { x: 0, y: 0 }, diameter: 20, netId: 4, type: 'not_connected' }),
        // UNCONNECTED net + component type still NC for color/selection.
        makePin({ id: 'bucket', pos: { x: 0, y: 0 }, diameter: 20, netId: 4, type: 'component' }),
        makePin({ id: 'sig', pos: { x: 5, y: 0 }, diameter: 20, netId: 1, type: 'component' }),
      ],
      nets,
    );
    const scale = view.scale;
    const hit = hitTestNearestPin(board, view, 100 + 5 * scale, 100, 200, 200);
    expect(hit?.id).toBe('sig');
    // Pure GND pad under cursor → null
    const onlyGnd = boardWith(
      [makePin({ id: 'g', pos: { x: 0, y: 0 }, diameter: 20, netId: 2, type: 'component' })],
      nets,
    );
    expect(hitTestNearestPin(onlyGnd, view, 100, 100, 200, 200)).toBeNull();
    // True NC → null
    const onlyNc = boardWith(
      [makePin({ id: 'n', pos: { x: 0, y: 0 }, diameter: 20, netId: 4, type: 'not_connected' })],
      nets,
    );
    expect(hitTestNearestPin(onlyNc, view, 100, 100, 200, 200)).toBeNull();
    // Component-typed pin on UNCONNECTED net → also not selectable
    const bucket = boardWith(
      [makePin({ id: 'b', pos: { x: 0, y: 0 }, diameter: 20, netId: 4, type: 'component' })],
      nets,
    );
    expect(hitTestNearestPin(bucket, view, 100, 100, 200, 200)).toBeNull();
  });
});
