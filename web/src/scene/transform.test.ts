import { describe, expect, test } from 'vitest';
import {
  boardToScreen,
  centerOnBounds,
  flipBoard,
  panByScreen,
  rotateView,
  screenToBoard,
  zoomAt,
  type ViewState,
} from './transform';

describe('transform', () => {
  test('roundtrip center', () => {
    const v = centerOnBounds({ minX: 0, minY: 0, maxX: 100, maxY: 50 }, 200, 100);
    const s = boardToScreen(v, 50, 25, 200, 100);
    const b = screenToBoard(v, s.x, s.y, 200, 100);
    expect(b.x).toBeCloseTo(50, 1);
    expect(b.y).toBeCloseTo(25, 1);
  });

  test('center maps bounds midpoint to canvas center', () => {
    const v = centerOnBounds({ minX: 0, minY: 0, maxX: 100, maxY: 50 }, 200, 100);
    const s = boardToScreen(v, 50, 25, 200, 100);
    expect(s.x).toBeCloseTo(100, 5);
    expect(s.y).toBeCloseTo(50, 5);
  });

  test('roundtrip under rotation and bottom side', () => {
    let v: ViewState = centerOnBounds({ minX: -10, minY: -20, maxX: 90, maxY: 80 }, 400, 300);
    v = rotateView(v, 1);
    v = flipBoard(v);
    const pts = [
      { x: 0, y: 0 },
      { x: 40, y: 30 },
      { x: -5, y: 70 },
    ];
    for (const p of pts) {
      const s = boardToScreen(v, p.x, p.y, 400, 300);
      const b = screenToBoard(v, s.x, s.y, 400, 300);
      expect(b.x).toBeCloseTo(p.x, 5);
      expect(b.y).toBeCloseTo(p.y, 5);
    }
  });

  test('zoomAt keeps cursor board point fixed', () => {
    const v = centerOnBounds({ minX: 0, minY: 0, maxX: 200, maxY: 100 }, 400, 200);
    const sx = 120;
    const sy = 80;
    const before = screenToBoard(v, sx, sy, 400, 200);
    const z = zoomAt(v, sx, sy, 400, 200, 2);
    const after = screenToBoard(z, sx, sy, 400, 200);
    expect(after.x).toBeCloseTo(before.x, 5);
    expect(after.y).toBeCloseTo(before.y, 5);
    expect(z.scale).toBeCloseTo(v.scale * 2, 5);
  });

  test('panByScreen moves look-at so content follows pointer', () => {
    const v = centerOnBounds({ minX: 0, minY: 0, maxX: 100, maxY: 100 }, 200, 200);
    const mid = boardToScreen(v, 50, 50, 200, 200);
    const panned = panByScreen(v, 20, 0, 200, 200);
    const mid2 = boardToScreen(panned, 50, 50, 200, 200);
    expect(mid2.x).toBeCloseTo(mid.x + 20, 5);
    expect(mid2.y).toBeCloseTo(mid.y, 5);
  });

  test('rotateView keeps panned screen-center board point fixed', () => {
    const W = 400;
    const H = 200;
    let v = centerOnBounds({ minX: 0, minY: 0, maxX: 200, maxY: 100 }, W, H);
    // Pan so look-at is no longer board midpoint.
    v = { ...v, mx: v.mx - 30 / v.scale, my: v.my + 15 / v.scale };
    const before = screenToBoard(v, W / 2, H / 2, W, H);
    const rotated = rotateView(v, 1, W, H);
    const after = screenToBoard(rotated, W / 2, H / 2, W, H);
    expect(after.x).toBeCloseTo(before.x, 5);
    expect(after.y).toBeCloseTo(before.y, 5);
    expect(rotated.rotation).toBe(((v.rotation + 1) & 3) as 0 | 1 | 2 | 3);
    // Former center point stays at canvas center in screen space.
    const s = boardToScreen(rotated, before.x, before.y, W, H);
    expect(s.x).toBeCloseTo(W / 2, 5);
    expect(s.y).toBeCloseTo(H / 2, 5);
  });

  test('flipBoard toggles side and rotates 180 when flipY', () => {
    const v = centerOnBounds({ minX: 0, minY: 0, maxX: 10, maxY: 10 }, 100, 100);
    const f = flipBoard(v);
    expect(f.side).toBe('bottom');
    expect(f.rotation).toBe(2);
    const f2 = flipBoard(f);
    expect(f2.side).toBe('top');
    expect(f2.rotation).toBe(0);
  });
});
