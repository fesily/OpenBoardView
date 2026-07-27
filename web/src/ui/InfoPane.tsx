import { useEffect, useMemo, useState } from 'react';
import {
  ApiError,
  deleteAnnotation,
  patchAnnotation,
  putOverlays,
} from '../api/client';
import type {
  BoardDocument,
  Net,
  OverlayAnnotation,
  OverlayDocument,
  PartInfo,
  Pin,
  PinInfo,
} from '../types/board';

export interface InfoPaneProps {
  boardId: string;
  board: BoardDocument;
  selectedPin: Pin | null;
  overlay: OverlayDocument | null;
  overlayLoading?: boolean;
  overlayError?: string | null;
  onOverlayChange: (doc: OverlayDocument) => void;
  /** Reload overlays from server (e.g. after external write). */
  onReloadOverlays?: () => void;
}

/** Desktop PartInfos pin map key is pin.name (falls back to number). */
function pinOverlayKey(pin: Pin): string {
  return pin.name || pin.number || pin.id;
}

function cloneOverlay(doc: OverlayDocument): OverlayDocument {
  return {
    annotations: doc.annotations.map((a) => ({ ...a })),
    partInfos: Object.fromEntries(
      Object.entries(doc.partInfos).map(([k, p]) => [
        k,
        {
          ...p,
          pins: p.pins
            ? Object.fromEntries(Object.entries(p.pins).map(([pk, pi]) => [pk, { ...pi }]))
            : undefined,
        },
      ]),
    ),
    netInfos: Object.fromEntries(Object.entries(doc.netInfos).map(([k, n]) => [k, { ...n }])),
  };
}

export default function InfoPane({
  boardId,
  board,
  selectedPin,
  overlay,
  overlayLoading = false,
  overlayError = null,
  onOverlayChange,
  onReloadOverlays,
}: InfoPaneProps) {
  const netById = useMemo(() => {
    const m = new Map<number, Net>();
    for (const n of board.nets) m.set(n.id, n);
    return m;
  }, [board.nets]);

  const part = useMemo(() => {
    if (!selectedPin?.component) return null;
    return board.components.find((c) => c.name === selectedPin.component) ?? null;
  }, [board.components, selectedPin]);

  const net = useMemo(() => {
    if (selectedPin?.netId == null) return null;
    return netById.get(selectedPin.netId) ?? null;
  }, [netById, selectedPin]);

  const pinKey = selectedPin ? pinOverlayKey(selectedPin) : '';
  const partName = selectedPin?.component ?? '';
  const pinInfo =
    selectedPin && partName
      ? (overlay?.partInfos[partName]?.pins?.[pinKey] ?? {})
      : ({} as PinInfo);
  const partInfo = partName ? overlay?.partInfos[partName] : undefined;
  const netInfo = net ? overlay?.netInfos[net.name] : undefined;

  const [note, setNote] = useState(pinInfo.note ?? '');
  const [showName, setShowName] = useState(pinInfo.show_name ?? '');
  const [voltage, setVoltage] = useState(pinInfo.voltage ?? '');
  const [diode, setDiode] = useState(pinInfo.diode ?? '');
  const [savingPin, setSavingPin] = useState(false);
  const [pinMsg, setPinMsg] = useState<string | null>(null);
  const [partType, setPartType] = useState(partInfo?.part_type ?? '');
  const [partAngle, setPartAngle] = useState(
    partInfo?.angle != null ? String(partInfo.angle) : '',
  );
  const [savingPart, setSavingPart] = useState(false);
  const [partMsg, setPartMsg] = useState<string | null>(null);
  const [netShowName, setNetShowName] = useState(netInfo?.showname ?? '');
  const [netNote, setNetNote] = useState(netInfo?.note ?? '');
  const [savingNet, setSavingNet] = useState(false);
  const [netMsg, setNetMsg] = useState<string | null>(null);
  const [annDrafts, setAnnDrafts] = useState<Record<number, string>>({});
  const [annBusy, setAnnBusy] = useState<number | null>(null);
  const [annMsg, setAnnMsg] = useState<string | null>(null);

  // Sync local editors when selection or overlay reloads.
  useEffect(() => {
    setNote(pinInfo.note ?? '');
    setShowName(pinInfo.show_name ?? '');
    setVoltage(pinInfo.voltage ?? '');
    setDiode(pinInfo.diode ?? '');
    setPinMsg(null);
  }, [selectedPin?.id, pinInfo.note, pinInfo.show_name, pinInfo.voltage, pinInfo.diode]);

  useEffect(() => {
    setPartType(partInfo?.part_type ?? '');
    setPartAngle(partInfo?.angle != null ? String(partInfo.angle) : '');
    setPartMsg(null);
  }, [partName, partInfo?.part_type, partInfo?.angle]);

  useEffect(() => {
    setNetShowName(netInfo?.showname ?? '');
    setNetNote(netInfo?.note ?? '');
    setNetMsg(null);
  }, [net?.name, netInfo?.showname, netInfo?.note]);

  useEffect(() => {
    const next: Record<number, string> = {};
    for (const a of overlay?.annotations ?? []) next[a.id] = a.note;
    setAnnDrafts(next);
    setAnnMsg(null);
  }, [overlay]);

  const savePinOverlay = async () => {
    if (!selectedPin || !partName) {
      setPinMsg('Select a pin on a part to edit overlay fields.');
      return;
    }
    setSavingPin(true);
    setPinMsg(null);
    try {
      const base = cloneOverlay(
        overlay ?? { annotations: [], partInfos: {}, netInfos: {} },
      );
      const partEntry: PartInfo = { ...(base.partInfos[partName] ?? {}) };
      const pins = { ...(partEntry.pins ?? {}) };
      const prev = { ...(pins[pinKey] ?? {}) };
      const nextPin: PinInfo = { ...prev };
      const setField = (key: keyof PinInfo, value: string) => {
        const t = value.trim();
        if (t) (nextPin as Record<string, string>)[key] = t;
        else delete (nextPin as Record<string, string | undefined>)[key];
      };
      setField('note', note);
      setField('show_name', showName);
      setField('voltage', voltage);
      setField('diode', diode);
      // Drop empty pin entries to keep YAML small.
      const hasAny = Object.values(nextPin).some((v) => v != null && String(v).length > 0);
      if (hasAny) pins[pinKey] = nextPin;
      else delete pins[pinKey];
      partEntry.pins = pins;
      if (
        Object.keys(pins).length === 0 &&
        !partEntry.part_type &&
        partEntry.angle == null
      ) {
        delete base.partInfos[partName];
      } else {
        base.partInfos[partName] = partEntry;
      }
      const updated = await putOverlays(boardId, {
        partInfos: base.partInfos,
        netInfos: base.netInfos,
      });
      onOverlayChange(updated);
      setPinMsg('Saved pin overlay.');
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setPinMsg(`Save failed: ${msg}`);
    } finally {
      setSavingPin(false);
    }
  };

  const savePartOverlay = async () => {
    if (!partName) {
      setPartMsg('Select a pin on a part to edit part overlay.');
      return;
    }
    setSavingPart(true);
    setPartMsg(null);
    try {
      const base = cloneOverlay(
        overlay ?? { annotations: [], partInfos: {}, netInfos: {} },
      );
      const partEntry: PartInfo = { ...(base.partInfos[partName] ?? {}) };
      const typeTrim = partType.trim();
      if (typeTrim) partEntry.part_type = typeTrim;
      else delete partEntry.part_type;
      if (partAngle === '') {
        delete partEntry.angle;
      } else {
        const ang = Number(partAngle);
        if (ang === 0 || ang === 90 || ang === 180 || ang === 270) {
          partEntry.angle = ang;
        } else {
          setPartMsg('Angle must be 0, 90, 180, or 270.');
          setSavingPart(false);
          return;
        }
      }
      const pins = partEntry.pins ?? {};
      const hasPins = Object.keys(pins).length > 0;
      if (!hasPins && !partEntry.part_type && partEntry.angle == null) {
        delete base.partInfos[partName];
      } else {
        base.partInfos[partName] = partEntry;
      }
      const updated = await putOverlays(boardId, {
        partInfos: base.partInfos,
        netInfos: base.netInfos,
      });
      onOverlayChange(updated);
      setPartMsg('Saved part overlay.');
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setPartMsg(`Save failed: ${msg}`);
    } finally {
      setSavingPart(false);
    }
  };

  const saveNetOverlay = async () => {
    if (!net) {
      setNetMsg('Select a pin on a net to edit net overlay.');
      return;
    }
    setSavingNet(true);
    setNetMsg(null);
    try {
      const base = cloneOverlay(
        overlay ?? { annotations: [], partInfos: {}, netInfos: {} },
      );
      const next: { showname?: string; note?: string } = {
        ...(base.netInfos[net.name] ?? {}),
      };
      const sn = netShowName.trim();
      const nn = netNote.trim();
      if (sn) next.showname = sn;
      else delete next.showname;
      if (nn) next.note = nn;
      else delete next.note;
      if (!next.showname && !next.note) {
        delete base.netInfos[net.name];
      } else {
        base.netInfos[net.name] = next;
      }
      const updated = await putOverlays(boardId, {
        partInfos: base.partInfos,
        netInfos: base.netInfos,
      });
      onOverlayChange(updated);
      setNetMsg('Saved net overlay.');
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setNetMsg(`Save failed: ${msg}`);
    } finally {
      setSavingNet(false);
    }
  };

  const saveAnnotationNote = async (ann: OverlayAnnotation) => {
    const draft = annDrafts[ann.id] ?? ann.note;
    setAnnBusy(ann.id);
    setAnnMsg(null);
    try {
      const updated = await patchAnnotation(boardId, ann.id, draft);
      const base = cloneOverlay(
        overlay ?? { annotations: [], partInfos: {}, netInfos: {} },
      );
      base.annotations = base.annotations.map((a) => (a.id === updated.id ? updated : a));
      onOverlayChange(base);
      setAnnMsg(`Annotation #${ann.id} updated.`);
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setAnnMsg(`Patch failed: ${msg}`);
    } finally {
      setAnnBusy(null);
    }
  };

  const removeAnnotation = async (ann: OverlayAnnotation) => {
    setAnnBusy(ann.id);
    setAnnMsg(null);
    try {
      await deleteAnnotation(boardId, ann.id);
      const base = cloneOverlay(
        overlay ?? { annotations: [], partInfos: {}, netInfos: {} },
      );
      base.annotations = base.annotations.filter((a) => a.id !== ann.id);
      onOverlayChange(base);
      setAnnMsg(`Annotation #${ann.id} deleted.`);
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setAnnMsg(`Delete failed: ${msg}`);
    } finally {
      setAnnBusy(null);
    }
  };

  const annotations = overlay?.annotations ?? [];

  return (
    <div className="info-pane">
      <div className="info-pane-header row">
        <h3>Selection / overlays</h3>
        {onReloadOverlays && (
          <button type="button" onClick={() => onReloadOverlays()} disabled={overlayLoading}>
            Reload overlays
          </button>
        )}
      </div>
      {overlayLoading && <p className="muted">Loading overlays…</p>}
      {overlayError && <p className="err">{overlayError}</p>}

      {!selectedPin ? (
        <p className="muted">
          Click a pin to inspect board + overlay fields. Right-click the canvas to add an
          annotation at board coordinates.
        </p>
      ) : (
        <div className="info-sections">
          <section className="info-section">
            <h4>Pin (board)</h4>
            <dl className="info-dl">
              <dt>id</dt>
              <dd className="mono">{selectedPin.id}</dd>
              <dt>part</dt>
              <dd>{selectedPin.component ?? '—'}</dd>
              <dt>number</dt>
              <dd>{selectedPin.number || '—'}</dd>
              <dt>name</dt>
              <dd>{selectedPin.name || '—'}</dd>
              <dt>net</dt>
              <dd>
                {net
                  ? `${net.name} (#${net.id}${net.isGround ? ', GND' : ''})`
                  : selectedPin.netId != null
                    ? `id ${selectedPin.netId}`
                    : '—'}
              </dd>
              <dt>pos</dt>
              <dd className="mono">
                ({selectedPin.pos.x.toFixed(1)}, {selectedPin.pos.y.toFixed(1)}) · {selectedPin.side}
              </dd>
            </dl>
          </section>

          {part && (
            <section className="info-section">
              <h4>Part (board)</h4>
              <dl className="info-dl">
                <dt>name</dt>
                <dd>{part.name}</dd>
                <dt>type</dt>
                <dd>{part.type || '—'}</dd>
                <dt>mount</dt>
                <dd>{part.mount || '—'}</dd>
                <dt>side</dt>
                <dd>{part.side}</dd>
                <dt>mfg</dt>
                <dd>{part.mfgcode || '—'}</dd>
              </dl>
              <h4 className="info-subhead">Part overlay</h4>
              <label className="info-field">
                <span>part_type</span>
                <input
                  type="text"
                  value={partType}
                  onChange={(e) => setPartType(e.target.value)}
                  disabled={savingPart}
                />
              </label>
              <label className="info-field">
                <span>angle</span>
                <select
                  value={partAngle}
                  onChange={(e) => setPartAngle(e.target.value)}
                  disabled={savingPart}
                >
                  <option value="">(none)</option>
                  <option value="0">0°</option>
                  <option value="90">90°</option>
                  <option value="180">180°</option>
                  <option value="270">270°</option>
                </select>
              </label>
              <div className="row">
                <button
                  type="button"
                  onClick={() => void savePartOverlay()}
                  disabled={savingPart || !partName}
                >
                  {savingPart ? 'Saving…' : 'Save part overlay'}
                </button>
              </div>
              {partMsg && (
                <p className={partMsg.startsWith('Save failed') || partMsg.startsWith('Angle') ? 'err' : 'msg'}>
                  {partMsg}
                </p>
              )}
            </section>
          )}

          {net && (
            <section className="info-section">
              <h4>Net (board + overlay)</h4>
              <dl className="info-dl">
                <dt>name</dt>
                <dd>{net.name}</dd>
                <dt>id</dt>
                <dd>{net.id}</dd>
                <dt>ground</dt>
                <dd>{net.isGround ? 'yes' : 'no'}</dd>
              </dl>
              <h4 className="info-subhead">Net overlay</h4>
              <label className="info-field">
                <span>showname</span>
                <input
                  type="text"
                  value={netShowName}
                  onChange={(e) => setNetShowName(e.target.value)}
                  disabled={savingNet}
                />
              </label>
              <label className="info-field">
                <span>note</span>
                <textarea
                  rows={2}
                  value={netNote}
                  onChange={(e) => setNetNote(e.target.value)}
                  disabled={savingNet}
                />
              </label>
              <div className="row">
                <button type="button" onClick={() => void saveNetOverlay()} disabled={savingNet}>
                  {savingNet ? 'Saving…' : 'Save net overlay'}
                </button>
              </div>
              {netMsg && (
                <p className={netMsg.startsWith('Save failed') ? 'err' : 'msg'}>{netMsg}</p>
              )}
            </section>
          )}

          <section className="info-section">
            <h4>Pin overlay</h4>
            {!partName ? (
              <p className="muted">Pin has no part; cannot store PartInfos entry.</p>
            ) : (
              <>
                <p className="muted mono">
                  key: {partName} / {pinKey}
                </p>
                <label className="info-field">
                  <span>show_name</span>
                  <input
                    type="text"
                    value={showName}
                    onChange={(e) => setShowName(e.target.value)}
                    disabled={savingPin}
                  />
                </label>
                <label className="info-field">
                  <span>note</span>
                  <textarea
                    rows={2}
                    value={note}
                    onChange={(e) => setNote(e.target.value)}
                    disabled={savingPin}
                  />
                </label>
                <label className="info-field">
                  <span>voltage</span>
                  <input
                    type="text"
                    value={voltage}
                    onChange={(e) => setVoltage(e.target.value)}
                    disabled={savingPin}
                  />
                </label>
                <label className="info-field">
                  <span>diode</span>
                  <input
                    type="text"
                    value={diode}
                    onChange={(e) => setDiode(e.target.value)}
                    disabled={savingPin}
                  />
                </label>
                <div className="row">
                  <button type="button" onClick={() => void savePinOverlay()} disabled={savingPin}>
                    {savingPin ? 'Saving…' : 'Save pin overlay'}
                  </button>
                </div>
                {pinMsg && (
                  <p className={pinMsg.startsWith('Save failed') ? 'err' : 'msg'}>{pinMsg}</p>
                )}
              </>
            )}
          </section>
        </div>
      )}

      <section className="info-section annotations-section">
        <h4>Annotations ({annotations.length})</h4>
        {annotations.length === 0 ? (
          <p className="muted">None yet — right-click canvas to create.</p>
        ) : (
          <ul className="ann-list">
            {annotations.map((a) => (
              <li key={a.id} className="ann-item">
                <div className="ann-meta mono muted">
                  #{a.id} · side {a.side} · ({a.x.toFixed(1)}, {a.y.toFixed(1)})
                  {a.part ? ` · ${a.part}` : ''}
                  {a.pin ? `.${a.pin}` : ''}
                  {a.net ? ` · ${a.net}` : ''}
                </div>
                <textarea
                  rows={2}
                  value={annDrafts[a.id] ?? a.note}
                  onChange={(e) =>
                    setAnnDrafts((prev) => ({ ...prev, [a.id]: e.target.value }))
                  }
                  disabled={annBusy === a.id}
                />
                <div className="row">
                  <button
                    type="button"
                    onClick={() => void saveAnnotationNote(a)}
                    disabled={annBusy === a.id}
                  >
                    Save note
                  </button>
                  <button
                    type="button"
                    onClick={() => void removeAnnotation(a)}
                    disabled={annBusy === a.id}
                  >
                    Delete
                  </button>
                </div>
              </li>
            ))}
          </ul>
        )}
        {annMsg && (
          <p className={annMsg.includes('failed') ? 'err' : 'msg'}>{annMsg}</p>
        )}
      </section>
    </div>
  );
}
