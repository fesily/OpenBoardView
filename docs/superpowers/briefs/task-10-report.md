# Task 10 Report: Client search + highlight

**Status:** DONE  
**Branch:** `merge_upsteam_my`  
**Base:** `d6f4230` (Task 9 pin.diameter radius fix)  
**Commit:** `feat(web): client part/net search and highlight`  
**Date:** 2026-07-27

---

## Summary

Ported desktop `Searcher` / `SearchMode` (case-insensitive Sub / Prefix / Whole) to the web client and wired live highlight + click-to-center:

- `searchParts` / `searchNets` over `BoardDocument` (order preserved, optional limit)
- `highlightFromMatches` → part name set + pin ids (part pins ∪ net pins)
- `SearchBox`: query, Parts/Nets toggles, mode radios, result lists
- Typing updates canvas highlights; clicking a hit isolates that match and centers look-at (`mx`/`my`) on part center or first net pin
- No overlay editor (Task 11)

---

## Files

| Path | Role |
|------|------|
| `web/src/search/search.ts` | SearchMode, matchesSearch, searchParts/Nets, highlight/focus helpers |
| `web/src/search/search.test.ts` | Vitest: sub/prefix/whole, limit, highlight/focus |
| `web/src/ui/SearchBox.tsx` | Search UI + selection callbacks |
| `web/src/ui/App.tsx` | Wire SearchBox + highlight/focus into BoardCanvas |
| `web/src/scene/BoardCanvas.tsx` | Highlight props + focusToken center |
| `web/src/index.css` | Search box styles |

---

## Search semantics (from Searcher.cpp)

- Case-insensitive via lowercased `indexOf` (desktop `strcasestr`)
- **sub:** match anywhere
- **prefix:** match must start at index 0
- **whole:** match at 0 and equal length
- Empty query → no results / clear highlights

---

## Verification

```text
cd web && npm test
# → 26 passed (transform + hitTest + search)

cd web && npm run build
# → tsc -b && vite build OK
```

Manual: load board → type part/net → highlights update; click result → view centers on match.

---

## Out of scope

- Overlay editor / annotations (Task 11)
- Server-side search index
- SpellCorrector “did you mean”
- searchableStringDetails (mfgcode etc.) — name-only MVP
