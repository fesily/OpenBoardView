import type { Net } from '../types/board';

/**
 * Special nets that cannot be selected and use solid pad colors.
 * Mirrors desktop:
 * - is_ground ⇔ name == "GND" || name == "GROUND"
 * - UNCONNECTED prefix (BRDBoard::kNetUnconnectedPrefix)
 */
export function isGroundNet(net: Pick<Net, 'name' | 'isGround'> | null | undefined): boolean {
  if (!net) return false;
  if (net.isGround) return true;
  const n = (net.name || '').trim().toUpperCase();
  return n === 'GND' || n === 'GROUND';
}

export function isUnconnectedNet(net: Pick<Net, 'name'> | null | undefined): boolean {
  if (!net) return false;
  const n = (net.name || '').trim().toUpperCase();
  return n === 'UNCONNECTED' || n.startsWith('UNCONNECTED');
}

/** Pins on these nets are not selectable (no same-net highlight). */
export function isNonSelectableNet(net: Pick<Net, 'name' | 'isGround'> | null | undefined): boolean {
  return isGroundNet(net) || isUnconnectedNet(net);
}

export function netByIdMap(nets: readonly Net[] | undefined | null): Map<number, Net> {
  const m = new Map<number, Net>();
  for (const n of nets ?? []) m.set(n.id, n);
  return m;
}
