import type { BoardDocument, OverlayDocument, Pin, PinInfo } from '../types/board';

/** Overlay / board pin scalar shown on the pad instead of net name. */
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

/** PartInfos pin key — same as InfoPane / overlay_store / desktop ReloadPinInfos. */
export function pinOverlayKey(pin: Pick<Pin, 'name' | 'number' | 'id'>): string {
  return pin.name || pin.number || pin.id || '';
}

function trimVal(raw: unknown): string {
  if (raw == null) return '';
  const t = String(raw).trim();
  return t;
}

/** Value from board JSON (file-native PIN_* fields). */
export function boardPinValue(pin: Pin, mode: PinValueMode): string {
  return trimVal(pin[mode]);
}

/** Value from overlay PartInfos only. */
export function overlayPinValue(
  pin: Pin,
  overlay: OverlayDocument | null | undefined,
  mode: PinValueMode,
): string {
  if (!overlay || !pin.component) return '';
  const key = pinOverlayKey(pin);
  if (!key) return '';
  const info: PinInfo | undefined = overlay.partInfos[pin.component]?.pins?.[key];
  return trimVal(info?.[mode]);
}

/**
 * Local display value for a pin:
 * 1. overlay PartInfos (desktop ReloadPinInfos overrides board)
 * 2. board-file field (PIN_DIODE_VALUE etc.)
 */
export function localPinValue(
  pin: Pin,
  overlay: OverlayDocument | null | undefined,
  mode: PinValueMode,
): string {
  const ov = overlayPinValue(pin, overlay, mode);
  if (ov) return ov;
  return boardPinValue(pin, mode);
}

/**
 * First non-empty value per net for `mode` (net propagation seed).
 * Local pin value always wins over this map when present.
 * Seeds from board+overlay so file diode values propagate across the net.
 */
export function buildNetPropagatedValues(
  board: BoardDocument,
  overlay: OverlayDocument | null | undefined,
  mode: PinValueMode,
): Map<number, string> {
  const byNet = new Map<number, string>();
  for (const pin of board.pins ?? []) {
    if (pin.netId == null) continue;
    const v = localPinValue(pin, overlay, mode);
    if (!v || byNet.has(pin.netId)) continue;
    byNet.set(pin.netId, v);
  }
  return byNet;
}

/** Local value, else same-net propagated value. */
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
