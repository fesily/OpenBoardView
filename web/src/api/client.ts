import type {
  ApiErrorBody,
  BoardDocument,
  BoardMeta,
  BoardSummary,
  HealthResponse,
  NewAnnotation,
  OperatingCondition,
  OverlayAnnotation,
  OverlayDocument,
  PartConditionsView,
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

/** Server public config (no secrets). boardRoot is the library scan root. */
export async function getServerConfig(): Promise<{
  boardRoot: string;
  host?: string;
  port?: number;
}> {
  const res = await fetch(`${API}/config`);
  return parseJson<{ boardRoot: string; host?: string; port?: number }>(res);
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

/** Create freeform annotation. Server returns 201 + created object. */
export async function postAnnotation(
  id: string,
  body: NewAnnotation,
): Promise<OverlayAnnotation> {
  const res = await fetch(`${API}/boards/${encodeURIComponent(id)}/annotations`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(body),
  });
  return parseJson<OverlayAnnotation>(res);
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

/** GET single part placement + overlay summary (includes partInfo). */
export async function getPart(boardId: string, part: string): Promise<unknown> {
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(boardId)}/parts/${encodeURIComponent(part)}`,
  );
  return parseJson(res);
}

/** PATCH part overlay fields (currently part_type bind). */
export async function patchPart(
  boardId: string,
  part: string,
  body: { part_type?: string },
): Promise<unknown> {
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(boardId)}/parts/${encodeURIComponent(part)}`,
    {
      method: 'PATCH',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    },
  );
  return parseJson(res);
}

/**
 * Merged operating conditions for a placement.
 * Prefer `effective`; fall back to `operating_conditions` when present.
 */
export async function getPartOperatingConditions(
  boardId: string,
  part: string,
): Promise<PartConditionsView> {
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(boardId)}/parts/${encodeURIComponent(part)}/operating-conditions`,
  );
  const data = await parseJson<PartConditionsView>(res);
  return normalizePartConditionsView(data);
}

/** Promote board-local conditions into the chip library for this part_type. */
export async function promotePartOperatingConditions(
  boardId: string,
  part: string,
  clearBoard = false,
): Promise<PartConditionsView> {
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(boardId)}/parts/${encodeURIComponent(part)}/operating-conditions/promote`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ clearBoard }),
    },
  );
  const data = await parseJson<PartConditionsView>(res);
  return normalizePartConditionsView(data);
}

/** Create one condition on board (default) or chip library (`scope=chip`). */
export async function postPartOperatingCondition(
  boardId: string,
  part: string,
  body: OperatingCondition,
  scope?: 'chip' | 'board',
): Promise<OperatingCondition> {
  const q = scope === 'chip' ? '?scope=chip' : '';
  const res = await fetch(
    `${API}/boards/${encodeURIComponent(boardId)}/parts/${encodeURIComponent(part)}/operating-conditions${q}`,
    {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(body),
    },
  );
  return parseJson<OperatingCondition>(res);
}

function normalizePartConditionsView(data: PartConditionsView): PartConditionsView {
  const effective =
    data.effective ??
    data.operating_conditions ??
    [];
  return {
    ...data,
    source: data.source ?? 'none',
    effective,
    board: data.board ?? [],
    chip: data.chip ?? [],
    operating_conditions: data.operating_conditions ?? effective,
    chipPins: data.chipPins ?? [],
    resolved: data.resolved ?? {},
  };
}
