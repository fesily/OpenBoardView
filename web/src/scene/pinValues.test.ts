import { describe, expect, test } from 'vitest';
import type { BoardDocument, OverlayDocument, Pin } from '../types/board';
import {
  buildNetPropagatedValues,
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

  test('localPinValue reads overlay field', () => {
    const p = pin({ id: 'A1', name: 'A1', component: 'U1', netId: 1 });
    expect(localPinValue(p, overlay, 'diode')).toBe('0.55');
    expect(localPinValue(p, overlay, 'voltage')).toBe('3.3');
    expect(localPinValue(p, overlay, 'ohm')).toBe('');
  });

  test('net propagation fills missing pins; local wins', () => {
    const pins = [
      pin({ id: 'A1', name: 'A1', component: 'U1', netId: 1 }),
      pin({ id: 'Z9', name: 'Z9', component: 'U1', netId: 1 }), // no overlay
      pin({ id: '1', name: '1', number: '1', component: 'U2', netId: 1 }), // other part same net
    ];
    const b = board(pins);
    const byNet = buildNetPropagatedValues(b, overlay, 'diode');
    expect(byNet.get(1)).toBe('0.55'); // first non-empty on net

    expect(resolvePinValue(pins[0], overlay, 'diode', byNet)).toBe('0.55');
    expect(resolvePinValue(pins[1], overlay, 'diode', byNet)).toBe('0.55'); // propagated
    // local on U2.1 is 0.70 — wins over net seed
    expect(resolvePinValue(pins[2], overlay, 'diode', byNet)).toBe('0.70');
  });

  test('ohm_black mode independent of diode', () => {
    const p = pin({ id: '1', name: '1', number: '1', component: 'U2', netId: 2 });
    const byNet = buildNetPropagatedValues(board([p]), overlay, 'ohm_black');
    expect(resolvePinValue(p, overlay, 'ohm_black', byNet)).toBe('1M');
    expect(resolvePinValue(p, overlay, 'diode', buildNetPropagatedValues(board([p]), overlay, 'diode'))).toBe(
      '0.70',
    );
  });
});
