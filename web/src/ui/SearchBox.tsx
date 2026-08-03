import { useEffect, useMemo, useState } from 'react';
import {
  focusPointForResult,
  highlightFromMatches,
  searchNets,
  searchParts,
  type SearchHighlight,
  type SearchMode,
} from '../search/search';
import type { BoardDocument, OverlayDocument } from '../types/board';

const DEFAULT_LIMIT = 40;

export interface SearchSelection {
  highlight: SearchHighlight;
  /** Board-space point to center when a result is clicked; null when typing only. */
  focus: { x: number; y: number } | null;
  /** Bumps when user clicks a result so canvas can re-center even on same point. */
  focusToken: number;
}

export interface SearchBoxProps {
  board: BoardDocument;
  /** When set, net search also matches overlay showname. */
  overlay?: OverlayDocument | null;
  onSelectionChange: (sel: SearchSelection) => void;
}

export default function SearchBox({ board, overlay = null, onSelectionChange }: SearchBoxProps) {
  const [query, setQuery] = useState('');
  const [mode, setMode] = useState<SearchMode>('sub');
  const [includeParts, setIncludeParts] = useState(true);
  const [includeNets, setIncludeNets] = useState(true);
  const [focusToken, setFocusToken] = useState(0);

  const partHits = useMemo(
    () => (includeParts ? searchParts(board, query.trim(), mode, DEFAULT_LIMIT) : []),
    [board, query, mode, includeParts],
  );
  const netHits = useMemo(
    () => (includeNets ? searchNets(board, query.trim(), mode, DEFAULT_LIMIT, overlay) : []),
    [board, overlay, query, mode, includeNets],
  );

  // Typing / mode / toggles → live highlight all current hits (no forced center).
  useEffect(() => {
    const highlight = highlightFromMatches(board, partHits, netHits);
    onSelectionChange({ highlight, focus: null, focusToken: 0 });
  }, [board, partHits, netHits, onSelectionChange]);

  const onPick = (kind: 'part' | 'net', name: string) => {
    const partNames = kind === 'part' ? [name] : [];
    const netNames = kind === 'net' ? [name] : [];
    const highlight = highlightFromMatches(board, partNames, netNames);
    const focus = focusPointForResult(board, kind, name);
    const token = focusToken + 1;
    setFocusToken(token);
    onSelectionChange({ highlight, focus, focusToken: token });
  };

  const empty = !query.trim();
  const noHits = !empty && partHits.length === 0 && netHits.length === 0;

  return (
    <div className="search-box">
      <div className="search-box-row">
        <input
          type="search"
          className="search-input"
          placeholder="Search parts / nets…"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
          aria-label="Search parts and nets"
        />
        <label className="search-check">
          <input
            type="checkbox"
            checked={includeParts}
            onChange={(e) => setIncludeParts(e.target.checked)}
          />
          Parts
        </label>
        <label className="search-check">
          <input
            type="checkbox"
            checked={includeNets}
            onChange={(e) => setIncludeNets(e.target.checked)}
          />
          Nets
        </label>
      </div>
      <div className="search-modes" role="radiogroup" aria-label="Search mode">
        {(
          [
            ['sub', 'Substring'],
            ['prefix', 'Prefix'],
            ['whole', 'Whole'],
          ] as const
        ).map(([value, label]) => (
          <label key={value} className="search-mode">
            <input
              type="radio"
              name="search-mode"
              checked={mode === value}
              onChange={() => setMode(value)}
            />
            {label}
          </label>
        ))}
      </div>
      <div className="search-results">
        {empty && <p className="muted search-hint">Type to filter; click a hit to center.</p>}
        {noHits && <p className="muted search-hint">No matches.</p>}
        {partHits.length > 0 && (
          <div className="search-group">
            <div className="search-group-title">Parts ({partHits.length})</div>
            <ul>
              {partHits.map((name) => (
                <li key={`p:${name}`}>
                  <button type="button" className="search-hit" onClick={() => onPick('part', name)}>
                    {name}
                  </button>
                </li>
              ))}
            </ul>
          </div>
        )}
        {netHits.length > 0 && (
          <div className="search-group">
            <div className="search-group-title">Nets ({netHits.length})</div>
            <ul>
              {netHits.map((name) => (
                <li key={`n:${name}`}>
                  <button type="button" className="search-hit" onClick={() => onPick('net', name)}>
                    {name}
                  </button>
                </li>
              ))}
            </ul>
          </div>
        )}
      </div>
    </div>
  );
}
