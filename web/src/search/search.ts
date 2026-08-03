import type { BoardDocument, OverlayDocument } from '../types/board';

/** Mirrors desktop SearchMode in Searcher.h (default Sub). */
export type SearchMode = 'sub' | 'prefix' | 'whole';

/**
 * Case-insensitive match mirroring Searcher::strstrModeSearch:
 * - sub: needle anywhere in haystack
 * - prefix: match must start at haystack[0]
 * - whole: full-string equality (case-insensitive)
 */
export function matchesSearch(haystack: string, needle: string, mode: SearchMode): boolean {
  if (!needle) return false;
  const h = haystack.toLowerCase();
  const n = needle.toLowerCase();
  const idx = h.indexOf(n);
  if (idx < 0) return false;
  if (mode === 'sub') return true;
  if (mode === 'prefix') return idx === 0;
  // whole
  return idx === 0 && h.length === n.length;
}

/** Component names matching q (order preserved, capped by limit; limit ≤ 0 → unlimited). */
export function searchParts(
  board: BoardDocument,
  q: string,
  mode: SearchMode,
  limit: number,
): string[] {
  if (!q) return [];
  const out: string[] = [];
  for (const c of board.components ?? []) {
    if (!matchesSearch(c.name, q, mode)) continue;
    out.push(c.name);
    if (limit > 0 && out.length >= limit) break;
  }
  return out;
}

/** Net board-names matching q on name, board showName, or overlay showname. */
export function searchNets(
  board: BoardDocument,
  q: string,
  mode: SearchMode,
  limit: number,
  overlay?: OverlayDocument | null,
): string[] {
  if (!q) return [];
  const out: string[] = [];
  for (const n of board.nets ?? []) {
    const ov = overlay?.netInfos?.[n.name]?.showname?.trim() || '';
    const boardShow = (n.showName || '').trim();
    const hit =
      matchesSearch(n.name, q, mode) ||
      (ov ? matchesSearch(ov, q, mode) : false) ||
      (boardShow ? matchesSearch(boardShow, q, mode) : false);
    if (!hit) continue;
    out.push(n.name);
    if (limit > 0 && out.length >= limit) break;
  }
  return out;
}

export interface SearchHighlight {
  partNames: Set<string>;
  pinIds: Set<string>;
  /** Nets matched by search — copper on these nets always draws. */
  netIds: Set<number>;
}

/** Build canvas highlight sets for matched part names and net names. */
export function highlightFromMatches(
  board: BoardDocument,
  partNames: readonly string[],
  netNames: readonly string[],
): SearchHighlight {
  const partSet = new Set(partNames);
  const netNameSet = new Set(netNames);
  const netIds = new Set<number>();
  for (const n of board.nets ?? []) {
    if (netNameSet.has(n.name)) netIds.add(n.id);
  }
  const pinIds = new Set<string>();
  for (const pin of board.pins ?? []) {
    if (pin.component && partSet.has(pin.component)) pinIds.add(pin.id);
    if (pin.netId != null && netIds.has(pin.netId)) pinIds.add(pin.id);
  }
  return { partNames: partSet, pinIds, netIds };
}

/** Board-space focus point for a part (center) or first pin of a net. */
export function focusPointForResult(
  board: BoardDocument,
  kind: 'part' | 'net',
  name: string,
): { x: number; y: number } | null {
  if (kind === 'part') {
    const part = (board.components ?? []).find((c) => c.name === name);
    if (!part) return null;
    if (part.center && Number.isFinite(part.center.x) && Number.isFinite(part.center.y)) {
      return { x: part.center.x, y: part.center.y };
    }
    const pin = (board.pins ?? []).find((p) => p.component === name);
    return pin ? { x: pin.pos.x, y: pin.pos.y } : null;
  }
  const net = (board.nets ?? []).find((n) => n.name === name);
  if (!net) return null;
  const pin = (board.pins ?? []).find((p) => p.netId === net.id);
  return pin ? { x: pin.pos.x, y: pin.pos.y } : null;
}
