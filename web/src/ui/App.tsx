import { useCallback, useEffect, useRef, useState } from 'react';
import {
  ApiError,
  getBoard,
  getHealth,
  getOverlays,
  getServerConfig,
  getVersion,
  listBoards,
  postAnnotation,
} from '../api/client';
import BoardCanvas from '../scene/BoardCanvas';
import type {
  BoardDocument,
  BoardSummary,
  OverlayDocument,
  Pin,
} from '../types/board';
import InfoPane from './InfoPane';
import SearchBox, { type SearchSelection } from './SearchBox';

type HealthState =
  | { kind: 'loading' }
  | { kind: 'ok'; status: string; version?: string }
  | { kind: 'error'; message: string };

export default function App() {
  const [health, setHealth] = useState<HealthState>({ kind: 'loading' });
  const [boardRoot, setBoardRoot] = useState<string | null>(null);
  const [boards, setBoards] = useState<BoardSummary[]>([]);
  const [listError, setListError] = useState<string | null>(null);
  const [selectedId, setSelectedId] = useState<string | null>(null);
  const [board, setBoard] = useState<BoardDocument | null>(null);
  const [boardError, setBoardError] = useState<string | null>(null);
  const [boardLoading, setBoardLoading] = useState(false);
  const [selectedPin, setSelectedPin] = useState<Pin | null>(null);
  const [searchSel, setSearchSel] = useState<SearchSelection | null>(null);
  const [overlay, setOverlay] = useState<OverlayDocument | null>(null);
  const [overlayLoading, setOverlayLoading] = useState(false);
  const [overlayError, setOverlayError] = useState<string | null>(null);
  const openBoardGen = useRef(0);

  const onSearchSelection = useCallback((sel: SearchSelection) => {
    setSearchSel(sel);
  }, []);

  const refreshBoards = useCallback(async () => {
    try {
      const list = await listBoards();
      setBoards(list);
      setListError(null);
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setListError(msg);
    }
  }, []);

  const refreshHealth = useCallback(async () => {
    setHealth({ kind: 'loading' });
    try {
      const h = await getHealth();
      let version: string | undefined;
      try {
        const v = await getVersion();
        version = `${v.server} ${v.serverVersion}`;
      } catch {
        // version optional
      }
      setHealth({ kind: 'ok', status: h.status, version });
      try {
        const cfg = await getServerConfig();
        setBoardRoot(cfg.boardRoot || null);
      } catch {
        // config optional for health
      }
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setHealth({ kind: 'error', message: msg });
    }
  }, []);

  useEffect(() => {
    void refreshHealth();
    void refreshBoards();
  }, [refreshHealth, refreshBoards]);

  const loadOverlays = useCallback(async (id: string, gen?: number) => {
    setOverlayLoading(true);
    setOverlayError(null);
    try {
      const doc = await getOverlays(id);
      if (gen != null && openBoardGen.current !== gen) return;
      setOverlay(doc);
    } catch (e) {
      if (gen != null && openBoardGen.current !== gen) return;
      setOverlay(null);
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setOverlayError(msg);
    } finally {
      if (gen == null || openBoardGen.current === gen) {
        setOverlayLoading(false);
      }
    }
  }, []);

  const openBoard = useCallback(
    async (id: string) => {
      const gen = ++openBoardGen.current;
      setSelectedId(id);
      setSelectedPin(null);
      setSearchSel(null);
      setOverlay(null);
      setOverlayError(null);
      // Drop previous board immediately so canvas coords never target a mismatched id.
      setBoard(null);
      setBoardLoading(true);
      setBoardError(null);
      try {
        const doc = await getBoard(id);
        // Drop stale responses if the user selected another board meanwhile.
        if (openBoardGen.current !== gen) return;
        setBoard(doc);
        // Mark list entry parse-ok after successful open.
        setBoards((prev) =>
          prev.map((b) => (b.id === id ? { ...b, ok: true, error: '' } : b)),
        );
        await loadOverlays(id, gen);
      } catch (e) {
        if (openBoardGen.current !== gen) return;
        setBoard(null);
        setOverlay(null);
        const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
        setBoardError(msg);
        setBoards((prev) =>
          prev.map((b) => (b.id === id ? { ...b, ok: false, error: msg } : b)),
        );
      } finally {
        if (openBoardGen.current === gen) {
          setBoardLoading(false);
        }
      }
    },
    [loadOverlays],
  );

  const onContextAnnotate = useCallback(
    async (pos: { x: number; y: number; side: number; pin: Pin | null }) => {
      // Always write against the currently rendered board, not a pending selection.
      if (!board || boardLoading) return;
      const targetId = board.boardId;
      const defaultNote = pos.pin
        ? `note @ ${pos.pin.id}`
        : `note @ (${pos.x.toFixed(1)}, ${pos.y.toFixed(1)})`;
      const note = window.prompt('Annotation note', defaultNote);
      if (note == null) return; // cancelled
      const netName =
        pos.pin?.netId != null
          ? (board.nets.find((n) => n.id === pos.pin!.netId)?.name ?? '')
          : '';
      try {
        const created = await postAnnotation(targetId, {
          side: pos.side,
          x: pos.x,
          y: pos.y,
          note,
          part: pos.pin?.component ?? '',
          pin: pos.pin ? pos.pin.name || pos.pin.number || '' : '',
          net: netName,
        });
        setOverlay((prev) => {
          const base = prev ?? { annotations: [], partInfos: {}, netInfos: {} };
          return {
            ...base,
            annotations: [...base.annotations.filter((a) => a.id !== created.id), created],
          };
        });
      } catch (e) {
        const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
        window.alert(`Failed to create annotation: ${msg}`);
      }
    },
    [board, boardLoading],
  );

  return (
    <div className="app-shell">
      <aside className="sidebar">
        <div className="sidebar-header">
          <h1>OpenBoardView</h1>
          <p className="sub">Library mode</p>
        </div>

        <div className="sidebar-status">
          {health.kind === 'loading' && <p className="muted">Checking server…</p>}
          {health.kind === 'ok' && (
            <p className="ok">
              Health: <strong>{health.status}</strong>
              {health.version ? ` · ${health.version}` : ''}
            </p>
          )}
          {health.kind === 'error' && (
            <p className="err">
              Unreachable: {health.message}
              <br />
              <span className="muted">Start obv_server (Vite proxies /api).</span>
            </p>
          )}
          {boardRoot && (
            <p className="board-root mono" title={boardRoot}>
              boardRoot: {boardRoot}
            </p>
          )}
          <div className="row sidebar-actions">
            <button type="button" onClick={() => void refreshHealth()}>
              Health
            </button>
            <button type="button" onClick={() => void refreshBoards()}>
              Refresh list
            </button>
          </div>
        </div>

        <div className="file-list-wrap">
          {listError && <p className="err">{listError}</p>}
          {!listError && boards.length === 0 && (
            <p className="muted">No board files under boardRoot.</p>
          )}
          <ul className="file-list">
            {boards.map((b) => {
              const label = b.path || b.name;
              const failed = !b.ok && !!b.error;
              return (
                <li key={b.id}>
                  <button
                    type="button"
                    className={
                      'file-item' +
                      (selectedId === b.id ? ' file-item-selected' : '') +
                      (failed ? ' file-item-error' : '')
                    }
                    title={b.error || label}
                    onClick={() => void openBoard(b.id)}
                  >
                    <span className="file-item-name">{label}</span>
                    {failed && <span className="file-item-badge">error</span>}
                  </button>
                </li>
              );
            })}
          </ul>
        </div>
      </aside>

      <main className="main">
        {!selectedId && !boardLoading && (
          <div className="main-placeholder">
            <p>从左侧选择板图</p>
          </div>
        )}
        {selectedId && (
          <section className="board-view-card card">
            <div className="row">
              <h2>Board view</h2>
              {board && (
                <span className="muted mono" title={board.boardId}>
                  {board.sourceName}
                </span>
              )}
            </div>
            {boardLoading && <p className="muted">Loading board…</p>}
            {boardError && <p className="err">{boardError}</p>}
            {board && !boardLoading && (
              <>
                <SearchBox
                  key={board.boardId}
                  board={board}
                  onSelectionChange={onSearchSelection}
                />
                <BoardCanvas
                  board={board}
                  selectedPinId={selectedPin?.id ?? null}
                  onSelectPin={setSelectedPin}
                  highlightPartNames={searchSel?.highlight.partNames}
                  highlightPinIds={searchSel?.highlight.pinIds}
                  focusPoint={searchSel?.focus ?? null}
                  focusToken={searchSel?.focusToken ?? 0}
                  annotations={overlay?.annotations ?? []}
                  onContextAnnotate={(pos) => void onContextAnnotate(pos)}
                />
                <InfoPane
                  boardId={board.boardId}
                  board={board}
                  selectedPin={selectedPin}
                  overlay={overlay}
                  overlayLoading={overlayLoading}
                  overlayError={overlayError}
                  onOverlayChange={setOverlay}
                  onReloadOverlays={() => {
                    void loadOverlays(board.boardId);
                  }}
                />
              </>
            )}
          </section>
        )}
      </main>
    </div>
  );
}
