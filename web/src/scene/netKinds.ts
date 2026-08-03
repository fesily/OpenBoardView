import type { Net, Pin } from '../types/board';

/**
 * Special nets / pin types for selection + solid pad colors.
 * Mirrors desktop:
 * - is_ground ⇔ name == "GND" || name == "GROUND"
 * - kPinTypeNotConnected when file net name is empty/"UNCONNECTED…"
 * - Shared UNCONNECTED net object is also used for missing nets; pin.type
 *   distinguishes true NC from "bucket" attachment for parse, but for
 *   display we still color UNCONNECTED-net pins as NC.
 */
export function isGroundNet(net: Pick<Net, 'name' | 'isGround'> | null | undefined): boolean {
  if (!net) return false;
  if (net.isGround) return true;
  const n = (net.name || '').trim().toUpperCase();
  return n === 'GND' || n === 'GROUND';
}

export function isUnconnectedNetName(net: Pick<Net, 'name'> | null | undefined): boolean {
  if (!net) return false;
  const n = (net.name || '').trim().toUpperCase();
  return n === 'UNCONNECTED' || n.startsWith('UNCONNECTED');
}

/**
 * Unconnected pad (solid gray).
 * - pin.type === not_connected (explicit file NC)
 * - net name UNCONNECTED / UNCONNECTED* — including component-typed pins
 *   attached to the shared NC bucket (common on BVR missing PIN_NET).
 */
export function isNotConnectedPin(
  pin: Pick<Pin, 'type' | 'netId'> | null | undefined,
  net: Pick<Net, 'name' | 'isGround'> | null | undefined,
): boolean {
  if (!pin) return false;
  const t = (pin.type || '').toLowerCase();
  if (t === 'not_connected') return true;
  // Net-name wins for color: UNCONNECTED pins look NC even if type is component.
  if (isUnconnectedNetName(net)) return true;
  if (t === 'component' || t === 'test_pad' || t === 'via') return false;
  // type unknown / missing: legacy boards without pin.type
  if (pin.netId == null && !net) return true;
  return false;
}

export function isTestPadPin(pin: Pick<Pin, 'type'> | null | undefined): boolean {
  return (pin?.type || '').toLowerCase() === 'test_pad';
}

/** Pins that should not become m_pinSelected / same-net expand. */
export function isNonSelectablePin(
  pin: Pick<Pin, 'type' | 'netId'> | null | undefined,
  net: Pick<Net, 'name' | 'isGround'> | null | undefined,
): boolean {
  if (isGroundNet(net)) return true;
  if (isNotConnectedPin(pin, net)) return true;
  return false;
}

export function netByIdMap(nets: readonly Net[] | undefined | null): Map<number, Net> {
  const m = new Map<number, Net>();
  for (const n of nets ?? []) m.set(n.id, n);
  return m;
}
