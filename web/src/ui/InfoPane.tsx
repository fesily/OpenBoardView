import { useCallback, useEffect, useMemo, useState } from 'react';
import type { ReactNode } from 'react';
import {
  ApiError,
  deleteAnnotation,
  getPartOperatingConditions,
  patchAnnotation,
  promotePartOperatingConditions,
  putOverlays,
} from '../api/client';
import {
  editTargetPin,
  localPinValue,
  type PinValueMode,
} from '../scene/pinValues';
import type {
  BoardDocument,
  ConditionSource,
  Net,
  OperatingCondition,
  OverlayAnnotation,
  OverlayDocument,
  PartConditionsView,
  PartInfo,
  Pin,
  PinInfo,
  ResolvedPinLabel,
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

/** Canvas/desktop display label: show_name (overlay or board) over name/number. */
function pinDisplayLabel(pin: Pin, overlayShow?: string): string {
  if (overlayShow && overlayShow.trim()) return overlayShow.trim();
  if (pin.show_name && pin.show_name.trim()) return pin.show_name.trim();
  if (pin.name && pin.name.trim()) return pin.name.trim();
  return pin.number || pin.id;
}

/** Net display: overlay showname > board showName > board name. */
function netDisplayName(
  net: Pick<Net, 'name' | 'showName'>,
  overlayShowname?: string | null,
): string {
  const ov = (overlayShowname ?? '').trim();
  if (ov) return ov;
  const boardShow = (net.showName ?? '').trim();
  if (boardShow) return boardShow;
  return net.name;
}


/** Desktop PartAngle enum integers accepted by ApplyOverlayJson. */
const PART_ANGLE_OPTIONS: ReadonlyArray<{ value: string; label: string }> = [
  { value: '0', label: '0°' }, // PartAngle::_0
  { value: '3', label: '90°' }, // PartAngle::_90
  { value: '2', label: '180°' }, // PartAngle::_180
  { value: '1', label: '270°' }, // PartAngle::_270
  { value: '4', label: 'sorted' }, // PartAngle::sorted
];

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

const SOURCE_BADGE: Record<ConditionSource, { label: string; className: string }> = {
  chip: { label: '共享(chip)', className: 'oc-badge oc-badge-chip' },
  board: { label: '本板(board)', className: 'oc-badge oc-badge-board' },
  none: { label: '无', className: 'oc-badge oc-badge-none' },
};

function formatResolvedLabel(
  label: string,
  resolved?: Pick<ResolvedPinLabel, 'matched' | 'id' | 'name'>,
): string {
  if (
    resolved &&
    resolved.matched &&
    resolved.matched !== 'none' &&
    resolved.id &&
    resolved.name
  ) {
    return `${resolved.id} (${resolved.name})`;
  }
  return label;
}

function formatPinList(labels?: string[], resolved?: ResolvedPinLabel[]): string {
  if (!labels || labels.length === 0) return '—';
  return labels
    .map((label, i) => formatResolvedLabel(label, resolved?.[i]))
    .join(', ');
}

function conditionTitle(oc: OperatingCondition, index: number): string {
  const name = oc.name?.trim();
  if (name) return name;
  const id = oc.id?.trim();
  if (id) return id;
  return `Group ${index + 1}`;
}

type PanelId =
  | 'pin'
  | 'part'
  | 'conditions'
  | 'net'
  | 'pinOverlay'
  | 'annotations';

function CollapsibleSection({
  id,
  title,
  summary,
  open,
  onToggle,
  children,
  className = '',
  badge,
}: {
  id: PanelId;
  title: string;
  summary?: string;
  open: boolean;
  onToggle: (id: PanelId) => void;
  children: ReactNode;
  className?: string;
  badge?: ReactNode;
}) {
  return (
    <section className={`info-section collapsible-section ${className}`.trim()}>
      <button
        type="button"
        className="collapsible-head"
        aria-expanded={open}
        aria-controls={`panel-${id}`}
        onClick={() => onToggle(id)}
      >
        <span className="collapsible-chevron" aria-hidden>
          {open ? '▾' : '▸'}
        </span>
        <span className="collapsible-title">{title}</span>
        {badge}
        {summary && !open ? (
          <span className="collapsible-summary muted mono">{summary}</span>
        ) : null}
      </button>
      {open ? (
        <div className="collapsible-body" id={`panel-${id}`}>
          {children}
        </div>
      ) : null}
    </section>
  );
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

  /**
   * net_source (default): measurement fields edit the pin that seeds net propagation.
   * local: always write overlay on the selected pin.
   * show_name / note always stay on the selected pin.
   */
  const [pinEditMode, setPinEditMode] = useState<'net_source' | 'local'>('net_source');

  const [note, setNote] = useState(pinInfo.note ?? '');
  const [showName, setShowName] = useState(pinInfo.show_name ?? '');
  const [voltage, setVoltage] = useState(pinInfo.voltage ?? '');
  const [diode, setDiode] = useState(pinInfo.diode ?? '');
  const [ohm, setOhm] = useState(pinInfo.ohm ?? '');
  const [ohmBlack, setOhmBlack] = useState(pinInfo.ohm_black ?? '');
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
  const [conditions, setConditions] = useState<PartConditionsView | null>(null);
  const [conditionsLoading, setConditionsLoading] = useState(false);
  const [conditionsError, setConditionsError] = useState<string | null>(null);
  const [conditionsMsg, setConditionsMsg] = useState<string | null>(null);
  const [clearBoardOnPromote, setClearBoardOnPromote] = useState(false);
  const [promoting, setPromoting] = useState(false);
  const [conditionsTick, setConditionsTick] = useState(0);
  /** Two-level accordion: only open panels expand body. */
  const [openPanels, setOpenPanels] = useState<Set<PanelId>>(
    () => new Set<PanelId>(),
  );
  const togglePanel = useCallback((id: PanelId) => {
    setOpenPanels((prev) => {
      const next = new Set(prev);
      if (next.has(id)) next.delete(id);
      else next.add(id);
      return next;
    });
  }, []);



  const measureTargets = useMemo((): Record<PinValueMode, Pin> | null => {
    if (!selectedPin) return null;
    const modes: PinValueMode[] = ['diode', 'voltage', 'ohm', 'ohm_black'];
    const out: Record<PinValueMode, Pin> = {
      diode: selectedPin,
      voltage: selectedPin,
      ohm: selectedPin,
      ohm_black: selectedPin,
    };
    for (const m of modes) {
      out[m] = editTargetPin(selectedPin, board, overlay, m, pinEditMode);
    }
    return out;
  }, [selectedPin, board, overlay, pinEditMode]);

  // Sync local editors when selection, mode, or overlay reloads.
  useEffect(() => {
    if (!selectedPin || !measureTargets) {
      setNote('');
      setShowName('');
      setVoltage('');
      setDiode('');
      setOhm('');
      setOhmBlack('');
      setPinMsg(null);
      return;
    }
    // Identity fields always on selected pin.
    const localOv = partName
      ? (overlay?.partInfos[partName]?.pins?.[pinKey] ?? {})
      : ({} as PinInfo);
    setNote(localOv.note ?? '');
    setShowName(localOv.show_name ?? '');
    // Measurement fields load from edit target (net source or local).
    setDiode(localPinValue(measureTargets.diode, overlay, 'diode'));
    setVoltage(localPinValue(measureTargets.voltage, overlay, 'voltage'));
    setOhm(localPinValue(measureTargets.ohm, overlay, 'ohm'));
    setOhmBlack(localPinValue(measureTargets.ohm_black, overlay, 'ohm_black'));
    setPinMsg(null);
  }, [
    selectedPin,
    selectedPin?.id,
    partName,
    pinKey,
    pinEditMode,
    overlay,
    measureTargets,
  ]);

  // Keep part overlay editors in sync when selection or overlays reload.
  useEffect(() => {
    if (!partName) {
      setPartType('');
      setPartAngle('');
      setPartMsg(null);
      return;
    }
    const info = overlay?.partInfos[partName];
    setPartType(info?.part_type ?? '');
    setPartAngle(info?.angle != null ? String(info.angle) : '');
    setPartMsg(null);
  }, [partName, overlay]);

  // Keep net overlay editors in sync when selection or overlays reload.
  useEffect(() => {
    if (!net) {
      setNetShowName('');
      setNetNote('');
      setNetMsg(null);
      return;
    }
    const info = overlay?.netInfos[net.name];
    setNetShowName(info?.showname ?? '');
    setNetNote(info?.note ?? '');
    setNetMsg(null);
  }, [net?.name, net?.id, overlay]);


  const reloadConditions = useCallback(() => {
    setConditionsTick((n) => n + 1);
  }, []);

  // Auto-surface merged chip/board operating conditions for the selected part.
  useEffect(() => {
    if (!partName || !boardId) {
      setConditions(null);
      setConditionsError(null);
      setConditionsLoading(false);
      setConditionsMsg(null);
      return;
    }
    let cancelled = false;
    setConditionsLoading(true);
    setConditionsError(null);
    setConditionsMsg(null);
    void getPartOperatingConditions(boardId, partName)
      .then((view) => {
        if (!cancelled) {
          setConditions(view);
          setConditionsLoading(false);
        }
      })
      .catch((e: unknown) => {
        if (!cancelled) {
          const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
          setConditions(null);
          setConditionsError(msg);
          setConditionsLoading(false);
        }
      });
    return () => {
      cancelled = true;
    };
  }, [boardId, partName, conditionsTick]);

  const promoteConditions = async () => {
    if (!partName) return;
    setPromoting(true);
    setConditionsMsg(null);
    try {
      const view = await promotePartOperatingConditions(
        boardId,
        partName,
        clearBoardOnPromote,
      );
      setConditions(view);
      setConditionsError(null);
      setConditionsMsg(
        clearBoardOnPromote
          ? 'Promoted to chip library and cleared board-local conditions.'
          : 'Promoted to chip library (board-local kept).',
      );
      // Board overlay may have changed when clearBoard is set.
      onReloadOverlays?.();
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setConditionsMsg(`Promote failed: ${msg}`);
    } finally {
      setPromoting(false);
    }
  };


  const savePinOverlay = async () => {
    if (!selectedPin || !measureTargets) {
      setPinMsg('Select a pin to edit overlay fields.');
      return;
    }
    setSavingPin(true);
    setPinMsg(null);
    try {
      const base = cloneOverlay(
        overlay ?? { annotations: [], partInfos: {}, netInfos: {} },
      );

      const writePinFields = (
        target: Pin,
        fields: Partial<Record<keyof PinInfo, string>>,
      ) => {
        const tPart = target.component;
        if (!tPart) {
          throw new Error(`Pin ${target.id} has no part; cannot store PartInfos.`);
        }
        const tKey = pinOverlayKey(target);
        const partEntry: PartInfo = { ...(base.partInfos[tPart] ?? {}) };
        const pins = { ...(partEntry.pins ?? {}) };
        const nextPin: PinInfo = { ...(pins[tKey] ?? {}) };
        for (const [k, value] of Object.entries(fields)) {
          const t = (value ?? '').trim();
          if (t) (nextPin as Record<string, string>)[k] = t;
          else delete (nextPin as Record<string, string | undefined>)[k];
        }
        const hasAny = Object.values(nextPin).some((v) => v != null && String(v).length > 0);
        if (hasAny) pins[tKey] = nextPin;
        else delete pins[tKey];
        partEntry.pins = pins;
        if (
          Object.keys(pins).length === 0 &&
          !partEntry.part_type &&
          partEntry.angle == null
        ) {
          delete base.partInfos[tPart];
        } else {
          base.partInfos[tPart] = partEntry;
        }
      };

      // show_name / note always on selected pin.
      if (!selectedPin.component) {
        setPinMsg('Selected pin has no part; cannot store PartInfos entry.');
        return;
      }
      writePinFields(selectedPin, {
        note,
        show_name: showName,
      });

      // Merge measurement writes per target pin (may differ in net_source mode).
      type Meas = { pin: Pin; fields: Partial<Record<PinValueMode, string>> };
      const buckets = new Map<string, Meas>();
      const addMeas = (mode: PinValueMode, value: string) => {
        const t = measureTargets[mode];
        if (!t.component) {
          throw new Error(`Target pin ${t.id} has no part for ${mode}.`);
        }
        const id = t.id;
        const bucket = buckets.get(id) ?? { pin: t, fields: {} };
        bucket.fields[mode] = value;
        buckets.set(id, bucket);
      };
      addMeas('diode', diode);
      addMeas('voltage', voltage);
      addMeas('ohm', ohm);
      addMeas('ohm_black', ohmBlack);

      for (const { pin: t, fields } of buckets.values()) {
        writePinFields(t, fields);
      }

      const updated = await putOverlays(boardId, {
        partInfos: base.partInfos,
        netInfos: base.netInfos,
      });
      onOverlayChange(updated);
      const remote =
        pinEditMode === 'net_source' &&
        (measureTargets.diode.id !== selectedPin.id ||
          measureTargets.voltage.id !== selectedPin.id ||
          measureTargets.ohm.id !== selectedPin.id ||
          measureTargets.ohm_black.id !== selectedPin.id);
      setPinMsg(
        remote
          ? 'Saved pin overlay (measurements wrote to net source pin(s)).'
          : 'Saved pin overlay.',
      );
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
        if (ang === 0 || ang === 1 || ang === 2 || ang === 3 || ang === 4) {
          partEntry.angle = ang;
        } else {
          setPartMsg('Angle must be a PartAngle enum 0–4.');
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
      // part_type bind may unlock chip-library merge / promote.
      reloadConditions();
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

  const pinSummary = selectedPin
    ? `${selectedPin.component ?? '—'} · ${pinDisplayLabel(selectedPin, pinInfo.show_name) || selectedPin.id}`
    : '';
  const partSummary = part
    ? `${part.name}${partType.trim() ? ` · ${partType.trim()}` : part.type ? ` · ${part.type}` : ''}`
    : '';
  const netSummary = net
    ? netDisplayName(net, netInfo?.showname)
    : '';
  const ocCount = conditions
    ? (conditions.effective?.length
        ? conditions.effective.length
        : conditions.operating_conditions?.length ?? 0)
    : 0;
  const conditionsSummary = conditions
    ? `${SOURCE_BADGE[conditions.source]?.label ?? conditions.source} · ${ocCount}`
    : conditionsLoading
      ? 'loading…'
      : '';
  const pinOverlaySummary = selectedPin?.component
    ? `${partName}/${pinKey}${pinEditMode === 'net_source' ? ' · net src' : ' · local'}`
    : 'no part';
  const annSummary = `${annotations.length}`;

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
          {conditions != null &&
            (ocCount > 0 ||
              conditions.board.length > 0 ||
              conditions.chip.length > 0 ||
              (conditions.operating_conditions?.length ?? 0) > 0) && (
            <CollapsibleSection
              id="conditions"
              title="Operating conditions"
              summary={conditionsSummary}
              open={openPanels.has('conditions')}
              onToggle={togglePanel}
              className="oc-section"
              badge={
                conditions ? (
                  <span
                    className={SOURCE_BADGE[conditions.source]?.className ?? SOURCE_BADGE.none.className}
                    title={`source=${conditions.source}`}
                  >
                    {SOURCE_BADGE[conditions.source]?.label ?? SOURCE_BADGE.none.label}
                  </span>
                ) : null
              }
            >
              {conditionsLoading && <p className="muted">Loading conditions…</p>}
              {conditionsError && <p className="err">{conditionsError}</p>}
              {!conditionsLoading && conditionsError && (
                <div className="row oc-actions">
                  <button
                    type="button"
                    onClick={() => reloadConditions()}
                    disabled={conditionsLoading || promoting}
                  >
                    Refresh
                  </button>
                </div>
              )}
              {!conditionsLoading && !conditionsError && conditions && (
                <>
                  {(() => {
                    const effective =
                      conditions.effective?.length
                        ? conditions.effective
                        : conditions.operating_conditions ?? [];
                    if (effective.length === 0) {
                      return (
                        <p className="muted">
                          No effective conditions
                          {conditions.board.length || conditions.chip.length
                            ? ` (board ${conditions.board.length}, chip ${conditions.chip.length}).`
                            : '.'}
                        </p>
                      );
                    }
                    return (
                      <ul className="oc-list">
                        {effective.map((oc, i) => {
                          const resolved =
                            (oc.id && conditions.resolved?.[oc.id]) || undefined;
                          return (
                            <li key={oc.id || `oc-${i}`} className="oc-item">
                              <div className="oc-item-title">
                                {conditionTitle(oc, i)}
                                {oc.id ? (
                                  <span className="muted mono oc-id"> · {oc.id}</span>
                                ) : null}
                              </div>
                              <dl className="info-dl oc-dl">
                                <dt>inputs</dt>
                                <dd className="mono">
                                  {formatPinList(oc.inputs, resolved?.inputs)}
                                </dd>
                                <dt>outputs</dt>
                                <dd className="mono">
                                  {formatPinList(oc.outputs, resolved?.outputs)}
                                </dd>
                                <dt>enables</dt>
                                <dd className="mono">
                                  {formatPinList(oc.enables, resolved?.enables)}
                                </dd>
                                {oc.note ? (
                                  <>
                                    <dt>note</dt>
                                    <dd>{oc.note}</dd>
                                  </>
                                ) : null}
                              </dl>
                            </li>
                          );
                        })}
                      </ul>
                    );
                  })()}
                  <p className="muted oc-meta">
                    board {conditions.board.length} · chip {conditions.chip.length}
                    {conditions.part_type ? ` · type ${conditions.part_type}` : ''}
                  </p>
                  {!partType.trim() && (
                    <p className="muted">
                      Set <span className="mono">part_type</span> in Part and save to bind shared
                      chip-library conditions.
                    </p>
                  )}
                  <div className="row oc-actions">
                    <button
                      type="button"
                      onClick={() => reloadConditions()}
                      disabled={conditionsLoading || promoting}
                    >
                      Refresh
                    </button>
                    <label className="oc-clear-board">
                      <input
                        type="checkbox"
                        checked={clearBoardOnPromote}
                        onChange={(e) => setClearBoardOnPromote(e.target.checked)}
                        disabled={
                          promoting || !partType.trim() || conditions.board.length === 0
                        }
                      />
                      <span>clearBoard</span>
                    </label>
                    <button
                      type="button"
                      onClick={() => void promoteConditions()}
                      disabled={
                        promoting || !partType.trim() || conditions.board.length === 0
                      }
                      title={
                        conditions.board.length === 0
                          ? 'No board-local conditions to promote (source is chip/none)'
                          : !partType.trim()
                            ? 'Set part_type first'
                            : 'Copy board conditions into chip library for this part_type'
                      }
                    >
                      {promoting ? 'Promoting…' : 'Promote to chip'}
                    </button>
                  </div>
                  {conditions.board.length === 0 && (
                    <p className="muted">
                      Promote needs <strong>board-local</strong> conditions first (agent
                      POST without scope, or overlay). Shared chip conditions already
                      live in the library — no promote needed.
                    </p>
                  )}
                  {conditionsMsg && (
                    <p
                      className={
                        conditionsMsg.startsWith('Promote failed') ? 'err' : 'msg'
                      }
                    >
                      {conditionsMsg}
                    </p>
                  )}
                  <p className="muted oc-edit-hint">
                    Board-local edits still via overlay/agent; chip writes use scope=chip
                    on the conditions API.
                  </p>
                </>
              )}
            </CollapsibleSection>
          )}

          <CollapsibleSection
            id="pin"
            title="Pin"
            summary={pinSummary}
            open={openPanels.has('pin')}
            onToggle={togglePanel}
          >
            <dl className="info-dl">
              <dt>id</dt>
              <dd className="mono">{selectedPin.id}</dd>
              <dt>part</dt>
              <dd>{selectedPin.component ?? '—'}</dd>
              <dt>number</dt>
              <dd>{selectedPin.number || '—'}</dd>
              <dt>name</dt>
              <dd>{selectedPin.name || '—'}</dd>
              <dt>show_name</dt>
              <dd>
                {pinDisplayLabel(selectedPin, pinInfo.show_name) || '—'}
              </dd>
              <dt>net</dt>
              <dd>
                {net
                  ? `${netDisplayName(net, netInfo?.showname)}${
                      netDisplayName(net, netInfo?.showname) !== net.name
                        ? ` ← ${net.name}`
                        : ''
                    } (#${net.id}${net.isGround ? ', GND' : ''})`
                  : selectedPin.netId != null
                    ? `id ${selectedPin.netId}`
                    : '—'}
              </dd>
              <dt>pos</dt>
              <dd className="mono">
                ({selectedPin.pos.x.toFixed(1)}, {selectedPin.pos.y.toFixed(1)}) · {selectedPin.side}
              </dd>
            </dl>
          </CollapsibleSection>

          {part && (
            <CollapsibleSection
              id="part"
              title="Part"
              summary={partSummary}
              open={openPanels.has('part')}
              onToggle={togglePanel}
            >
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
              <h5 className="info-subhead">Part overlay</h5>
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
                  {PART_ANGLE_OPTIONS.map((opt) => (
                    <option key={opt.value} value={opt.value}>
                      {opt.label}
                    </option>
                  ))}
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
                <p
                  className={
                    partMsg.startsWith('Save failed') || partMsg.startsWith('Angle')
                      ? 'err'
                      : 'msg'
                  }
                >
                  {partMsg}
                </p>
              )}
            </CollapsibleSection>
          )}

          {net && (
            <CollapsibleSection
              id="net"
              title="Net"
              summary={netSummary}
              open={openPanels.has('net')}
              onToggle={togglePanel}
            >
              <dl className="info-dl">
                <dt>name</dt>
                <dd>{net.name}</dd>
                <dt>display</dt>
                <dd>{netDisplayName(net, netInfo?.showname)}</dd>
                <dt>id</dt>
                <dd>{net.id}</dd>
                <dt>ground</dt>
                <dd>{net.isGround ? 'yes' : 'no'}</dd>
              </dl>
              <h5 className="info-subhead">Net overlay</h5>
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
            </CollapsibleSection>
          )}

          <CollapsibleSection
            id="pinOverlay"
            title="Pin overlay"
            summary={pinOverlaySummary}
            open={openPanels.has('pinOverlay')}
            onToggle={togglePanel}
          >
            {!selectedPin.component ? (
              <p className="muted">Pin has no part; cannot store PartInfos entry.</p>
            ) : (
              <>
                <div
                  className="pin-edit-modes"
                  role="radiogroup"
                  aria-label="Pin overlay edit target"
                >
                  <label className="pin-edit-mode">
                    <input
                      type="radio"
                      name="pin-edit-mode"
                      checked={pinEditMode === 'net_source'}
                      onChange={() => setPinEditMode('net_source')}
                      disabled={savingPin}
                    />
                    <span>Net source</span>
                  </label>
                  <label className="pin-edit-mode">
                    <input
                      type="radio"
                      name="pin-edit-mode"
                      checked={pinEditMode === 'local'}
                      onChange={() => setPinEditMode('local')}
                      disabled={savingPin}
                    />
                    <span>Local pin</span>
                  </label>
                </div>
                <p className="muted pin-edit-hint">
                  {pinEditMode === 'net_source'
                    ? 'Default: diode/voltage/ohm write to the pin that seeds net propagation (or this pin if none). show_name/note always local.'
                    : 'Local: all fields write only on the selected pin.'}
                </p>
                <p className="muted mono">
                  selected: {partName} / {pinKey}
                </p>
                {pinEditMode === 'net_source' && measureTargets && (
                  <p className="muted mono pin-edit-targets">
                    meas targets: d=
                    {measureTargets.diode.id === selectedPin.id
                      ? 'self'
                      : `${measureTargets.diode.component}/${pinOverlayKey(measureTargets.diode)}`}
                    {' · '}v=
                    {measureTargets.voltage.id === selectedPin.id
                      ? 'self'
                      : `${measureTargets.voltage.component}/${pinOverlayKey(measureTargets.voltage)}`}
                    {' · '}Ω=
                    {measureTargets.ohm.id === selectedPin.id
                      ? 'self'
                      : `${measureTargets.ohm.component}/${pinOverlayKey(measureTargets.ohm)}`}
                    {' · '}Ωb=
                    {measureTargets.ohm_black.id === selectedPin.id
                      ? 'self'
                      : `${measureTargets.ohm_black.component}/${pinOverlayKey(measureTargets.ohm_black)}`}
                  </p>
                )}
                <label className="info-field">
                  <span>show_name (local)</span>
                  <input
                    type="text"
                    value={showName}
                    onChange={(e) => setShowName(e.target.value)}
                    disabled={savingPin}
                  />
                </label>
                <label className="info-field">
                  <span>note (local)</span>
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
                <label className="info-field">
                  <span>ohm</span>
                  <input
                    type="text"
                    value={ohm}
                    onChange={(e) => setOhm(e.target.value)}
                    disabled={savingPin}
                  />
                </label>
                <label className="info-field">
                  <span>ohm_black</span>
                  <input
                    type="text"
                    value={ohmBlack}
                    onChange={(e) => setOhmBlack(e.target.value)}
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
          </CollapsibleSection>
        </div>
      )}

      <CollapsibleSection
        id="annotations"
        title="Annotations"
        summary={annSummary}
        open={openPanels.has('annotations')}
        onToggle={togglePanel}
        className="annotations-section"
      >
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
      </CollapsibleSection>
    </div>
  );
}
