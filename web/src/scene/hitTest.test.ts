import { describe, expect, test } from 'vitest';
import type { BoardDocument, Component, Net, Pin, Point } from '../types/board';
import {
  DEFAULT_PIN_RADIUS,
  hitTestComponent,
  hitTestNearestPin,
  pinPickRadius,
} from './hitTest';
import { boardToScreen, centerOnBounds, type ViewState } from './transform';

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

function makeComponent(
  partial: Partial<Component> & Pick<Component, 'name' | 'outline'>,
): Component {
  return {
    side: 'top',
    mount: 'smd',
    type: 'ic',
    mfgcode: '',
    center: { x: 0, y: 0 },
    pins: [],
    ...partial,
  };
}

function square(half: number, center: Point = { x: 0, y: 0 }): Point[] {
  return [
    { x: center.x - half, y: center.y - half },
    { x: center.x + half, y: center.y - half },
    { x: center.x + half, y: center.y + half },
    { x: center.x - half, y: center.y + half },
  ];
}

function boardWith(pins: Pin[], nets?: Net[], components: Component[] = []): BoardDocument {
  return {
    boardSchemaVersion: 1,
    boardId: 'test',
    sourceName: 't',
    sides: ['top', 'bottom'],
    outline: { points: [], segments: [] },
    components,
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

describe('hitTestComponent', () => {
  const view: ViewState = centerOnBounds({ minX: -50, minY: -50, maxX: 50, maxY: 50 }, 200, 200);
  const at = (x: number, y: number) => boardToScreen(view, x, y, 200, 200);

  test('returns the part whose outline contains the point, null outside', () => {
    const comp = makeComponent({ name: 'U1', outline: square(10) });
    const board = boardWith([], undefined, [comp]);
    const inside = at(0, 0);
    const outside = at(20, 0);
    expect(hitTestComponent(board, view, inside.x, inside.y, 200, 200)?.name).toBe('U1');
    expect(hitTestComponent(board, view, outside.x, outside.y, 200, 200)).toBeNull();
  });

  test('skips parts on the other board side', () => {
    const bottom = makeComponent({ name: 'U2', side: 'bottom', outline: square(10) });
    const board = boardWith([], undefined, [bottom]);
    const p = at(0, 0);
    expect(hitTestComponent(board, view, p.x, p.y, 200, 200)).toBeNull();
    const bottomView: ViewState = { ...view, side: 'bottom' };
    expect(hitTestComponent(board, bottomView, p.x, p.y, 200, 200)?.name).toBe('U2');
  });

  test('skips board and dummy pseudo-parts', () => {
    const boardPart = makeComponent({ name: 'BOARD', type: 'board', outline: square(10) });
    const dummy = makeComponent({ name: 'TP1', type: 'dummy', outline: square(5) });
    const board = boardWith([], undefined, [boardPart, dummy]);
    const p = at(0, 0);
    expect(hitTestComponent(board, view, p.x, p.y, 200, 200)).toBeNull();
  });

  test('overlapping hulls: later array entry (drawn on top) wins', () => {
    const outer = makeComponent({ name: 'OUTER', outline: square(20) });
    const inner = makeComponent({ name: 'INNER', outline: square(5) });
    const board = boardWith([], undefined, [outer, inner]);
    const p = at(0, 0);
    expect(hitTestComponent(board, view, p.x, p.y, 200, 200)?.name).toBe('INNER');
    const reversed = boardWith([], undefined, [inner, outer]);
    expect(hitTestComponent(reversed, view, p.x, p.y, 200, 200)?.name).toBe('OUTER');
  });

  test('ignores outlines with fewer than 3 points', () => {
    const line = makeComponent({
      name: 'LINE',
      outline: [
        { x: -10, y: 0 },
        { x: 10, y: 0 },
      ],
    });
    const board = boardWith([], undefined, [line]);
    const p = at(0, 0);
    expect(hitTestComponent(board, view, p.x, p.y, 200, 200)).toBeNull();
  });
});
