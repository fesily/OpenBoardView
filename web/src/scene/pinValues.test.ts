import { describe, expect, test } from 'vitest';
import type { BoardDocument, OverlayDocument, Pin } from '../types/board';
import {
  boardPinValue,
  buildNetPropagatedValues,
  editTargetPin,
  findNetSourcePin,
  localPinValue,
  pinOverlayKey,
  resolvePinValue,
} from './pinValues';

function pin(partial: Partial<Pin> & Pick<Pin, 'id'>): Pin {
  return {
    component: 'U1',
    number: '1',
    name: partial.id,
    netId: 1,
    side: 'top',
    pos: { x: 0, y: 0 },
    shape: 'circle',
    diameter: 7,
    size: { x: 0, y: 0 },
    angle: 0,
    ...partial,
  };
}

function board(pins: Pin[]): BoardDocument {
  return {
    boardSchemaVersion: 1,
    boardId: 't',
    sourceName: 't',
    sides: ['top'],
    outline: { points: [], segments: [] },
    components: [],
    nets: [
      { id: 1, name: 'N1', isGround: false },
      { id: 2, name: 'N2', isGround: false },
    ],
    pins,
    tracks: [],
    vias: [],
    arcs: [],
    bounds: { minX: 0, minY: 0, maxX: 1, maxY: 1 },
  };
}

const overlay: OverlayDocument = {
  annotations: [],
  partInfos: {
    U1: {
      pins: {
        A1: { diode: '0.55', voltage: '3.3' },
        B1: { diode: '', ohm: '10k' },
      },
    },
    U2: {
      pins: {
        1: { diode: '0.70', ohm_black: '1M' },
      },
    },
  },
  netInfos: {},
};

describe('pinValues', () => {
  test('pinOverlayKey prefers name then number', () => {
    expect(pinOverlayKey({ name: 'A1', number: '1', id: 'x' })).toBe('A1');
    expect(pinOverlayKey({ name: '', number: '2', id: 'x' })).toBe('2');
  });

  test('boardPinValue reads file-native fields', () => {
    const p = pin({ id: 'X', diode: '86', voltage: '1.8v' });
    expect(boardPinValue(p, 'diode')).toBe('86');
    expect(boardPinValue(p, 'voltage')).toBe('1.8v');
    expect(boardPinValue(p, 'ohm')).toBe('');
  });

  test('overlay overrides board; board fills when overlay empty', () => {
    const p = pin({
      id: 'A1',
      name: 'A1',
      component: 'U1',
      diode: '99', // board file
      voltage: '1.0', // board file, overlay has 3.3
    });
    // overlay diode 0.55 wins over board 99
    expect(localPinValue(p, overlay, 'diode')).toBe('0.55');
    expect(localPinValue(p, overlay, 'voltage')).toBe('3.3');
    // no overlay ohm → empty (board also empty)
    expect(localPinValue(p, overlay, 'ohm')).toBe('');
    // board-only when overlay missing part
    const solo = pin({ id: 'Z', name: 'Z', component: 'NONE', diode: '86' });
    expect(localPinValue(solo, overlay, 'diode')).toBe('86');
    // board without overlay still works
    expect(localPinValue(solo, null, 'diode')).toBe('86');
  });

  test('net propagation uses board diode when overlay empty', () => {
    const pins = [
      pin({ id: 'src', name: 'src', component: 'R1', netId: 1, diode: '86' }),
      pin({ id: 'dst', name: 'dst', component: 'R2', netId: 1 }), // no local
    ];
    const b = board(pins);
    const byNet = buildNetPropagatedValues(b, null, 'diode');
    expect(byNet.get(1)).toBe('86');
    expect(resolvePinValue(pins[0], null, 'diode', byNet)).toBe('86');
    expect(resolvePinValue(pins[1], null, 'diode', byNet)).toBe('86');
  });

  test('localPinValue still reads overlay fields', () => {
    const p = pin({ id: 'A1', name: 'A1', component: 'U1', netId: 1 });
    expect(localPinValue(p, overlay, 'diode')).toBe('0.55');
    expect(localPinValue(p, overlay, 'voltage')).toBe('3.3');
  });

  test('findNetSourcePin / editTargetPin: net_source vs local', () => {
    const src = pin({ id: 'src', name: 'src', component: 'R1', netId: 1, diode: '86' });
    const dst = pin({ id: 'dst', name: 'dst', component: 'R2', netId: 1 });
    const b = board([src, dst]);
    expect(findNetSourcePin(b, null, 1, 'diode')?.id).toBe('src');
    expect(editTargetPin(dst, b, null, 'diode', 'net_source').id).toBe('src');
    expect(editTargetPin(dst, b, null, 'diode', 'local').id).toBe('dst');
    // no source → write on selected
    expect(editTargetPin(dst, b, null, 'voltage', 'net_source').id).toBe('dst');
  });
});
