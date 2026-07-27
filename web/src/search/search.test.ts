import { describe, expect, test } from 'vitest';
import type { BoardDocument, Component, Net, Pin } from '../types/board';
import {
  focusPointForResult,
  highlightFromMatches,
  matchesSearch,
  searchNets,
  searchParts,
  type SearchMode,
} from './search';

function part(name: string, center = { x: 0, y: 0 }): Component {
  return {
    name,
    side: 'top',
    mount: 'smd',
    type: 'IC',
    mfgcode: '',
    center,
    outline: [],
    pins: [],
  };
}

function net(id: number, name: string): Net {
  return { id, name, isGround: false };
}

function pin(partial: Partial<Pin> & Pick<Pin, 'id'>): Pin {
  return {
    component: null,
    number: '1',
    name: partial.id,
    netId: null,
    side: 'top',
    pos: { x: 0, y: 0 },
    shape: 'circle',
    diameter: 7,
    size: { x: 0, y: 0 },
    angle: 0,
    ...partial,
  };
}

function board(components: Component[], nets: Net[], pins: Pin[] = []): BoardDocument {
  return {
    boardSchemaVersion: 1,
    boardId: 't',
    sourceName: 't',
    bounds: { minX: 0, minY: 0, maxX: 100, maxY: 100 },
    sides: ['top'],
    outline: { points: [], segments: [] },
    nets,
    components,
    pins,
    tracks: [],
    vias: [],
    arcs: [],
  };
}

const sample = board(
  [part('R1'), part('R10'), part('U1'), part('C12'), part('r2')],
  [net(1, 'GND'), net(2, 'VCC'), net(3, 'NET_GND_SENSE'), net(4, 'gnd_aux')],
);

describe('matchesSearch', () => {
  const modes: SearchMode[] = ['sub', 'prefix', 'whole'];

  test('empty needle never matches', () => {
    for (const mode of modes) {
      expect(matchesSearch('R1', '', mode)).toBe(false);
    }
  });

  test('sub: case-insensitive substring', () => {
    expect(matchesSearch('R10', 'r1', 'sub')).toBe(true);
    expect(matchesSearch('R10', '10', 'sub')).toBe(true);
    expect(matchesSearch('R10', 'R2', 'sub')).toBe(false);
  });

  test('prefix: only at start', () => {
    expect(matchesSearch('R10', 'r1', 'prefix')).toBe(true);
    expect(matchesSearch('R10', '10', 'prefix')).toBe(false);
    expect(matchesSearch('AR10', 'r1', 'prefix')).toBe(false);
  });

  test('whole: full equality case-insensitive', () => {
    expect(matchesSearch('GND', 'gnd', 'whole')).toBe(true);
    expect(matchesSearch('GND', 'GN', 'whole')).toBe(false);
    expect(matchesSearch('GND', 'GNDA', 'whole')).toBe(false);
  });
});

describe('searchParts', () => {
  test('sub finds embedded and case-insensitive', () => {
    expect(searchParts(sample, 'r1', 'sub', 50)).toEqual(['R1', 'R10']);
  });

  test('prefix only leading', () => {
    expect(searchParts(sample, 'R1', 'prefix', 50)).toEqual(['R1', 'R10']);
    expect(searchParts(sample, '1', 'prefix', 50)).toEqual([]);
  });

  test('whole exact name only', () => {
    expect(searchParts(sample, 'R1', 'whole', 50)).toEqual(['R1']);
    expect(searchParts(sample, 'r2', 'whole', 50)).toEqual(['r2']);
    expect(searchParts(sample, 'R', 'whole', 50)).toEqual([]);
  });

  test('empty query returns empty', () => {
    expect(searchParts(sample, '', 'sub', 50)).toEqual([]);
  });

  test('respects limit', () => {
    expect(searchParts(sample, 'r', 'sub', 2)).toHaveLength(2);
  });
});

describe('searchNets', () => {
  test('sub finds substring nets', () => {
    expect(searchNets(sample, 'gnd', 'sub', 50)).toEqual(['GND', 'NET_GND_SENSE', 'gnd_aux']);
  });

  test('prefix', () => {
    expect(searchNets(sample, 'gnd', 'prefix', 50)).toEqual(['GND', 'gnd_aux']);
  });

  test('whole', () => {
    expect(searchNets(sample, 'GND', 'whole', 50)).toEqual(['GND']);
    expect(searchNets(sample, 'vcc', 'whole', 50)).toEqual(['VCC']);
  });

  test('limit and empty', () => {
    expect(searchNets(sample, 'gnd', 'sub', 1)).toEqual(['GND']);
    expect(searchNets(sample, '', 'sub', 10)).toEqual([]);
  });
});

describe('highlightFromMatches / focusPointForResult', () => {
  const b = board(
    [part('U1', { x: 10, y: 20 }), part('R1', { x: 5, y: 5 })],
    [net(7, 'VCC'), net(8, 'GND')],
    [
      pin({ id: 'U1.1', component: 'U1', netId: 7, pos: { x: 11, y: 21 } }),
      pin({ id: 'U1.2', component: 'U1', netId: 8, pos: { x: 12, y: 22 } }),
      pin({ id: 'R1.1', component: 'R1', netId: 7, pos: { x: 6, y: 6 } }),
    ],
  );

  test('highlights part pins and net pins', () => {
    const h = highlightFromMatches(b, ['U1'], ['GND']);
    expect([...h.partNames]).toEqual(['U1']);
    expect(h.pinIds.has('U1.1')).toBe(true);
    expect(h.pinIds.has('U1.2')).toBe(true); // part pin + GND pin
    expect(h.pinIds.has('R1.1')).toBe(false);
  });

  test('focus part uses center; net uses first pin', () => {
    expect(focusPointForResult(b, 'part', 'U1')).toEqual({ x: 10, y: 20 });
    expect(focusPointForResult(b, 'net', 'VCC')).toEqual({ x: 11, y: 21 });
    expect(focusPointForResult(b, 'net', 'MISSING')).toBeNull();
  });
});
