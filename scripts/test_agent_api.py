#!/usr/bin/env python3
"""Agent pin / part operating-conditions API test suite.

Prereqs:
  - obv_server built (default: build-web/src/obv_server/Release/obv_server.exe)
  - board library with at least one multi-pin part (default: data/boards)

Usage (PowerShell / cmd, repo root):

  # Auto-start server on 18080 against data/boards, run tests, stop:
  python scripts/test_agent_api.py

  # Attach to an already-running server:
  python scripts/test_agent_api.py --base http://127.0.0.1:8080 --no-start

  # Custom boards / binary / port:
  python scripts/test_agent_api.py --boards data/boards --port 18080 \\
      --server build-web/src/obv_server/Release/obv_server.exe

  # Only run selected cases:
  python scripts/test_agent_api.py -k pin,conditions

Env overrides:
  OBV_TEST_BASE, OBV_TEST_BOARDS, OBV_TEST_SERVER, OBV_TEST_PORT

Exit 0 = all pass; non-zero = failures printed.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import traceback
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Optional


# ---------------------------------------------------------------------------
# HTTP helpers
# ---------------------------------------------------------------------------

class HttpError(Exception):
    def __init__(self, status: int, body: str, url: str):
        super().__init__(f"HTTP {status} {url}: {body[:300]}")
        self.status = status
        self.body = body
        self.url = url


def _urljoin(base: str, path: str) -> str:
    return base.rstrip("/") + "/" + path.lstrip("/")


def request(
    base: str,
    method: str,
    path: str,
    *,
    body: Any = None,
    expect: Optional[int | tuple[int, ...]] = None,
    timeout: float = 30.0,
) -> tuple[int, Any]:
    url = _urljoin(base, path)
    data = None
    headers = {"Accept": "application/json"}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method.upper())
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read().decode("utf-8", errors="replace")
            status = resp.status
    except urllib.error.HTTPError as e:
        raw = e.read().decode("utf-8", errors="replace")
        status = e.code
    except urllib.error.URLError as e:
        raise RuntimeError(f"request failed {method} {url}: {e}") from e

    parsed: Any = raw
    if raw:
        try:
            parsed = json.loads(raw)
        except json.JSONDecodeError:
            parsed = raw

    if expect is not None:
        allowed = expect if isinstance(expect, tuple) else (expect,)
        if status not in allowed:
            raise HttpError(status, raw if isinstance(parsed, str) else json.dumps(parsed), url)
    return status, parsed


def request_raw(
    base: str,
    method: str,
    path: str,
    *,
    body: Any = None,
    expect: Optional[int | tuple[int, ...]] = None,
    timeout: float = 60.0,
    accept: str = "*/*",
) -> tuple[int, bytes, dict[str, str]]:
    """HTTP request returning raw body bytes (no JSON parse). Headers lower-cased."""
    url = _urljoin(base, path)
    data = None
    headers = {"Accept": accept}
    if body is not None:
        data = json.dumps(body).encode("utf-8")
        headers["Content-Type"] = "application/json"
    req = urllib.request.Request(url, data=data, headers=headers, method=method.upper())
    resp_headers: dict[str, str] = {}
    try:
        with urllib.request.urlopen(req, timeout=timeout) as resp:
            raw = resp.read()
            status = resp.status
            for k, v in resp.headers.items():
                resp_headers[k.lower()] = v
    except urllib.error.HTTPError as e:
        raw = e.read()
        status = e.code
        for k, v in e.headers.items():
            resp_headers[k.lower()] = v
    except urllib.error.URLError as e:
        raise RuntimeError(f"request failed {method} {url}: {e}") from e

    if expect is not None:
        allowed = expect if isinstance(expect, tuple) else (expect,)
        if status not in allowed:
            preview = raw[:300]
            try:
                preview_s = preview.decode("utf-8", errors="replace")
            except Exception:
                preview_s = repr(preview)
            raise HttpError(status, preview_s, url)
    return status, raw, resp_headers


def enc(segment: str) -> str:
    """Path segment encoding (spaces, #, / etc)."""
    return urllib.parse.quote(str(segment), safe="")


# ---------------------------------------------------------------------------
# Assertions
# ---------------------------------------------------------------------------

class AssertError(Exception):
    pass


def check(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertError(msg)


def check_eq(a: Any, b: Any, msg: str = "") -> None:
    if a != b:
        raise AssertError(msg or f"expected {b!r}, got {a!r}")


def check_in(item: Any, container: Any, msg: str = "") -> None:
    if item not in container:
        raise AssertError(msg or f"{item!r} not in {container!r}")


def error_code(body: Any) -> str:
    if isinstance(body, dict) and isinstance(body.get("error"), dict):
        return str(body["error"].get("code") or "")
    return ""


# ---------------------------------------------------------------------------
# Fixtures from live board
# ---------------------------------------------------------------------------

@dataclass
class Fixture:
    board_id: str
    board_name: str
    board_path: str
    part: str
    pin_ref: str
    pin_number: str
    pin_name: str
    pin_id: str
    net_id: Any
    net_name: str
    other_part: str  # second part if available (for isolation)
    multi_pin_part: str


def pick_fixture(base: str) -> Fixture:
    _, boards = request(base, "GET", "/api/v1/boards", expect=200)
    check(isinstance(boards, list) and len(boards) > 0, "library has no boards — set --boards")
    entry = boards[0]
    board_id = entry["id"]
    board_name = entry.get("name") or ""
    board_path = entry.get("path") or board_name

    _, board = request(base, "GET", f"/api/v1/boards/{enc(board_id)}", expect=200)
    comps = board.get("components") or []
    pins = board.get("pins") or []
    check(comps and pins, "board has no components/pins")

    multi = None
    for c in comps:
        if c.get("name") and len(c.get("pins") or []) >= 2:
            multi = c["name"]
            break
    if not multi:
        for c in comps:
            if c.get("name") and c.get("pins"):
                multi = c["name"]
                break
    check(multi, "no named part with pins")

    pin_obj = None
    for p in pins:
        if p.get("component") == multi:
            pin_obj = p
            break
    check(pin_obj, f"no pin under part {multi}")

    other = ""
    for c in comps:
        n = c.get("name") or ""
        if n and n != multi:
            other = n
            break

    return Fixture(
        board_id=board_id,
        board_name=board_name,
        board_path=board_path,
        part=multi,
        pin_ref=pin_obj.get("number") or pin_obj.get("name") or pin_obj.get("id"),
        pin_number=str(pin_obj.get("number") or ""),
        pin_name=str(pin_obj.get("name") or ""),
        pin_id=str(pin_obj.get("id") or ""),
        net_id=pin_obj.get("netId"),
        net_name=_pick_net_name(board, pin_obj.get("netId")),
        other_part=other,
        multi_pin_part=multi,
    )



def _pick_net_name(board: dict, net_id: Any) -> str:
    nets = board.get("nets") or []
    if net_id is not None:
        for n in nets:
            if n.get("id") == net_id and n.get("name"):
                return str(n["name"])
    for n in nets:
        name = str(n.get("name") or "")
        if not name:
            continue
        upper = name.upper()
        if upper in ("GND", "GROUND") or upper.startswith("UNCONNECTED"):
            continue
        return name
    for n in nets:
        if n.get("name"):
            return str(n["name"])
    return ""


# ---------------------------------------------------------------------------
# Test cases
# ---------------------------------------------------------------------------

@dataclass
class CaseResult:
    name: str
    ok: bool
    detail: str = ""
    duration_ms: float = 0.0


@dataclass
class Suite:
    base: str
    fx: Fixture
    results: list[CaseResult] = field(default_factory=list)
    created_condition_ids: list[str] = field(default_factory=list)

    def run(self, name: str, fn: Callable[[], None]) -> None:
        t0 = time.perf_counter()
        try:
            fn()
            self.results.append(CaseResult(name, True, duration_ms=(time.perf_counter() - t0) * 1000))
            print(f"  PASS  {name}  ({self.results[-1].duration_ms:.0f} ms)")
        except Exception as e:
            detail = f"{type(e).__name__}: {e}"
            self.results.append(
                CaseResult(name, False, detail=detail, duration_ms=(time.perf_counter() - t0) * 1000)
            )
            print(f"  FAIL  {name}  ({self.results[-1].duration_ms:.0f} ms)")
            print(f"        {detail}")
            if os.environ.get("OBV_TEST_TRACE"):
                traceback.print_exc()

    # ---- helpers ----
    def parts_base(self, ref: Optional[str] = None) -> str:
        r = ref if ref is not None else self.fx.board_id
        return f"/api/v1/boards/{enc(r)}/parts/{enc(self.fx.part)}"

    def pin_path(self, pin: Optional[str] = None, ref: Optional[str] = None) -> str:
        p = pin if pin is not None else self.fx.pin_ref
        return f"{self.parts_base(ref)}/pins/{enc(p)}"

    def conds_path(self, ref: Optional[str] = None) -> str:
        return f"{self.parts_base(ref)}/operating-conditions"

    def cond_path(self, cid: str, ref: Optional[str] = None) -> str:
        return f"{self.conds_path(ref)}/{enc(cid)}"

    # ---- cases ----
    def t_health(self) -> None:
        st, body = request(self.base, "GET", "/api/v1/health", expect=200)
        check_eq(st, 200)
        check(isinstance(body, dict) and body.get("status") == "ok", f"health body={body}")

    def t_list_boards(self) -> None:
        _, boards = request(self.base, "GET", "/api/v1/boards", expect=200)
        check(isinstance(boards, list) and boards, "empty board list")
        b0 = boards[0]
        for k in ("id", "name", "ok"):
            check_in(k, b0, f"board entry missing {k}")
        check(len(b0["id"]) == 64, f"boardId not 64 hex: {b0['id']!r}")

    def t_pin_resolve_by_id(self) -> None:
        _, body = request(self.base, "GET", self.pin_path(), expect=200)
        for k in ("boardId", "sourceName", "part", "pinKey", "pin", "measurements", "overlay"):
            check_in(k, body, f"pin response missing {k}")
        check_eq(body["boardId"], self.fx.board_id)
        check_eq(body["part"], self.fx.part)
        pin = body["pin"]
        for k in ("id", "component", "number", "name", "type", "side", "pos"):
            check_in(k, pin, f"pin.{k} missing")
        check_eq(pin.get("component"), self.fx.part)
        meas = body["measurements"]
        for mode in ("diode", "voltage", "ohm", "ohm_black"):
            check_in(mode, meas)
            m = meas[mode]
            for k in ("local", "effective", "board", "overlay"):
                check_in(k, m, f"measurements.{mode}.{k}")
            check_in(m["local"].get("source"), ("overlay", "board", "none"))
            check_in(m["effective"].get("source"), ("overlay", "board", "propagated", "none"))
            if m["effective"].get("source") == "propagated":
                check_in("from", m["effective"], "propagated without from")
                fr = m["effective"]["from"]
                for k in ("component", "pinKey", "pinId"):
                    check_in(k, fr)
        ov = body["overlay"]
        for k in ("note", "show_name", "voltage_flag"):
            check_in(k, ov)

    def t_pin_resolve_by_filename(self) -> None:
        name = self.fx.board_name
        check(name, "board name empty")
        _, body = request(self.base, "GET", self.pin_path(ref=name), expect=200)
        check_eq(body["boardId"], self.fx.board_id)
        check_eq(body["part"], self.fx.part)

    def t_pin_resolve_by_path(self) -> None:
        path = self.fx.board_path
        check(path, "board path empty")
        _, body = request(self.base, "GET", self.pin_path(ref=path), expect=200)
        check_eq(body["boardId"], self.fx.board_id)

    def t_pin_match_number_name_id(self) -> None:
        # Resolve via each available identity field; all must hit same part pinKey family.
        refs = []
        if self.fx.pin_number:
            refs.append(("number", self.fx.pin_number))
        if self.fx.pin_name:
            refs.append(("name", self.fx.pin_name))
        if self.fx.pin_id:
            refs.append(("id", self.fx.pin_id))
        check(refs, "no pin identity fields")
        keys = set()
        for label, ref in refs:
            _, body = request(self.base, "GET", self.pin_path(pin=ref), expect=200)
            keys.add(body.get("pinKey"))
            check_eq(body["part"], self.fx.part, f"match via {label} wrong part")
        # pinKey may differ only if name/number/id map differently; at least all 200s
        check(len(keys) >= 1, "no pinKey")

    def t_pin_not_found(self) -> None:
        st, body = request(
            self.base, "GET", self.pin_path(pin="__no_such_pin_zz__"), expect=404
        )
        check_eq(st, 404)
        check_eq(error_code(body), "PIN_NOT_FOUND")

    def t_part_not_found_on_pin(self) -> None:
        path = f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc('__NoSuchPart__')}/pins/{enc('1')}"
        st, body = request(self.base, "GET", path, expect=404)
        check_eq(error_code(body), "PART_NOT_FOUND")

    def t_board_not_found(self) -> None:
        path = f"/api/v1/boards/{enc('__missing_board__.bvr')}/parts/{enc(self.fx.part)}/pins/{enc(self.fx.pin_ref)}"
        st, body = request(self.base, "GET", path, expect=404)
        check_eq(error_code(body), "NOT_FOUND")

    def t_part_summary(self) -> None:
        _, body = request(self.base, "GET", self.parts_base(), expect=200)
        for k in ("boardId", "sourceName", "part", "pins", "partInfo"):
            check_in(k, body, f"part summary missing {k}")
        check_eq(body["boardId"], self.fx.board_id)
        part = body["part"]
        for k in ("name", "side", "mount", "type", "center", "outline", "pins"):
            check_in(k, part, f"part.{k} missing")
        check_eq(part["name"], self.fx.part)
        check(isinstance(part["outline"], list), "outline must be array")
        check(isinstance(part["center"], dict) and "x" in part["center"] and "y" in part["center"],
              "center shape")
        check(isinstance(body["pins"], list) and len(body["pins"]) >= 1, "pins list empty")
        p0 = body["pins"][0]
        for k in ("id", "number", "name", "type"):
            check_in(k, p0, f"pins[0].{k}")
        pi = body["partInfo"]
        check_in("operating_conditions", pi)
        check(isinstance(pi["operating_conditions"], list), "operating_conditions not list")
        # sourceName must not look like absolute Windows path with drive root only check: no "C:\\Users"
        sn = str(body.get("sourceName") or "")
        check(not sn.startswith("C:\\Users") and not sn.startswith("/home/"),
              f"sourceName looks absolute: {sn!r}")

    def t_part_not_found(self) -> None:
        path = f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc('__NoSuchPart__')}"
        st, body = request(self.base, "GET", path, expect=404)
        check_eq(error_code(body), "PART_NOT_FOUND")

    def t_conditions_list_empty_or_array(self) -> None:
        _, body = request(self.base, "GET", self.conds_path(), expect=200)
        check_in("operating_conditions", body)
        check(isinstance(body["operating_conditions"], list), "not list")
        check_eq(body.get("boardId"), self.fx.board_id)
        check_eq(body.get("part"), self.fx.part)

    def t_conditions_crud_lifecycle(self) -> None:
        # POST create (auto id)
        st, created = request(
            self.base,
            "POST",
            self.conds_path(),
            body={
                "name": "agent_test_g1",
                "inputs": ["IN_A", "IN_B"],
                "outputs": ["OUT_Y"],
                "enables": ["EN"],
                "note": "created by scripts/test_agent_api.py",
            },
            expect=201,
        )
        check_eq(st, 201)
        for k in ("id", "name", "inputs", "outputs", "enables", "note"):
            check_in(k, created, f"created missing {k}")
        cid = created["id"]
        check(cid, "empty id")
        self.created_condition_ids.append(cid)
        check_eq(created["name"], "agent_test_g1")
        check_eq(created["inputs"], ["IN_A", "IN_B"])
        check_eq(created["outputs"], ["OUT_Y"])
        check_eq(created["enables"], ["EN"])

        # GET list contains it
        _, listed = request(self.base, "GET", self.conds_path(), expect=200)
        ids = [c.get("id") for c in listed["operating_conditions"]]
        check_in(cid, ids, f"{cid} not in list {ids}")

        # GET one
        _, one = request(self.base, "GET", self.cond_path(cid), expect=200)
        check_eq(one.get("id"), cid)
        check_eq(one.get("note"), "created by scripts/test_agent_api.py")

        # PUT one
        st, updated = request(
            self.base,
            "PUT",
            self.cond_path(cid),
            body={
                "name": "agent_test_g1_updated",
                "inputs": ["IN_A"],
                "outputs": ["OUT_Y", "OUT_Z"],
                "enables": ["EN"],
                "note": "updated",
            },
            expect=200,
        )
        check_eq(updated.get("id"), cid)
        check_eq(updated.get("name"), "agent_test_g1_updated")
        check_eq(updated.get("outputs"), ["OUT_Y", "OUT_Z"])
        check_eq(updated.get("note"), "updated")

        # POST with explicit id
        cid2 = "oc_agent_explicit"
        st, c2 = request(
            self.base,
            "POST",
            self.conds_path(),
            body={
                "id": cid2,
                "name": "explicit",
                "inputs": [],
                "outputs": ["X"],
                "enables": [],
                "note": "",
            },
            expect=201,
        )
        check_eq(c2.get("id"), cid2)
        self.created_condition_ids.append(cid2)

        # POST conflict
        st, err = request(
            self.base,
            "POST",
            self.conds_path(),
            body={"id": cid2, "name": "dup", "inputs": [], "outputs": [], "enables": [], "note": ""},
            expect=409,
        )
        check_eq(error_code(err), "CONDITION_ID_CONFLICT")

        # POST whitespace id → should allocate (normalize first)
        st, c3 = request(
            self.base,
            "POST",
            self.conds_path(),
            body={
                "id": "   ",
                "name": "ws_id",
                "inputs": ["W"],
                "outputs": [],
                "enables": [],
                "note": "",
            },
            expect=201,
        )
        check(c3.get("id") and c3["id"].strip(), f"whitespace id not allocated: {c3!r}")
        check(c3["id"] != "   ", "id still whitespace")
        self.created_condition_ids.append(c3["id"])

        # DELETE missing → 404
        st, err = request(
            self.base, "DELETE", self.cond_path("__nope_missing__"), expect=404
        )
        check_eq(error_code(err), "CONDITION_NOT_FOUND")

        # DELETE one
        st, _ = request(self.base, "DELETE", self.cond_path(cid2), expect=(200, 204))
        self.created_condition_ids = [x for x in self.created_condition_ids if x != cid2]
        st, err = request(self.base, "GET", self.cond_path(cid2), expect=404)
        check_eq(error_code(err), "CONDITION_NOT_FOUND")

        # PUT full replace (must not wipe unrelated pins — we only check conditions array)
        replace_body = {
            "operating_conditions": [
                {
                    "id": "oc_replace_1",
                    "name": "r1",
                    "inputs": ["I1"],
                    "outputs": ["O1"],
                    "enables": [],
                    "note": "replace",
                }
            ]
        }
        st, replaced = request(
            self.base, "PUT", self.conds_path(), body=replace_body, expect=200
        )
        ocs = replaced.get("operating_conditions")
        if ocs is None and isinstance(replaced, dict) and "id" in replaced:
            # some impls return list wrapper differently
            _, listed = request(self.base, "GET", self.conds_path(), expect=200)
            ocs = listed["operating_conditions"]
        else:
            # response might be list wrapper
            if "operating_conditions" not in replaced:
                _, listed = request(self.base, "GET", self.conds_path(), expect=200)
                ocs = listed["operating_conditions"]
            else:
                ocs = replaced["operating_conditions"]
        check(isinstance(ocs, list), f"replace response ocs={replaced!r}")
        check_eq(len(ocs), 1, f"expected 1 after replace, got {ocs!r}")
        check_eq(ocs[0].get("id"), "oc_replace_1")
        self.created_condition_ids = ["oc_replace_1"]

        # part summary still has pins (not wiped)
        _, summary = request(self.base, "GET", self.parts_base(), expect=200)
        check(isinstance(summary.get("pins"), list) and summary["pins"], "pins wiped after OC replace")
        check_eq(len(summary["partInfo"]["operating_conditions"]), 1)

        # cleanup remaining
        for x in list(self.created_condition_ids):
            request(self.base, "DELETE", self.cond_path(x), expect=(200, 204, 404))
        self.created_condition_ids.clear()

        _, listed = request(self.base, "GET", self.conds_path(), expect=200)
        # may still have pre-existing user conditions; just ensure our replace id gone
        ids = [c.get("id") for c in listed["operating_conditions"]]
        check("oc_replace_1" not in ids, f"cleanup failed, still have {ids}")

    def t_conditions_persistence(self) -> None:
        """Write one condition, re-GET (same process) — disk path exercised by SavePartNetYaml."""
        st, created = request(
            self.base,
            "POST",
            self.conds_path(),
            body={
                "id": "oc_persist_smoke",
                "name": "persist",
                "inputs": ["P"],
                "outputs": ["Q"],
                "enables": [],
                "note": "persist-check",
            },
            expect=201,
        )
        self.created_condition_ids.append(created["id"])
        _, one = request(self.base, "GET", self.cond_path("oc_persist_smoke"), expect=200)
        check_eq(one.get("note"), "persist-check")
        # also via part summary
        _, summary = request(self.base, "GET", self.parts_base(), expect=200)
        ids = [c.get("id") for c in summary["partInfo"]["operating_conditions"]]
        check_in("oc_persist_smoke", ids)
        request(self.base, "DELETE", self.cond_path("oc_persist_smoke"), expect=(200, 204))
        self.created_condition_ids = [x for x in self.created_condition_ids if x != "oc_persist_smoke"]

    def t_conditions_validation(self) -> None:
        # id too long
        long_id = "a" * 65
        st, err = request(
            self.base,
            "POST",
            self.conds_path(),
            body={"id": long_id, "name": "x", "inputs": [], "outputs": [], "enables": [], "note": ""},
            expect=400,
        )
        check_eq(error_code(err), "BAD_REQUEST")
        # note too long
        st, err = request(
            self.base,
            "POST",
            self.conds_path(),
            body={
                "name": "x",
                "inputs": [],
                "outputs": [],
                "enables": [],
                "note": "n" * 2049,
            },
            expect=400,
        )
        check_eq(error_code(err), "BAD_REQUEST")

    def t_conditions_part_not_found(self) -> None:
        path = f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc('__NoSuchPart__')}/operating-conditions"
        st, body = request(
            self.base,
            "POST",
            path,
            body={"name": "x", "inputs": [], "outputs": [], "enables": [], "note": ""},
            expect=404,
        )
        check_eq(error_code(body), "PART_NOT_FOUND")

    def screenshot_path(self, query: str = "", ref: Optional[str] = None) -> str:
        q = f"?{query}" if query else ""
        return f"{self.parts_base(ref)}/screenshot{q}"

    def screenshot_meta_path(self, query: str = "", ref: Optional[str] = None) -> str:
        q = f"?{query}" if query else ""
        return f"{self.parts_base(ref)}/screenshot/meta{q}"

    def pins_patch_path(self, ref: Optional[str] = None) -> str:
        return f"{self.parts_base(ref)}/pins"

    def pin_grid_path(self, ref: Optional[str] = None) -> str:
        return f"{self.parts_base(ref)}/pin-grid"

    def t_pin_grid_shape(self) -> None:
        _, body = request(self.base, "GET", self.pin_grid_path(), expect=200)
        for k in (
            "boardId",
            "sourceName",
            "part",
            "kind",
            "rows",
            "cols",
            "pitchX",
            "pitchY",
            "origin",
            "row0",
            "col0",
            "fillRatio",
            "warnings",
            "pins",
        ):
            check_in(k, body, f"pin-grid missing {k}")
        check_eq(body["boardId"], self.fx.board_id)
        check_eq(body["part"], self.fx.part)
        check_eq(body["row0"], "min_local_y")
        check_eq(body["col0"], "min_local_x")
        rows = int(body["rows"])
        cols = int(body["cols"])
        check(rows >= 1 and cols >= 1, f"rows/cols {rows}x{cols}")
        check_in(body["kind"], ("single", "row", "column", "grid", "sparse", "peripheral", "unordered"))
        pins = body["pins"]
        check(isinstance(pins, list) and pins, "pins empty")
        for p in pins:
            for k in ("key", "board", "row", "col", "displayLabel"):
                check_in(k, p, f"pin field {k}")
            check(0 <= int(p["row"]) < rows, f"row out of range {p}")
            check(0 <= int(p["col"]) < cols, f"col out of range {p}")
            check_in("x", p["board"])
            check_in("y", p["board"])
        # missing part
        st, err = request(
            self.base,
            "GET",
            f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc('__NoSuchPart__')}/pin-grid",
            expect=404,
        )
        check_eq(error_code(err), "PART_NOT_FOUND")


    def _pin_overlay_show_name(self) -> str:
        _, body = request(self.base, "GET", self.pin_path(), expect=200)
        ov = body.get("overlay") or {}
        return str(ov.get("show_name") or "")

    def _restore_pin_show_name(self, previous: str) -> None:
        """Restore overlay show_name (empty clears)."""
        request(
            self.base,
            "PATCH",
            self.pins_patch_path(),
            body={"pins": {str(self.fx.pin_ref): {"show_name": previous}}},
            expect=200,
        )

    def t_screenshot_png_magic(self) -> None:
        st, raw, headers = request_raw(
            self.base,
            "GET",
            self.screenshot_path(),
            expect=200,
            accept="image/png",
        )
        check_eq(st, 200)
        check(len(raw) >= 8, f"PNG too short: {len(raw)}")
        check(raw[:8] == b"\x89PNG\r\n\x1a\n", f"bad PNG magic: {raw[:8]!r}")
        ctype = (headers.get("content-type") or "").split(";")[0].strip().lower()
        check_eq(ctype, "image/png", f"Content-Type={headers.get('content-type')!r}")

    def t_screenshot_meta_shape(self) -> None:
        _, body = request(self.base, "GET", self.screenshot_meta_path(), expect=200)
        for k in ("boardId", "sourceName", "part", "image", "boardBounds", "transform", "pins"):
            check_in(k, body, f"meta missing {k}")
        check_eq(body["boardId"], self.fx.board_id)
        check_eq(body["part"], self.fx.part)
        img = body["image"]
        for k in ("width", "height", "scale", "padding", "labels", "partName"):
            check_in(k, img, f"image.{k}")
        w = int(img["width"])
        h = int(img["height"])
        check(w >= 1 and h >= 1, f"image size {w}x{h}")
        check(max(w, h) <= 2048, f"edge > 2048: {w}x{h}")
        bb = body["boardBounds"]
        for k in ("minX", "minY", "maxX", "maxY"):
            check_in(k, bb, f"boardBounds.{k}")
        tr = body["transform"]
        check_in("boardToImage", tr)
        b2i = tr["boardToImage"]
        for k in ("originBoardX", "originBoardY", "scale", "flipY"):
            check_in(k, b2i, f"transform.boardToImage.{k}")
        pins = body["pins"]
        check(isinstance(pins, list) and pins, "pins[] empty")
        p0 = pins[0]
        for k in (
            "key",
            "id",
            "number",
            "name",
            "boardShowName",
            "overlayShowName",
            "displayLabel",
            "board",
            "image",
            "type",
            "shape",
            "diameter",
            "netName",
        ):
            check_in(k, p0, f"pins[0].{k}")
        for p in pins:
            im = p.get("image") or {}
            check_in("x", im, "image.x")
            check_in("y", im, "image.y")
            ix = float(im["x"])
            iy = float(im["y"])
            # Allow a small margin outside the pixel grid (centers near edges).
            check(-2.0 <= ix <= w + 2.0, f"image.x out of range: {ix} (w={w})")
            check(-2.0 <= iy <= h + 2.0, f"image.y out of range: {iy} (h={h})")

    def t_screenshot_meta_matches_query(self) -> None:
        _, body = request(
            self.base, "GET", self.screenshot_meta_path("maxEdge=128"), expect=200
        )
        img = body["image"]
        w = int(img["width"])
        h = int(img["height"])
        check(w <= 128 and h <= 128, f"maxEdge=128 but image {w}x{h}")
        check(w >= 1 and h >= 1, f"degenerate image {w}x{h}")

    def t_pins_patch_show_name(self) -> None:
        prev = self._pin_overlay_show_name()
        marker = "agent_test_show_name"
        try:
            st, patched = request(
                self.base,
                "PATCH",
                self.pins_patch_path(),
                body={"pins": {str(self.fx.pin_ref): {"show_name": marker}}},
                expect=200,
            )
            check_eq(st, 200)
            check_eq(patched.get("boardId"), self.fx.board_id)
            check_eq(patched.get("part"), self.fx.part)
            updated = patched.get("updated")
            check(isinstance(updated, list) and len(updated) == 1, f"updated={updated!r}")
            u0 = updated[0]
            check_eq(u0.get("show_name"), marker)
            check_eq(u0.get("displayLabel"), marker)

            _, meta = request(self.base, "GET", self.screenshot_meta_path(), expect=200)
            match = None
            for p in meta.get("pins") or []:
                if (
                    str(p.get("key")) == str(self.fx.pin_ref)
                    or str(p.get("number")) == str(self.fx.pin_ref)
                    or str(p.get("name")) == str(self.fx.pin_ref)
                    or str(p.get("id")) == str(self.fx.pin_ref)
                ):
                    match = p
                    break
            if match is None and self.fx.pin_number:
                for p in meta.get("pins") or []:
                    if str(p.get("number")) == self.fx.pin_number:
                        match = p
                        break
            check(match is not None, f"pin not in meta pins for {self.fx.pin_ref!r}")
            check_eq(match.get("overlayShowName"), marker)
            check_eq(match.get("displayLabel"), marker)

            _, pin_body = request(self.base, "GET", self.pin_path(), expect=200)
            ov = pin_body.get("overlay") or {}
            check_eq(ov.get("show_name"), marker)
        finally:
            self._restore_pin_show_name(prev)

    def t_pins_patch_clear(self) -> None:
        prev = self._pin_overlay_show_name()
        try:
            request(
                self.base,
                "PATCH",
                self.pins_patch_path(),
                body={"pins": {str(self.fx.pin_ref): {"show_name": "agent_clear_tmp"}}},
                expect=200,
            )
            st, patched = request(
                self.base,
                "PATCH",
                self.pins_patch_path(),
                body={"pins": {str(self.fx.pin_ref): {"show_name": ""}}},
                expect=200,
            )
            check_eq(st, 200)
            u0 = (patched.get("updated") or [None])[0]
            check(u0 is not None, "no updated entry")
            check_eq(u0.get("show_name"), "")
            # display falls back to board name/number — not the cleared overlay marker
            check(u0.get("displayLabel") != "agent_clear_tmp", f"display still marker: {u0!r}")

            _, pin_body = request(self.base, "GET", self.pin_path(), expect=200)
            ov = pin_body.get("overlay") or {}
            check_eq(ov.get("show_name") or "", "")

            _, meta = request(self.base, "GET", self.screenshot_meta_path(), expect=200)
            match = None
            for p in meta.get("pins") or []:
                if str(p.get("number")) == self.fx.pin_number or str(p.get("key")) == str(
                    self.fx.pin_ref
                ):
                    match = p
                    break
            if match is None:
                for p in meta.get("pins") or []:
                    if str(p.get("id")) == self.fx.pin_id:
                        match = p
                        break
            check(match is not None, "pin missing from meta after clear")
            check_eq(match.get("overlayShowName") or "", "")
            check(match.get("displayLabel") != "agent_clear_tmp", f"display still marker: {match!r}")
        finally:
            self._restore_pin_show_name(prev)

    def t_pins_patch_unknown_key(self) -> None:
        st, err = request(
            self.base,
            "PATCH",
            self.pins_patch_path(),
            body={"pins": {"__no_such_pin_key_zz__": {"show_name": "x"}}},
            expect=400,
        )
        check_eq(st, 400)
        check_eq(error_code(err), "UNKNOWN_PIN_KEY")

    def t_pins_patch_empty(self) -> None:
        st, err = request(
            self.base,
            "PATCH",
            self.pins_patch_path(),
            body={"pins": {}},
            expect=400,
        )
        check_eq(st, 400)
        check_eq(error_code(err), "BAD_REQUEST")
        st, err = request(
            self.base,
            "PATCH",
            self.pins_patch_path(),
            body={},
            expect=400,
        )
        check_eq(st, 400)
        check_eq(error_code(err), "BAD_REQUEST")

    # ---- chip library ----
    def _unique_part_type(self) -> str:
        return f"TestChip_{int(time.time() * 1000)}"

    def chip_path(self, part_type: str) -> str:
        return f"/api/v1/chips/{enc(part_type)}"

    def chip_conds_path(self, part_type: str) -> str:
        return f"{self.chip_path(part_type)}/operating-conditions"

    def promote_path(self, ref: Optional[str] = None) -> str:
        return f"{self.conds_path(ref)}/promote"

    def _chip_part(self) -> str:
        return self.fx.multi_pin_part or self.fx.part

    def _patch_part_type(self, part_type: str, part: Optional[str] = None) -> None:
        p = part if part is not None else self._chip_part()
        path = f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(p)}"
        st, body = request(
            self.base,
            "PATCH",
            path,
            body={"part_type": part_type},
            expect=200,
        )
        check_eq(st, 200)
        check_eq(body.get("part_type"), part_type)
        check_eq(body.get("part"), p)

    def _clear_part_type(self, part: Optional[str] = None) -> None:
        p = part if part is not None else self._chip_part()
        path = f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(p)}"
        try:
            request(self.base, "PATCH", path, body={"part_type": ""}, expect=200)
        except Exception:
            pass

    def _delete_chip(self, part_type: str) -> None:
        try:
            request(self.base, "DELETE", self.chip_path(part_type), expect=(200, 204, 404))
        except Exception:
            pass

    def _delete_board_condition(self, cid: str, part: Optional[str] = None) -> None:
        p = part if part is not None else self._chip_part()
        path = (
            f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(p)}"
            f"/operating-conditions/{enc(cid)}"
        )
        try:
            request(self.base, "DELETE", path, expect=(200, 204, 404))
        except Exception:
            pass

    def _oc_body(
        self,
        *,
        cid: Optional[str] = None,
        name: str = "chip_smoke",
        inputs: Optional[list[str]] = None,
        outputs: Optional[list[str]] = None,
        enables: Optional[list[str]] = None,
        note: str = "",
    ) -> dict[str, Any]:
        body: dict[str, Any] = {
            "name": name,
            "inputs": inputs if inputs is not None else ["IN1"],
            "outputs": outputs if outputs is not None else ["OUT1"],
            "enables": enables if enables is not None else [],
            "note": note,
        }
        if cid is not None:
            body["id"] = cid
        return body

    def _effective_ids(self, body: Any) -> list[str]:
        ocs = body.get("operating_conditions")
        if ocs is None:
            ocs = body.get("effective")
        if not isinstance(ocs, list):
            return []
        return [str(c.get("id") or "") for c in ocs]

    def t_chip_put_get(self) -> None:
        """1. PUT /api/v1/chips/{type} with one condition → GET returns it."""
        pt = self._unique_part_type()
        try:
            put_body = {
                "note": "chip_put_get_smoke",
                "operating_conditions": [
                    self._oc_body(
                        cid="oc_chip_put",
                        name="lib_cond",
                        inputs=["A"],
                        outputs=["Y"],
                        note="from put",
                    )
                ],
            }
            st, created = request(
                self.base, "PUT", self.chip_path(pt), body=put_body, expect=200
            )
            check_eq(st, 200)
            check_eq(created.get("part_type"), pt)
            check_eq(created.get("note"), "chip_put_get_smoke")
            ocs = created.get("operating_conditions") or []
            check_eq(len(ocs), 1, f"put ocs={created!r}")
            check_eq(ocs[0].get("id"), "oc_chip_put")

            _, got = request(self.base, "GET", self.chip_path(pt), expect=200)
            check_eq(got.get("part_type"), pt)
            check_eq(got.get("note"), "chip_put_get_smoke")
            gocs = got.get("operating_conditions") or []
            check_eq(len(gocs), 1)
            check_eq(gocs[0].get("id"), "oc_chip_put")
            check_eq(gocs[0].get("name"), "lib_cond")
            check_eq(gocs[0].get("inputs"), ["A"])
            check_eq(gocs[0].get("outputs"), ["Y"])
        finally:
            self._delete_chip(pt)

    def t_chip_bind_part_type(self) -> None:
        """2. Bind part_type on a real multi-pin part via PATCH."""
        pt = self._unique_part_type()
        part = self._chip_part()
        try:
            self._patch_part_type(pt, part)
            path = f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}"
            _, summary = request(self.base, "GET", path, expect=200)
            pi = summary.get("partInfo") or {}
            check_eq(pi.get("part_type"), pt, f"partInfo after bind: {pi!r}")
        finally:
            self._clear_part_type(part)

    def t_chip_source_from_library(self) -> None:
        """3. GET part conditions → source=chip, effective matches library."""
        pt = self._unique_part_type()
        part = self._chip_part()
        board_cids: list[str] = []
        try:
            # Ensure board layer empty so chip is effective.
            _, listed = request(
                self.base,
                "GET",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                expect=200,
            )
            for c in listed.get("board") or []:
                cid = c.get("id")
                if cid:
                    board_cids.append(str(cid))
                    self._delete_board_condition(str(cid), part)

            put_body = {
                "note": "source_chip_smoke",
                "operating_conditions": [
                    self._oc_body(
                        cid="oc_src_chip",
                        name="from_lib",
                        inputs=["S1"],
                        outputs=["SO"],
                        note="chip-source",
                    )
                ],
            }
            request(self.base, "PUT", self.chip_path(pt), body=put_body, expect=200)
            self._patch_part_type(pt, part)

            _, body = request(
                self.base,
                "GET",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                expect=200,
            )
            check_eq(body.get("source"), "chip", f"source body={body!r}")
            check_eq(body.get("part_type"), pt)
            chip = body.get("chip") or []
            check_eq(len(chip), 1, f"chip layer={chip!r}")
            check_eq(chip[0].get("id"), "oc_src_chip")
            eff_ids = self._effective_ids(body)
            check_in("oc_src_chip", eff_ids, f"effective missing chip id: {body!r}")
            board = body.get("board") or []
            check_eq(len(board), 0, f"board should be empty for pure chip source: {board!r}")
        finally:
            self._clear_part_type(part)
            self._delete_chip(pt)

    def t_chip_board_override_isolation(self) -> None:
        """4. Board POST → source=board; chip GET unchanged."""
        pt = self._unique_part_type()
        part = self._chip_part()
        board_cid = "oc_board_ovr"
        try:
            put_body = {
                "note": "board_ovr_smoke",
                "operating_conditions": [
                    self._oc_body(
                        cid="oc_lib_keep",
                        name="lib_keep",
                        inputs=["LK"],
                        outputs=["LO"],
                        note="must-stay",
                    )
                ],
            }
            request(self.base, "PUT", self.chip_path(pt), body=put_body, expect=200)
            self._patch_part_type(pt, part)

            _, before_chip = request(self.base, "GET", self.chip_path(pt), expect=200)
            before_ocs = before_chip.get("operating_conditions") or []
            check_eq(len(before_ocs), 1)
            check_eq(before_ocs[0].get("id"), "oc_lib_keep")

            st, created = request(
                self.base,
                "POST",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                body=self._oc_body(
                    cid=board_cid,
                    name="board_only",
                    inputs=["B1"],
                    outputs=["BO"],
                    note="board-override",
                ),
                expect=201,
            )
            check_eq(st, 201)
            check_eq(created.get("id"), board_cid)
            self.created_condition_ids.append(board_cid)

            _, body = request(
                self.base,
                "GET",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                expect=200,
            )
            check_eq(body.get("source"), "board", f"source after board post: {body!r}")
            eff_ids = self._effective_ids(body)
            check_in(board_cid, eff_ids)
            check("oc_lib_keep" not in eff_ids, f"board whole-set should hide chip: {eff_ids}")
            chip_layer = body.get("chip") or []
            check_eq(len(chip_layer), 1)
            check_eq(chip_layer[0].get("id"), "oc_lib_keep")

            _, after_chip = request(self.base, "GET", self.chip_path(pt), expect=200)
            after_ocs = after_chip.get("operating_conditions") or []
            check_eq(len(after_ocs), 1, f"chip mutated by board write: {after_chip!r}")
            check_eq(after_ocs[0].get("id"), "oc_lib_keep")
            check_eq(after_ocs[0].get("note"), "must-stay")
        finally:
            self._delete_board_condition(board_cid, part)
            self.created_condition_ids = [
                x for x in self.created_condition_ids if x != board_cid
            ]
            self._clear_part_type(part)
            self._delete_chip(pt)

    def t_chip_promote_clear_board(self) -> None:
        """5. promote clearBoard true → source chip again; board empty."""
        pt = self._unique_part_type()
        part = self._chip_part()
        board_cid = "oc_promo_board"
        try:
            self._patch_part_type(pt, part)
            st, created = request(
                self.base,
                "POST",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                body=self._oc_body(
                    cid=board_cid,
                    name="to_promote",
                    inputs=["P1"],
                    outputs=["PO"],
                    note="promote-me",
                ),
                expect=201,
            )
            check_eq(created.get("id"), board_cid)
            self.created_condition_ids.append(board_cid)

            st, promoted = request(
                self.base,
                "POST",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions/promote",
                body={"clearBoard": True},
                expect=200,
            )
            check_eq(st, 200)
            check_eq(promoted.get("source"), "chip", f"promote resp: {promoted!r}")
            check_eq(promoted.get("part_type"), pt)
            board = promoted.get("board") or []
            check_eq(len(board), 0, f"board not cleared: {board!r}")
            chip = promoted.get("chip") or []
            chip_ids = [c.get("id") for c in chip]
            check_in(board_cid, chip_ids, f"promoted not in chip: {chip!r}")
            eff_ids = self._effective_ids(promoted)
            check_in(board_cid, eff_ids)

            _, chip_rec = request(self.base, "GET", self.chip_path(pt), expect=200)
            rec_ids = [c.get("id") for c in (chip_rec.get("operating_conditions") or [])]
            check_in(board_cid, rec_ids)

            # board layer no longer owns the condition id
            self.created_condition_ids = [
                x for x in self.created_condition_ids if x != board_cid
            ]
        finally:
            self._delete_board_condition(board_cid, part)
            self._clear_part_type(part)
            self._delete_chip(pt)

    def t_chip_scope_post_adds_library(self) -> None:
        """6. scope=chip POST adds to library."""
        pt = self._unique_part_type()
        part = self._chip_part()
        try:
            self._patch_part_type(pt, part)
            st, created = request(
                self.base,
                "POST",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}"
                f"/operating-conditions?scope=chip",
                body=self._oc_body(
                    cid="oc_scope_chip",
                    name="via_scope",
                    inputs=["SC"],
                    outputs=["SO"],
                    note="scope-post",
                ),
                expect=201,
            )
            check_eq(st, 201)
            check_eq(created.get("id"), "oc_scope_chip")
            check_eq(created.get("scope"), "chip")

            _, chip_rec = request(self.base, "GET", self.chip_path(pt), expect=200)
            ids = [c.get("id") for c in (chip_rec.get("operating_conditions") or [])]
            check_in("oc_scope_chip", ids, f"chip missing scope post: {chip_rec!r}")

            # Board override must remain empty for this id
            _, merged = request(
                self.base,
                "GET",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                expect=200,
            )
            board_ids = [c.get("id") for c in (merged.get("board") or [])]
            check("oc_scope_chip" not in board_ids, f"scope=chip leaked to board: {board_ids}")
        finally:
            self._clear_part_type(part)
            self._delete_chip(pt)

    def t_chip_board_post_does_not_mutate_chip(self) -> None:
        """7. Default board POST does not change chip file content."""
        pt = self._unique_part_type()
        part = self._chip_part()
        board_cid = "oc_no_mutate"
        try:
            put_body = {
                "note": "immutable_lib",
                "operating_conditions": [
                    self._oc_body(
                        cid="oc_lib_immut",
                        name="lib_immut",
                        inputs=["I"],
                        outputs=["O"],
                        note="before-board",
                    )
                ],
            }
            request(self.base, "PUT", self.chip_path(pt), body=put_body, expect=200)
            self._patch_part_type(pt, part)

            _, before = request(self.base, "GET", self.chip_path(pt), expect=200)

            st, created = request(
                self.base,
                "POST",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                body=self._oc_body(
                    cid=board_cid,
                    name="board_local",
                    inputs=["BX"],
                    outputs=["BY"],
                    note="must-not-touch-chip",
                ),
                expect=201,
            )
            check_eq(created.get("id"), board_cid)
            self.created_condition_ids.append(board_cid)

            _, after = request(self.base, "GET", self.chip_path(pt), expect=200)
            check_eq(
                after.get("operating_conditions"),
                before.get("operating_conditions"),
                f"chip mutated\nbefore={before!r}\nafter={after!r}",
            )
            check_eq(after.get("note"), before.get("note"))
            check_eq(after.get("part_type"), pt)
        finally:
            self._delete_board_condition(board_cid, part)
            self.created_condition_ids = [
                x for x in self.created_condition_ids if x != board_cid
            ]
            self._clear_part_type(part)
            self._delete_chip(pt)

    def t_chip_scope_requires_part_type(self) -> None:
        """8. scope=chip without part_type → 400 PART_TYPE_REQUIRED."""
        part = self._chip_part()
        # Ensure no part_type bound
        self._clear_part_type(part)
        st, err = request(
            self.base,
            "POST",
            f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}"
            f"/operating-conditions?scope=chip",
            body=self._oc_body(name="needs_type", inputs=["X"], outputs=["Y"]),
            expect=400,
        )
        check_eq(st, 400)
        check_eq(error_code(err), "PART_TYPE_REQUIRED")

    # ---- chip pin map ----
    def chip_pins_path(self, part_type: str) -> str:
        return f"{self.chip_path(part_type)}/pins"

    def chip_pin_path(self, part_type: str, pin_key: str) -> str:
        return f"{self.chip_pins_path(part_type)}/{enc(pin_key)}"

    def chip_resolve_path(self, part_type: str, label: str) -> str:
        return f"{self.chip_path(part_type)}/resolve?label={enc(label)}"

    def _sample_pin_table(self) -> list[dict[str, Any]]:
        return [
            {
                "id": "B3",
                "name": "VBAT",
                "aliases": ["BAT"],
                "dir": "power",
                "note": "battery",
            },
            {
                "id": "H4",
                "name": "UART_TXD",
                "aliases": [],
                "dir": "out",
                "note": "",
            },
        ]

    def t_chip_pins_put_get(self) -> None:
        """1. PUT pins on unique part_type → GET pins returns table."""
        pt = self._unique_part_type()
        try:
            pins = self._sample_pin_table()
            st, body = request(
                self.base,
                "PUT",
                self.chip_pins_path(pt),
                body={"pins": pins},
                expect=200,
            )
            check_eq(st, 200)
            got = body.get("pins") if isinstance(body, dict) and "pins" in body else body
            if isinstance(body, dict) and body.get("part_type"):
                check_eq(body.get("part_type"), pt)
            check(isinstance(got, list) and len(got) == 2, f"pins put resp={body!r}")
            by_id = {p.get("id"): p for p in got}
            check_in("B3", by_id)
            check_eq(by_id["B3"].get("name"), "VBAT")
            check_in("BAT", by_id["B3"].get("aliases") or [])
            check_eq(by_id["B3"].get("dir"), "power")
            check_in("H4", by_id)

            _, listed = request(self.base, "GET", self.chip_pins_path(pt), expect=200)
            listed_pins = (
                listed.get("pins") if isinstance(listed, dict) and "pins" in listed else listed
            )
            check(isinstance(listed_pins, list) and len(listed_pins) == 2, f"get pins={listed!r}")

            _, one = request(self.base, "GET", self.chip_pin_path(pt, "B3"), expect=200)
            check_eq(one.get("id"), "B3")
            check_eq(one.get("name"), "VBAT")
        finally:
            self._delete_chip(pt)

    def t_chip_pins_resolve_id_and_name(self) -> None:
        """2. resolve by id and by name → same pin."""
        pt = self._unique_part_type()
        try:
            request(
                self.base,
                "PUT",
                self.chip_pins_path(pt),
                body={"pins": self._sample_pin_table()},
                expect=200,
            )
            _, by_id = request(
                self.base, "GET", self.chip_resolve_path(pt, "B3"), expect=200
            )
            _, by_name = request(
                self.base, "GET", self.chip_resolve_path(pt, "VBAT"), expect=200
            )
            _, by_alias = request(
                self.base, "GET", self.chip_resolve_path(pt, "BAT"), expect=200
            )

            for label, body, matched in (
                ("B3", by_id, "id"),
                ("VBAT", by_name, "name"),
                ("BAT", by_alias, "alias"),
            ):
                check_eq(body.get("matched"), matched, f"label={label} body={body!r}")
                pin = body.get("pin") if isinstance(body.get("pin"), dict) else body
                # resolve may nest pin or flatten id/name on top level
                pid = pin.get("id") if isinstance(pin, dict) else body.get("id")
                pname = pin.get("name") if isinstance(pin, dict) else body.get("name")
                check_eq(pid, "B3", f"label={label} resolve={body!r}")
                check_eq(pname, "VBAT", f"label={label} resolve={body!r}")

            check_eq(
                (by_id.get("pin") or by_id).get("id")
                if isinstance(by_id.get("pin") or by_id, dict)
                else by_id.get("id"),
                (by_name.get("pin") or by_name).get("id")
                if isinstance(by_name.get("pin") or by_name, dict)
                else by_name.get("id"),
            )
        finally:
            self._delete_chip(pt)

    def t_chip_pins_key_conflict(self) -> None:
        """3. second pin with same name as existing id → 400 PIN_KEY_CONFLICT."""
        pt = self._unique_part_type()
        try:
            conflict = [
                {"id": "B3", "name": "VBAT", "aliases": [], "dir": "power", "note": ""},
                {"id": "Z1", "name": "B3", "aliases": [], "dir": "in", "note": ""},
            ]
            st, err = request(
                self.base,
                "PUT",
                self.chip_pins_path(pt),
                body={"pins": conflict},
                expect=400,
            )
            check_eq(st, 400)
            check_eq(error_code(err), "PIN_KEY_CONFLICT", f"err={err!r}")

            # also via chip PUT body pins
            st2, err2 = request(
                self.base,
                "PUT",
                self.chip_path(pt),
                body={"pins": conflict},
                expect=400,
            )
            check_eq(st2, 400)
            check_eq(error_code(err2), "PIN_KEY_CONFLICT", f"err={err2!r}")
        finally:
            self._delete_chip(pt)

    def t_chip_pins_board_enrichment(self) -> None:
        """4. bind part_type; GET conditions → chipPins + resolved hit for B3."""
        pt = self._unique_part_type()
        part = self._chip_part()
        try:
            put_body = {
                "note": "pin_enrich_smoke",
                "pins": self._sample_pin_table(),
                "operating_conditions": [
                    self._oc_body(
                        cid="oc_pin_resolve",
                        name="with_b3",
                        inputs=["B3"],
                        outputs=["E1"],
                        enables=["VBAT"],
                        note="resolve-me",
                    )
                ],
            }
            request(self.base, "PUT", self.chip_path(pt), body=put_body, expect=200)
            self._patch_part_type(pt, part)

            _, body = request(
                self.base,
                "GET",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                expect=200,
            )
            chip_pins = body.get("chipPins") or []
            check(isinstance(chip_pins, list) and len(chip_pins) >= 1, f"chipPins={body!r}")
            ids = {p.get("id") for p in chip_pins}
            check_in("B3", ids, f"chipPins missing B3: {chip_pins!r}")

            resolved = body.get("resolved") or {}
            check(isinstance(resolved, dict), f"resolved not map: {body!r}")
            oc_res = resolved.get("oc_pin_resolve") or {}
            check(oc_res, f"resolved missing oc_pin_resolve: {resolved!r}")

            inputs = oc_res.get("inputs") or []
            check(inputs, f"resolved.inputs empty: {oc_res!r}")
            r0 = inputs[0]
            check_eq(r0.get("label"), "B3")
            check_eq(r0.get("matched"), "id")
            check_eq(r0.get("id"), "B3")
            check_eq(r0.get("name"), "VBAT")

            enables = oc_res.get("enables") or []
            check(enables, f"resolved.enables empty: {oc_res!r}")
            e0 = enables[0]
            check_eq(e0.get("label"), "VBAT")
            check_eq(e0.get("matched"), "name")
            check_eq(e0.get("id"), "B3")
            check_eq(e0.get("name"), "VBAT")

            outputs = oc_res.get("outputs") or []
            check(outputs, f"resolved.outputs empty: {oc_res!r}")
            check_eq(outputs[0].get("label"), "E1")
            check_eq(outputs[0].get("matched"), "none")
        finally:
            self._clear_part_type(part)
            self._delete_chip(pt)

    def t_chip_pins_promote_preserves(self) -> None:
        """5. promote conditions does not clear pins."""
        pt = self._unique_part_type()
        part = self._chip_part()
        board_cid = "oc_promo_pins"
        try:
            request(
                self.base,
                "PUT",
                self.chip_path(pt),
                body={
                    "note": "promo_pins",
                    "pins": self._sample_pin_table(),
                    "operating_conditions": [],
                },
                expect=200,
            )
            self._patch_part_type(pt, part)

            st, created = request(
                self.base,
                "POST",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions",
                body=self._oc_body(
                    cid=board_cid,
                    name="to_promote_pins",
                    inputs=["B3"],
                    outputs=["PO"],
                    note="promo-pins",
                ),
                expect=201,
            )
            check_eq(created.get("id"), board_cid)
            self.created_condition_ids.append(board_cid)

            st, promoted = request(
                self.base,
                "POST",
                f"/api/v1/boards/{enc(self.fx.board_id)}/parts/{enc(part)}/operating-conditions/promote",
                body={"clearBoard": True},
                expect=200,
            )
            check_eq(st, 200)
            check_eq(promoted.get("source"), "chip", f"promote resp: {promoted!r}")

            # pins still present via dedicated GET and chip record
            _, pins_body = request(self.base, "GET", self.chip_pins_path(pt), expect=200)
            pins = (
                pins_body.get("pins")
                if isinstance(pins_body, dict) and "pins" in pins_body
                else pins_body
            )
            check(isinstance(pins, list) and len(pins) >= 2, f"pins cleared after promote: {pins_body!r}")
            pin_ids = {p.get("id") for p in pins}
            check_in("B3", pin_ids)
            check_in("H4", pin_ids)

            _, chip_rec = request(self.base, "GET", self.chip_path(pt), expect=200)
            rec_pins = chip_rec.get("pins") or []
            check(len(rec_pins) >= 2, f"chip record pins cleared: {chip_rec!r}")
            # promote response may also expose chipPins
            promo_pins = promoted.get("chipPins")
            if promo_pins is not None:
                check(len(promo_pins) >= 2, f"promote chipPins cleared: {promoted!r}")

            self.created_condition_ids = [
                x for x in self.created_condition_ids if x != board_cid
            ]
        finally:
            self._delete_board_condition(board_cid, part)
            self._clear_part_type(part)
            self._delete_chip(pt)

    def t_chip_pins_put_omit_preserves(self) -> None:
        """6. chip PUT omit pins preserves table."""
        pt = self._unique_part_type()
        try:
            request(
                self.base,
                "PUT",
                self.chip_path(pt),
                body={
                    "note": "omit_pins_v1",
                    "pins": self._sample_pin_table(),
                    "operating_conditions": [
                        self._oc_body(cid="oc_keep", name="keep", inputs=["A"], outputs=["Y"])
                    ],
                },
                expect=200,
            )

            st, updated = request(
                self.base,
                "PUT",
                self.chip_path(pt),
                body={
                    "note": "omit_pins_v2",
                    "operating_conditions": [
                        self._oc_body(cid="oc_keep", name="keep2", inputs=["A"], outputs=["Y"])
                    ],
                },
                expect=200,
            )
            check_eq(st, 200)
            check_eq(updated.get("note"), "omit_pins_v2")
            pins = updated.get("pins") or []
            check(len(pins) == 2, f"omit pins wiped table: {updated!r}")
            by_id = {p.get("id"): p for p in pins}
            check_in("B3", by_id)
            check_eq(by_id["B3"].get("name"), "VBAT")

            _, got = request(self.base, "GET", self.chip_path(pt), expect=200)
            check_eq(got.get("note"), "omit_pins_v2")
            gpins = got.get("pins") or []
            check_eq(len(gpins), 2, f"GET after omit lost pins: {got!r}")
            ocs = got.get("operating_conditions") or []
            check_eq(len(ocs), 1)
            check_eq(ocs[0].get("name"), "keep2")
        finally:
            self._delete_chip(pt)


    def cleanup(self) -> None:
        for cid in list(self.created_condition_ids):
            try:
                request(self.base, "DELETE", self.cond_path(cid), expect=(200, 204, 404))
            except Exception:
                pass
        self.created_condition_ids.clear()



    def nets_patch_path(self, ref: Optional[str] = None) -> str:
        r = ref if ref is not None else self.fx.board_id
        return f"/api/v1/boards/{enc(r)}/nets"

    def net_get_path(self, net_name: Optional[str] = None, ref: Optional[str] = None) -> str:
        r = ref if ref is not None else self.fx.board_id
        n = net_name if net_name is not None else self.fx.net_name
        return f"/api/v1/boards/{enc(r)}/nets/{enc(n)}"

    def t_net_get_and_patch_showname(self) -> None:
        check(self.fx.net_name, "fixture has no net_name")
        # GET baseline
        _, body = request(self.base, "GET", self.net_get_path(), expect=200)
        for k in ("boardId", "sourceName", "name", "displayName", "showname", "note", "isGround"):
            check_in(k, body, f"net get missing {k}")
        check_eq(body["name"], self.fx.net_name)
        prev = str(body.get("showname") or "")

        try:
            st, patched = request(
                self.base,
                "PATCH",
                self.nets_patch_path(),
                body={"nets": {self.fx.net_name: {"showname": "AGENT_NET_TEST"}}},
                expect=200,
            )
            check_eq(st, 200)
            check_in("updated", patched)
            check(len(patched["updated"]) == 1, "expected 1 updated")
            u = patched["updated"][0]
            check_eq(u.get("name"), self.fx.net_name)
            check_eq(u.get("showname"), "AGENT_NET_TEST")
            check_eq(u.get("displayName"), "AGENT_NET_TEST")

            _, again = request(self.base, "GET", self.net_get_path(), expect=200)
            check_eq(again.get("showname"), "AGENT_NET_TEST")
            check_eq(again.get("displayName"), "AGENT_NET_TEST")

            # clear
            st, cleared = request(
                self.base,
                "PATCH",
                self.nets_patch_path(),
                body={"nets": {self.fx.net_name: {"showname": ""}}},
                expect=200,
            )
            check_eq(cleared["updated"][0].get("showname"), "")
            check_eq(cleared["updated"][0].get("displayName"), self.fx.net_name)
            _, after = request(self.base, "GET", self.net_get_path(), expect=200)
            check_eq(after.get("showname") or "", "")
            check_eq(after.get("displayName"), self.fx.net_name)
        finally:
            # restore previous overlay showname
            request(
                self.base,
                "PATCH",
                self.nets_patch_path(),
                body={"nets": {self.fx.net_name: {"showname": prev}}},
                expect=200,
            )

    def t_net_patch_unknown(self) -> None:
        st, err = request(
            self.base,
            "PATCH",
            self.nets_patch_path(),
            body={"nets": {"__NO_SUCH_NET_ZZ__": {"showname": "x"}}},
            expect=400,
        )
        check_eq(error_code(err), "UNKNOWN_NET")

    def t_net_not_found(self) -> None:
        st, err = request(
            self.base,
            "GET",
            f"/api/v1/boards/{enc(self.fx.board_id)}/nets/{enc('__NO_SUCH_NET_ZZ__')}",
            expect=404,
        )
        check_eq(error_code(err), "NET_NOT_FOUND")



    def part_layout_path(self, query: str = "", ref: Optional[str] = None) -> str:
        r = ref if ref is not None else self.fx.board_id
        q = f"?{query}" if query else ""
        return f"/api/v1/boards/{enc(r)}/part-layout{q}"

    def t_part_layout_shape(self) -> None:
        _, body = request(self.base, "GET", self.part_layout_path(), expect=200)
        for k in ("boardId", "sourceName", "minPins", "partCount", "bounds", "parts"):
            check_in(k, body, f"part-layout missing {k}")
        check_eq(body["boardId"], self.fx.board_id)
        check_eq(int(body["minPins"]), 1)
        parts = body["parts"]
        check(isinstance(parts, list), "parts not list")
        check_eq(int(body["partCount"]), len(parts))
        check(len(parts) >= 1, "no parts")
        p0 = parts[0]
        for k in ("name", "center", "pinCount"):
            check_in(k, p0, f"part.{k}")
        check_in("x", p0["center"])
        check_in("y", p0["center"])
        check(int(p0["pinCount"]) >= 1, "default minPins=1 violated")
        # sorted by y,x,name
        for i in range(1, len(parts)):
            a, b = parts[i - 1], parts[i]
            ya, yb = float(a["center"]["y"]), float(b["center"]["y"])
            xa, xb = float(a["center"]["x"]), float(b["center"]["x"])
            if ya != yb:
                check(ya < yb, f"sort y failed {a['name']}->{b['name']}")
            elif xa != xb:
                check(xa < xb, f"sort x failed {a['name']}->{b['name']}")
            else:
                check(a["name"] <= b["name"], f"sort name failed")
        # minPins filter
        _, body2 = request(self.base, "GET", self.part_layout_path("minPins=2"), expect=200)
        check_eq(int(body2["minPins"]), 2)
        for p in body2["parts"]:
            check(int(p["pinCount"]) >= 2, f"minPins=2 leaked {p}")
        check(int(body2["partCount"]) <= int(body["partCount"]), "minPins=2 grew count")
        # bad minPins
        st, err = request(self.base, "GET", self.part_layout_path("minPins=-1"), expect=400)
        check_eq(error_code(err), "BAD_REQUEST")



    def t_match_parts_self(self) -> None:
        # Same board, rot0/all: near-perfect self match.
        body = {
            "a": {"ref": self.fx.board_id, "rot": 0, "region": "all"},
            "b": {"ref": self.fx.board_id, "rot": 0, "region": "all"},
            "split": "none",
            "minPins": 2,
            "maxDist": 1,
        }
        st, resp = request(self.base, "POST", "/api/v1/boards/match-parts", body=body, expect=200)
        for k in ("a", "b", "matchCount", "matches", "unmatchedA", "unmatchedB", "align", "minPins", "maxDist"):
            check_in(k, resp, f"match-parts missing {k}")
        check_eq(resp["align"], "region_centroid")
        check_eq(int(resp["minPins"]), 2)
        check_eq(float(resp["maxDist"]), 1.0)
        check_eq(int(resp["a"]["rot"]), 0)
        check_eq(resp["a"]["region"], "all")
        n = int(resp["a"]["partCount"])
        check(n >= 1, "no parts on side a")
        check_eq(int(resp["matchCount"]), len(resp["matches"]))
        # self-match with maxDist=1 should match most/all same-name centers
        check(int(resp["matchCount"]) >= max(1, n // 2), "self match too low")
        if resp["matches"]:
            m0 = resp["matches"][0]
            for k in ("partA", "partB", "pinCount", "dist", "canvasA", "canvasB"):
                check_in(k, m0, f"match.{k}")
            # many exact self pairs
            same = sum(1 for m in resp["matches"] if m["partA"] == m["partB"])
            check(same >= 1, "expected at least one same-name self match")

    def t_match_parts_bad_rot(self) -> None:
        body = {
            "a": {"ref": self.fx.board_id, "rot": 45, "region": "all"},
            "b": {"ref": self.fx.board_id, "rot": 0, "region": "all"},
        }
        st, err = request(self.base, "POST", "/api/v1/boards/match-parts", body=body, expect=400)
        check_eq(error_code(err), "BAD_REQUEST")

    def t_match_parts_missing_side(self) -> None:
        body = {"a": {"ref": self.fx.board_id, "rot": 0, "region": "all"}}
        st, err = request(self.base, "POST", "/api/v1/boards/match-parts", body=body, expect=400)
        check_eq(error_code(err), "BAD_REQUEST")


# ---------------------------------------------------------------------------
# Case registry
# ---------------------------------------------------------------------------

ALL_CASES: list[tuple[str, str, Callable[[Suite], None]]] = [
    # (group, name, method)
    ("meta", "health", lambda s: s.t_health()),
    ("meta", "list_boards", lambda s: s.t_list_boards()),
    ("pin", "resolve_by_board_id", lambda s: s.t_pin_resolve_by_id()),
    ("pin", "resolve_by_filename", lambda s: s.t_pin_resolve_by_filename()),
    ("pin", "resolve_by_path", lambda s: s.t_pin_resolve_by_path()),
    ("pin", "match_number_name_id", lambda s: s.t_pin_match_number_name_id()),
    ("pin", "pin_not_found", lambda s: s.t_pin_not_found()),
    ("pin", "part_not_found_on_pin", lambda s: s.t_part_not_found_on_pin()),
    ("pin", "board_not_found", lambda s: s.t_board_not_found()),
    ("part", "summary_shape", lambda s: s.t_part_summary()),
    ("part", "part_not_found", lambda s: s.t_part_not_found()),
    ("conditions", "list", lambda s: s.t_conditions_list_empty_or_array()),
    ("conditions", "crud_lifecycle", lambda s: s.t_conditions_crud_lifecycle()),
    ("conditions", "persistence_readback", lambda s: s.t_conditions_persistence()),
    ("conditions", "validation_limits", lambda s: s.t_conditions_validation()),
    ("conditions", "part_not_found", lambda s: s.t_conditions_part_not_found()),
    ("screenshot", "png_magic", lambda s: s.t_screenshot_png_magic()),
    ("screenshot", "meta_shape", lambda s: s.t_screenshot_meta_shape()),
    ("screenshot", "meta_matches_query", lambda s: s.t_screenshot_meta_matches_query()),
    ("pins", "patch_show_name", lambda s: s.t_pins_patch_show_name()),
    ("pins", "patch_clear", lambda s: s.t_pins_patch_clear()),
    ("pins", "patch_unknown_key", lambda s: s.t_pins_patch_unknown_key()),
    ("pins", "patch_empty", lambda s: s.t_pins_patch_empty()),
    ("chip", "put_get", lambda s: s.t_chip_put_get()),
    ("chip", "bind_part_type", lambda s: s.t_chip_bind_part_type()),
    ("chip", "source_from_library", lambda s: s.t_chip_source_from_library()),
    ("chip", "board_override_isolation", lambda s: s.t_chip_board_override_isolation()),
    ("chip", "promote_clear_board", lambda s: s.t_chip_promote_clear_board()),
    ("chip", "scope_post_adds_library", lambda s: s.t_chip_scope_post_adds_library()),
    ("chip", "board_post_no_chip_mutate", lambda s: s.t_chip_board_post_does_not_mutate_chip()),
    ("chip", "scope_requires_part_type", lambda s: s.t_chip_scope_requires_part_type()),
    ("chip_pins", "put_get", lambda s: s.t_chip_pins_put_get()),
    ("chip_pins", "resolve_id_and_name", lambda s: s.t_chip_pins_resolve_id_and_name()),
    ("chip_pins", "key_conflict", lambda s: s.t_chip_pins_key_conflict()),
    ("chip_pins", "board_enrichment", lambda s: s.t_chip_pins_board_enrichment()),
    ("chip_pins", "promote_preserves", lambda s: s.t_chip_pins_promote_preserves()),
    ("chip_pins", "put_omit_preserves", lambda s: s.t_chip_pins_put_omit_preserves()),
    ("grid", "pin_grid_shape", lambda s: s.t_pin_grid_shape()),
    ("nets", "get_patch_showname", lambda s: s.t_net_get_and_patch_showname()),
    ("nets", "patch_unknown", lambda s: s.t_net_patch_unknown()),
    ("nets", "not_found", lambda s: s.t_net_not_found()),
    ("layout", "part_layout_shape", lambda s: s.t_part_layout_shape()),
    ("match", "parts_self", lambda s: s.t_match_parts_self()),
    ("match", "parts_bad_rot", lambda s: s.t_match_parts_bad_rot()),
    ("match", "parts_missing_side", lambda s: s.t_match_parts_missing_side()),
]


# ---------------------------------------------------------------------------
# Server lifecycle
# ---------------------------------------------------------------------------

def find_server_binary(explicit: Optional[str], root: Path) -> Path:
    if explicit:
        p = Path(explicit)
        if not p.is_file():
            raise SystemExit(f"server binary not found: {p}")
        return p
    candidates = [
        root / "build-web" / "src" / "obv_server" / "Release" / "obv_server.exe",
        root / "build-web" / "src" / "obv_server" / "Debug" / "obv_server.exe",
        root / "build-web" / "src" / "obv_server" / "obv_server.exe",
        root / "build-web" / "src" / "obv_server" / "obv_server",
        root / "build" / "src" / "obv_server" / "Release" / "obv_server.exe",
        root / "build" / "src" / "obv_server" / "obv_server",
    ]
    for c in candidates:
        if c.is_file():
            return c
    raise SystemExit(
        "obv_server binary not found. Build with:\n"
        "  cmake --build build-web --config Release --target obv_server\n"
        "or pass --server PATH"
    )


def wait_health(base: str, timeout: float = 15.0) -> None:
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            st, body = request(base, "GET", "/api/v1/health", timeout=2.0)
            if st == 200:
                return
            last = f"status={st} body={body!r}"
        except Exception as e:
            last = str(e)
        time.sleep(0.2)
    raise SystemExit(f"server not healthy at {base} within {timeout}s ({last})")


def start_server(binary: Path, boards: Path, host: str, port: int) -> subprocess.Popen:
    cmd = [
        str(binary),
        "--host",
        host,
        "--port",
        str(port),
        "--boards",
        str(boards),
    ]
    print(f"Starting: {' '.join(cmd)}")
    # CREATE_NEW_PROCESS_GROUP on Windows so we can terminate cleanly
    kwargs: dict[str, Any] = {}
    if sys.platform == "win32":
        kwargs["creationflags"] = subprocess.CREATE_NEW_PROCESS_GROUP  # type: ignore[attr-defined]
    proc = subprocess.Popen(
        cmd,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        **kwargs,
    )
    return proc


def stop_server(proc: Optional[subprocess.Popen]) -> None:
    if not proc:
        return
    if proc.poll() is not None:
        return
    try:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
    except Exception:
        try:
            proc.kill()
        except Exception:
            pass


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main() -> int:
    root = Path(__file__).resolve().parents[1]
    ap = argparse.ArgumentParser(description="Agent pin/part API tests")
    ap.add_argument("--base", default=os.environ.get("OBV_TEST_BASE", ""), help="API base URL")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=int(os.environ.get("OBV_TEST_PORT", "18080")))
    ap.add_argument(
        "--boards",
        default=os.environ.get("OBV_TEST_BOARDS", str(root / "data" / "boards")),
        help="boardRoot library directory",
    )
    ap.add_argument(
        "--server",
        default=os.environ.get("OBV_TEST_SERVER", ""),
        help="path to obv_server binary",
    )
    ap.add_argument("--no-start", action="store_true", help="do not start server; require --base")

    ap.add_argument(
        "-k",
        "--filter",
        default="",
        help="comma-separated substrings to filter case names/groups (e.g. pin,conditions)",
    )
    ap.add_argument("--list", action="store_true", help="list cases and exit")
    args = ap.parse_args()

    if args.list:
        for group, name, _ in ALL_CASES:
            print(f"{group}.{name}")
        return 0

    filters = [x.strip() for x in args.filter.split(",") if x.strip()]

    proc: Optional[subprocess.Popen] = None
    base = args.base.rstrip("/")
    try:
        if args.no_start:
            if not base:
                raise SystemExit("--no-start requires --base http://host:port")
        else:
            if not base:
                base = f"http://{args.host}:{args.port}"
            # If already healthy, reuse; else start
            try:
                wait_health(base, timeout=1.0)
                print(f"Reusing healthy server at {base}")
            except SystemExit:
                binary = find_server_binary(args.server or None, root)
                boards = Path(args.boards)
                if not boards.is_dir():
                    raise SystemExit(f"boards dir not found: {boards}")
                proc = start_server(binary, boards, args.host, args.port)
                try:
                    wait_health(base, timeout=20.0)
                except SystemExit:
                    out = ""
                    if proc.stdout:
                        try:
                            out = proc.stdout.read().decode("utf-8", errors="replace")[:2000]
                        except Exception:
                            pass
                    stop_server(proc)
                    raise SystemExit(f"server failed to become healthy\n{out}")

        print(f"Base: {base}")
        fx = pick_fixture(base)
        print(
            f"Fixture: boardId={fx.board_id[:12]}… name={fx.board_name!r} "
            f"part={fx.part!r} pin={fx.pin_ref!r}"
        )
        print()

        suite = Suite(base=base, fx=fx)
        selected = []
        for group, name, fn in ALL_CASES:
            key = f"{group}.{name}"
            if filters and not any(f in key or f in group or f in name for f in filters):
                continue
            selected.append((key, fn))

        if not selected:
            print("No cases matched filter", file=sys.stderr)
            return 2

        print(f"Running {len(selected)} cases…")
        for key, fn in selected:
            suite.run(key, lambda f=fn: f(suite))

        suite.cleanup()

        passed = sum(1 for r in suite.results if r.ok)
        failed = [r for r in suite.results if not r.ok]
        print()
        print(f"Result: {passed}/{len(suite.results)} passed")
        if failed:
            print("Failures:")
            for r in failed:
                print(f"  - {r.name}: {r.detail}")
            return 1
        print("ALL PASS")
        return 0
    finally:
        stop_server(proc)


if __name__ == "__main__":
    sys.exit(main())
