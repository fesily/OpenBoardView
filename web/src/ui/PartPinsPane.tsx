import { useMemo } from 'react';
import { isGroundNet, isNotConnectedPin, isTestPadPin } from '../scene/netKinds';
import { netDisplayName } from '../scene/netDisplay';
import type { BoardDocument, OverlayDocument, Pin } from '../types/board';

export interface PartPinsPaneProps {
  board: BoardDocument;
  overlay?: OverlayDocument | null;
  partName: string | null;
  selectedPin: Pin | null;
  onSelectPin: (pin: Pin) => void;
  onClear: () => void;
}

/** Canvas/desktop label: overlay show_name > board show_name > name > number. */
function pinLabel(pin: Pin, overlay?: OverlayDocument | null): string {
  const key = pin.name || pin.number || '';
  const ov = key ? overlay?.partInfos?.[pin.component ?? '']?.pins?.[key]?.show_name : undefined;
  if (ov && ov.trim()) return ov.trim();
  if (pin.show_name && pin.show_name.trim()) return pin.show_name.trim();
  if (pin.name && pin.name.trim()) return pin.name.trim();
  return pin.number || pin.id;
}

/** Part header + list box of the part's pins (desktop Info Panel pin table). */
export default function PartPinsPane({
  board,
  overlay = null,
  partName,
  selectedPin,
  onSelectPin,
  onClear,
}: PartPinsPaneProps) {
  const pinById = useMemo(() => {
    const m = new Map<string, Pin>();
    for (const p of board.pins ?? []) m.set(p.id, p);
    return m;
  }, [board.pins]);

  const netById = useMemo(() => {
    const m = new Map<number, (typeof board.nets)[number]>();
    for (const n of board.nets ?? []) m.set(n.id, n);
    return m;
  }, [board.nets]);

  const part = useMemo(
    () => (partName ? ((board.components ?? []).find((c) => c.name === partName) ?? null) : null),
    [board.components, partName],
  );

  /**
   * Board-file pin order (`part.pins` ids); fallback to global pin scan.
   * GND / NC pins are omitted — they cannot be selected (desktop
   * isNonSelectablePin), so they would only be dead rows here.
   */
  const rows = useMemo(() => {
    if (!partName) return [];
    const ids = part?.pins ?? [];
    const ordered: Pin[] =
      ids.length > 0
        ? ids.map((id) => pinById.get(id)).filter((p): p is Pin => !!p)
        : (board.pins ?? []).filter((p) => p.component === partName);
    return ordered.filter((pin) => {
      const net = pin.netId != null ? netById.get(pin.netId) : undefined;
      return !isGroundNet(net) && !isNotConnectedPin(pin, net);
    });
  }, [board.pins, part?.pins, partName, pinById, netById]);

  if (!partName) return null;

  return (
    <div className="right-section part-pane">
      <div className="part-pane-header row">
        <h2 className="right-section-title part-pane-title">{partName}</h2>
        <span className="muted mono part-pane-count">{rows.length} pin(s)</span>
        <span className="part-pane-spacer" />
        <button
          type="button"
          className="part-pane-clear"
          title="Clear part selection"
          aria-label="Clear part selection"
          onClick={onClear}
        >
          ×
        </button>
      </div>
      {part && (part.mfgcode || part.type || part.side) && (
        <p className="part-pane-meta mono">
          {[part.type, part.mount, part.side].filter(Boolean).join(' · ')}
          {part.mfgcode ? ` · ${part.mfgcode}` : ''}
        </p>
      )}
      {!part && <p className="muted">Part not found.</p>}
      {part && rows.length === 0 && <p className="muted">No pins in this part.</p>}
      {rows.length > 0 && (
        <ul className="part-pin-list" aria-label={`Pins of ${partName}`}>
          {rows.map((pin) => {
            const net = pin.netId != null ? netById.get(pin.netId) : undefined;
            const selected = selectedPin?.id === pin.id;
            const tag = isTestPadPin(pin) ? 'TP' : '';
            return (
              <li key={pin.id}>
                <button
                  type="button"
                  className={'part-pin-row' + (selected ? ' selected' : '')}
                  aria-pressed={selected}
                  title={`${pin.id}${net ? ` · net ${net.name}` : ''}`}
                  onClick={() => onSelectPin(pin)}
                >
                  <span className="part-pin-num mono">{pin.number || pin.name || pin.id}</span>
                  <span className="part-pin-name">{pinLabel(pin, overlay)}</span>
                  <span className="part-pin-net mono">
                    {net ? netDisplayName(net, overlay) : '—'}
                  </span>
                  {tag && <span className="part-pin-tag">{tag}</span>}
                </button>
              </li>
            );
          })}
        </ul>
      )}
    </div>
  );
}
