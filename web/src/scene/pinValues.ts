import type { BoardDocument, OverlayDocument, Pin, PinInfo } from '../types/board';

/** Overlay pin scalar shown on the pad instead of net name. */
export type PinValueMode = 'diode' | 'voltage' | 'ohm' | 'ohm_black';

export const PIN_VALUE_MODES: readonly PinValueMode[] = [
  'diode',
  'voltage',
  'ohm',
  'ohm_black',
] as const;

export const PIN_VALUE_MODE_LABEL: Record<PinValueMode, string> = {
  diode: 'Diode',
  voltage: 'Voltage',
  ohm: 'Ohm',
  ohm_black: 'Ohm black',
};

/** PartInfos pin key — same as InfoPane / overlay_store. */
export function pinOverlayKey(pin: Pick<Pin, 'name' | 'number' | 'id'>): string {
  return pin.name || pin.number || pin.id || '';
}

export function localPinValue(
  pin: Pin,
  overlay: OverlayDocument | null | undefined,
  mode: PinValueMode,
): string {
  if (!overlay || !pin.component) return '';
  const key = pinOverlayKey(pin);
  if (!key) return '';
  const info: PinInfo | undefined = overlay.partInfos[pin.component]?.pins?.[key];
  const raw = info?.[mode];
  if (raw == null) return '';
  const t = String(raw).trim();
  return t;
}

/**
 * First non-empty value per net for `mode` (net propagation seed).
 * Local pin value always wins over this map when present.
 */
export function buildNetPropagatedValues(
  board: BoardDocument,
  overlay: OverlayDocument | null | undefined,
  mode: PinValueMode,
): Map<number, string> {
  const byNet = new Map<number, string>();
  if (!overlay) return byNet;
  for (const pin of board.pins ?? []) {
    if (pin.netId == null) continue;
    const v = localPinValue(pin, overlay, mode);
    if (!v || byNet.has(pin.netId)) continue;
    byNet.set(pin.netId, v);
  }
  return byNet;
}

/** Local overlay value, else same-net propagated value. */
export function resolvePinValue(
  pin: Pin,
  overlay: OverlayDocument | null | undefined,
  mode: PinValueMode,
  netValues: Map<number, string>,
): string {
  const local = localPinValue(pin, overlay, mode);
  if (local) return local;
  if (pin.netId != null) return netValues.get(pin.netId) ?? '';
  return '';
}
