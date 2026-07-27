### Task 10: Client search + highlight

**Files:**
- Create: `web/src/search/search.ts`
- Modify: `App.tsx`, `BoardCanvas.tsx`, `SearchBox.tsx`

**Interfaces:**
```ts
export type SearchMode = 'sub' | 'prefix' | 'whole';
export function searchParts(board: BoardDocument, q: string, mode: SearchMode, limit: number): string[]; // component names
export function searchNets(board: BoardDocument, q: string, mode: SearchMode, limit: number): string[]; // net names
```

Mirror `Searcher` / `SearchMode` in `Searcher.cpp` (case-insensitive substring default).

- [ ] **Step 1: Vitest cases for sub/prefix/whole**

- [ ] **Step 2: UI: typing filters highlights; click result centers view**

- [ ] **Step 3: Commit**

```bash
git commit -am "feat(web): client part/net search and highlight"
```

---
