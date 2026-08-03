# Net show-name Rename (Agent REST) — Design Spec

**Date:** 2026-08-01  
**Status:** Approved for implementation  
**Parent:** agent pin/part overlay APIs  
**Goal:** Agent-facing batch PATCH to set/clear overlay net display names (`NetInfos[].showname`) without mutating board netlist names.

---

## 1. Goals / non-goals

### Goals

1. `PATCH /api/v1/boards/:ref/nets` — batch update `showname` for board nets.
2. `GET /api/v1/boards/:ref/nets/:netName` — read one net’s board name + overlay display fields.
3. Persist via existing YAML `NetInfos` (desktop-compatible `showname`).
4. Geometry/netlist remain read-only; only overlay writable.

### Non-goals

- Renaming board-file `net.name` / rewiring netlist  
- Merging/splitting nets  
- Editing `note` in this MVP (field remains readable on GET)  
- Web UI changes (already has full PUT overlays path)

### Success criteria

1. PATCH writes `netInfos[name].showname`; empty string clears.  
2. Unknown board net name → 400 `UNKNOWN_NET`.  
3. Desktop/web display priority: overlay showname > board name (existing).  
4. HTTP suite cases green.

---

## 2. Data model

Desktop already loads:

```
netInfos[boardNetName].showname → net->show_name
```

YAML key = **board net name** (export `nets[].name`).

Display:

```
displayName = trim(showname) non-empty ? showname : boardName
```

---

## 3. HTTP

### 3.1 Batch PATCH

```
PATCH /api/v1/boards/:ref/nets
Content-Type: application/json

{
  "nets": {
    "N1234": { "showname": "I2C_SDA" },
    "GND": { "showname": "" }
  }
}
```

Rules:

| rule | value |
|------|--------|
| max nets per request | 512 |
| showname max length | 128 after trim |
| empty/whitespace showname | clear overlay showname |
| only field allowed in object | `showname` |
| unknown board net | 400 `UNKNOWN_NET` (list first few) |
| empty `nets` object | 400 `BAD_REQUEST` |

Flow: ResolveRef → parse board → validate all keys exist as net names → lock overlay → load → set/clear NetInfo → prune empty NetInfo → SavePartNetYaml → respond.

**200:**

```json
{
  "boardId": "...",
  "updated": [
    { "name": "N1234", "showname": "I2C_SDA", "displayName": "I2C_SDA" },
    { "name": "GND", "showname": "", "displayName": "GND" }
  ]
}
```

### 3.2 GET one net

```
GET /api/v1/boards/:ref/nets/:netName
```

URL-encode net names (`GND`, names with `/` etc).

**200:**

```json
{
  "boardId": "...",
  "sourceName": "...",
  "name": "N1234",
  "displayName": "I2C_SDA",
  "showname": "I2C_SDA",
  "note": "",
  "isGround": false
}
```

404 `NET_NOT_FOUND` if board has no such net name.

### 3.3 Errors

Unified `{error:{code,message}}`: `NOT_FOUND`, `NET_NOT_FOUND`, `UNKNOWN_NET`, `BAD_REQUEST`, `PARSE_FAILED`, `OVERLAY_*`, `BOARD_REF_AMBIGUOUS`.

---

## 4. Implementation

- `routes.cpp`: GET + PATCH; reuse MiniJson / overlay lock / ResolveRef  
- Net existence: scan `board.Nets()` by `name`  
- Ground flag: name `GND`/`GROUND` (match desktop/web)  
- Tests: `scripts/test_agent_api.py` cases for patch/clear/unknown/get  

---

## 5. Approval

Approved 2026-08-01: batch PATCH showname by board net name + GET one net.
