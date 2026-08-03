import { useCallback, useEffect, useMemo, useState } from 'react';
import {
  ApiError,
  getPartOperatingConditions,
  promotePartOperatingConditions,
} from '../api/client';
import type {
  ConditionSource,
  OperatingCondition,
  OverlayDocument,
  PartConditionsView,
  Pin,
  ResolvedPinLabel,
} from '../types/board';

export interface OperatingConditionsPaneProps {
  boardId: string;
  selectedPin: Pin | null;
  /** Used to re-fetch after part_type / overlay changes. */
  overlay: OverlayDocument | null;
  onReloadOverlays?: () => void;
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
  const name = (oc.name || '').trim();
  if (name) return name;
  return `Condition ${index + 1}`;
}

/**
 * Top-level right-panel block for chip/board operating conditions.
 * Independent from Selection / overlays — only renders when the selected
 * part has board/chip/effective conditions.
 */
export default function OperatingConditionsPane({
  boardId,
  selectedPin,
  overlay,
  onReloadOverlays,
}: OperatingConditionsPaneProps) {
  const partName = selectedPin?.component ?? '';
  const partType =
    (partName && overlay?.partInfos[partName]?.part_type?.trim()) || '';

  const [conditions, setConditions] = useState<PartConditionsView | null>(null);
  const [conditionsLoading, setConditionsLoading] = useState(false);
  const [conditionsError, setConditionsError] = useState<string | null>(null);
  const [conditionsMsg, setConditionsMsg] = useState<string | null>(null);
  const [clearBoardOnPromote, setClearBoardOnPromote] = useState(false);
  const [promoting, setPromoting] = useState(false);
  const [conditionsTick, setConditionsTick] = useState(0);
  const [open, setOpen] = useState(false);

  const reloadConditions = useCallback(() => {
    setConditionsTick((n) => n + 1);
  }, []);

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
  }, [boardId, partName, conditionsTick, overlay]);

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
      onReloadOverlays?.();
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setConditionsMsg(`Promote failed: ${msg}`);
    } finally {
      setPromoting(false);
    }
  };

  const ocCount = useMemo(() => {
    if (!conditions) return 0;
    if (conditions.effective?.length) return conditions.effective.length;
    return conditions.operating_conditions?.length ?? 0;
  }, [conditions]);

  const hasData =
    conditions != null &&
    (ocCount > 0 ||
      conditions.board.length > 0 ||
      conditions.chip.length > 0 ||
      (conditions.operating_conditions?.length ?? 0) > 0);

  // Hide entirely when no part selected or no conditions for this part.
  if (!partName || !hasData) return null;

  const summary = `${SOURCE_BADGE[conditions.source]?.label ?? conditions.source} · ${ocCount}${
    partType ? ` · ${partType}` : ''
  }`;

  return (
    <div className="right-section oc-pane">
      <button
        type="button"
        className="collapsible-head oc-pane-head"
        aria-expanded={open}
        aria-controls="panel-operating-conditions"
        onClick={() => setOpen((v) => !v)}
      >
        <span className="collapsible-chevron" aria-hidden>
          {open ? '▾' : '▸'}
        </span>
        <span className="collapsible-title">Operating conditions</span>
        <span
          className={SOURCE_BADGE[conditions.source]?.className ?? SOURCE_BADGE.none.className}
          title={`source=${conditions.source}`}
        >
          {SOURCE_BADGE[conditions.source]?.label ?? SOURCE_BADGE.none.label}
        </span>
        {!open ? <span className="collapsible-summary muted mono">{summary}</span> : null}
      </button>

      {open ? (
        <div className="oc-pane-body" id="panel-operating-conditions">
          <p className="muted mono oc-pane-part">
            {partName}
            {partType ? ` · type ${partType}` : ''}
          </p>
          {conditionsLoading && <p className="muted">Loading conditions…</p>}
          {conditionsError && <p className="err">{conditionsError}</p>}
          {!conditionsLoading && !conditionsError && (
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
              {!partType && (
                <p className="muted">
                  Set <span className="mono">part_type</span> in Selection / overlays → Part
                  and save to bind shared chip-library conditions.
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
                      promoting || !partType || conditions.board.length === 0
                    }
                  />
                  <span>clearBoard</span>
                </label>
                <button
                  type="button"
                  onClick={() => void promoteConditions()}
                  disabled={promoting || !partType || conditions.board.length === 0}
                  title={
                    conditions.board.length === 0
                      ? 'No board-local conditions to promote (source is chip/none)'
                      : !partType
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
        </div>
      ) : null}
    </div>
  );
}
