import type {
  ApiErrorBody,
  BoardDocument,
  BoardMeta,
  BoardSummary,
  HealthResponse,
  NewAnnotation,
  OverlayAnnotation,
  OverlayDocument,
  UploadBoardResult,
  VersionResponse,
} from '../types/board';

const API = '/api/v1';

export class ApiError extends Error {
  readonly status: number;
  readonly code: string;

  constructor(status: number, code: string, message: string) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.code = code;
  }
}

async function parseJson<T>(res: Response): Promise<T> {
  const text = await res.text();
  if (!text) {
    if (!res.ok) {
      throw new ApiError(res.status, 'HTTP_ERROR', res.statusText || 'request failed');
    }
    return undefined as T;
  }
  let data: unknown;
  try {
    data = JSON.parse(text) as unknown;
  } catch {
    throw new ApiError(res.status, 'BAD_JSON', 'response is not JSON');
  }
  if (!res.ok) {
    const body = data as ApiErrorBody;
    const code = body?.error?.code ?? 'HTTP_ERROR';
    const message = body?.error?.message ?? res.statusText ?? 'request failed';
    throw new ApiError(res.status, code, message);
  }
  return data as T;
}

export async function getHealth(): Promise<HealthResponse> {
  const res = await fetch(`${API}/health`);
  return parseJson<HealthResponse>(res);
}

export async function getVersion(): Promise<VersionResponse> {
  const res = await fetch(`${API}/version`);
  return parseJson<VersionResponse>(res);
}

export async function listBoards(): Promise<BoardSummary[]> {
  const res = await fetch(`${API}/boards`);
  return parseJson<BoardSummary[]>(res);
}

/** Multipart upload (field name "file"). Returns content-addressed id even when parse fails. */
export async function uploadBoard(file: File): Promise<UploadBoardResult> {
  const form = new FormData();
  form.append('file', file, file.name);
  const res = await fetch(`${API}/boards`, {
    method: 'POST',
    body: form,
  });
  return parseJson<UploadBoardResult>(res);
}

export async function getBoard(id: string): Promise<BoardDocument> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(id)}`);
  return parseJson<BoardDocument>(res);
}

export async function getBoardMeta(id: string): Promise<BoardMeta> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(id)}/meta`);
  return parseJson<BoardMeta>(res);
}

export async function deleteBoard(id: string): Promise<void> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(id)}`, {
    method: 'DELETE',
  });
  if (res.status === 204) return;
  await parseJson<void>(res);
}

export async function getOverlays(id: string): Promise<OverlayDocument> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(id)}/overlays`);
  return parseJson<OverlayDocument>(res);
}

/** Replace partInfos/netInfos; annotation array in body is ignored by the server. */
export async function putOverlays(
  id: string,
  body: Pick<OverlayDocument, 'partInfos' | 'netInfos'> | OverlayDocument,
): Promise<OverlayDocument> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(id)}/overlays`, {
    method: 'PUT',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  return parseJson<OverlayDocument>(res);
}

/** Create freeform annotation. Brief interface is void; server returns 201 + object. */
export async function postAnnotation(id: string, body: NewAnnotation): Promise<void> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(id)}/annotations`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  await parseJson<OverlayAnnotation>(res);
}

export async function patchAnnotation(
  id: string,
  annId: number,
  note: string,
): Promise<OverlayAnnotation> {
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(id)}/annotations/${annId}`,
    {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ note }),
    },
  );
  return parseJson<OverlayAnnotation>(res);
}

export async function deleteAnnotation(id: string, annId: number): Promise<void> {
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(id)}/annotations/${annId}`,
    { method: 'DELETE' },
  );
  if (res.status === 204) return;
  await parseJson<void>(res);
}
