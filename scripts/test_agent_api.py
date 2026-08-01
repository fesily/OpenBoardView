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
        net_name="",
        other_part=other,
        multi_pin_part=multi,
    )


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
        check_eq(body["row0"], "min_y")
        check_eq(body["col0"], "min_x")
        rows = int(body["rows"])
        cols = int(body["cols"])
        check(rows >= 1 and cols >= 1, f"rows/cols {rows}x{cols}")
        check_in(body["kind"], ("single", "row", "column", "grid", "sparse"))
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

    def cleanup(self) -> None:
        for cid in list(self.created_condition_ids):
            try:
                request(self.base, "DELETE", self.cond_path(cid), expect=(200, 204, 404))
            except Exception:
                pass
        self.created_condition_ids.clear()


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
    ("grid", "pin_grid_shape", lambda s: s.t_pin_grid_shape()),
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


def start_server(binary: Path, boards: Path, host: str, port: int, data: Path) -> subprocess.Popen:
    data.mkdir(parents=True, exist_ok=True)
    (data / "boards").mkdir(exist_ok=True)
    (data / "overlays").mkdir(exist_ok=True)
    (data / "config").mkdir(exist_ok=True)
    cmd = [
        str(binary),
        "--host",
        host,
        "--port",
        str(port),
        "--boards",
        str(boards),
        "--data",
        str(data),
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
    ap.add_argument("--data", default=str(root / "data"), help="--data root for auto-started server")
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
                proc = start_server(binary, boards, args.host, args.port, Path(args.data))
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
