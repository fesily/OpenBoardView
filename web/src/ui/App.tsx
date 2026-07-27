import { useCallback, useEffect, useState, type ChangeEvent } from 'react';
import {
  ApiError,
  getHealth,
  getVersion,
  listBoards,
  uploadBoard,
} from '../api/client';
import type { BoardSummary } from '../types/board';

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
        <p className="sub">Task 8 scaffold — health, upload, board list (no canvas yet)</p>
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
                <tr key={b.id}>
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
    </div>
  );
}
