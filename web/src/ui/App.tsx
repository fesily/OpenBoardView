import { useCallback, useEffect, useRef, useState, type ChangeEvent } from 'react';
import {
  ApiError,
  getBoard,
  getHealth,
  getOverlays,
  getVersion,
  listBoards,
  postAnnotation,
  uploadBoard,
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
  const [boards, setBoards] = useState<BoardSummary[]>([]);
  const [listError, setListError] = useState<string | null>(null);
  const [uploading, setUploading] = useState(false);
  const [uploadMsg, setUploadMsg] = useState<string | null>(null);
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
        await loadOverlays(id, gen);
      } catch (e) {
        if (openBoardGen.current !== gen) return;
        setBoard(null);
        setOverlay(null);
        const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
        setBoardError(msg);
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

  const onFile = async (ev: ChangeEvent<HTMLInputElement>) => {
    const file = ev.target.files?.[0];
    ev.target.value = '';
    if (!file) return;
    setUploading(true);
    setUploadMsg(null);
    try {
      const result = await uploadBoard(file);
      setUploadMsg(
        result.ok
          ? `Uploaded ${file.name} → ${result.id}`
          : `Stored ${file.name} → ${result.id} (parse: ${result.error || 'failed'})`,
      );
      await refreshBoards();
      if (result.ok) {
        await openBoard(result.id);
      }
    } catch (e) {
      const msg = e instanceof ApiError ? `${e.code}: ${e.message}` : String(e);
      setUploadMsg(`Upload failed: ${msg}`);
    } finally {
      setUploading(false);
    }
  };

  return (
    <div className="app">
      <header>
        <h1>OpenBoardView Web</h1>
        <p className="sub">Canvas board view — pan / zoom / search / overlays</p>
      </header>

      <section className="card">
        <div className="row">
          <h2>Server</h2>
          <button type="button" onClick={() => void refreshHealth()}>
            Refresh
          </button>
        </div>
        {health.kind === 'loading' && <p className="muted">Checking /api/v1/health…</p>}
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
            <span className="muted">Start obv_server on 127.0.0.1:8080 (Vite proxies /api).</span>
          </p>
        )}
      </section>

      <section className="card">
        <h2>Upload board</h2>
        <input
          type="file"
          disabled={uploading}
          onChange={(e) => void onFile(e)}
          accept=".brd,.brd2,.bdv,.bvr,.bvr3,.fz,.cae,.asc,.bom,.cad,.cst,.json,.zip,.xzz,.alg,.brd.gz,.pcb,.txt"
        />
        {uploading && <p className="muted">Uploading…</p>}
        {uploadMsg && <p className="msg">{uploadMsg}</p>}
      </section>

      <section className="card">
        <div className="row">
          <h2>Boards</h2>
          <button type="button" onClick={() => void refreshBoards()}>
            Refresh list
          </button>
        </div>
        {listError && <p className="err">{listError}</p>}
        {!listError && boards.length === 0 && <p className="muted">No boards yet.</p>}
        {boards.length > 0 && (
          <table>
            <thead>
              <tr>
                <th>Name</th>
                <th>Parse</th>
                <th>Id</th>
              </tr>
            </thead>
            <tbody>
              {boards.map((b) => (
                <tr
                  key={b.id}
                  className={selectedId === b.id ? 'row-selected' : undefined}
                  onClick={() => {
                    if (b.ok) void openBoard(b.id);
                  }}
                  style={{ cursor: b.ok ? 'pointer' : 'default' }}
                >
                  <td>{b.name}</td>
                  <td className={b.ok ? 'ok' : 'err'}>{b.ok ? 'ok' : b.error || 'error'}</td>
                  <td className="mono" title={b.id}>
                    {b.id.slice(0, 12)}…
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
        )}
      </section>

      <section className="card board-view-card">
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
        {!boardLoading && !board && !boardError && (
          <p className="muted">Select an uploaded board or upload a new one.</p>
        )}
        {board && !boardLoading && (
          <>
            <SearchBox key={board.boardId} board={board} onSelectionChange={onSearchSelection} />
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
    </div>
  );
}
