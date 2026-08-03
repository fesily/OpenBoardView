import {
  useCallback,
  useEffect,
  useMemo,
  useRef,
  useState,
  type MouseEvent as ReactMouseEvent,
  type PointerEvent as ReactPointerEvent,
  type WheelEvent as ReactWheelEvent,
} from 'react';
import type { BoardDocument, OverlayAnnotation, OverlayDocument, Pin } from '../types/board';
import { collectCopperLayers, drawBoard, LAYER_COPPER } from './draw';
import { hitTestNearestPin } from './hitTest';
import { isNonSelectablePin, netByIdMap } from './netKinds';
import { netDisplayName } from './netDisplay';

import {
  buildNetPropagatedValues,
  localPinValue,
  PIN_VALUE_MODE_LABEL,
  PIN_VALUE_MODES,
  resolvePinValue,
  type PinValueMode,
} from './pinValues';
import {
  centerOnBounds,
  flipBoard,
  panByScreen,
  rotateView,
  screenToBoard,
  zoomAt,
  type ViewState,
} from './transform';

export interface BoardCanvasProps {
  board: BoardDocument;
  onSelectPin?: (pin: Pin | null) => void;
  selectedPinId?: string | null;
  /** Search / selection highlight sets (parts + pins + nets). */
  highlightPartNames?: ReadonlySet<string>;
  highlightPinIds?: ReadonlySet<string>;
  highlightNetIds?: ReadonlySet<number>;
  /** Board-space point to pan to (search result click). */
  focusPoint?: { x: number; y: number } | null;
  /** Changes on each explicit center request. */
  focusToken?: number;
  /** Freeform annotations drawn as markers. */
  annotations?: readonly OverlayAnnotation[];
  /** Overlay show_name overrides for pin/net labels (desktop ReloadPinInfos). */
  overlay?: OverlayDocument | null;
  /**
   * Right-click at board coordinates. Caller creates annotation (prompt/API).
   * `side` is 0=top, 1=bottom matching desktop SQLite annotations.
   */
  onContextAnnotate?: (pos: {
    x: number;
    y: number;
    side: number;
    pin: Pin | null;
  }) => void;
}

export default function BoardCanvas({
  board,
  onSelectPin,
  selectedPinId = null,
  highlightPartNames,
  highlightPinIds,
  highlightNetIds,
  focusPoint = null,
  focusToken = 0,
  annotations = [],
  overlay = null,
  onContextAnnotate,
}: BoardCanvasProps) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const viewRef = useRef<ViewState | null>(null);
  const [view, setView] = useState<ViewState | null>(null);
  const [size, setSize] = useState({ w: 640, h: 480 });
  const [layerMenuOpen, setLayerMenuOpen] = useState(false);
  const [valueMenuOpen, setValueMenuOpen] = useState(false);
  /** Overlay pin field shown under pad name; propagates across net. */
  const [pinValueMode, setPinValueMode] = useState<PinValueMode>('diode');
  /** Hover target for bottom status bar (includes GND/NC). */
  const [hoverPin, setHoverPin] = useState<Pin | null>(null);
  /**
   * null = all copper layers on.
   * Set = explicit filter; default on board open is top+bottom only.
   */
  const [enabledLayers, setEnabledLayers] = useState<Set<string> | null>(null);
  const dragRef = useRef<{
    active: boolean;
    moved: boolean;
    lastX: number;
    lastY: number;
    pointerId: number;
  } | null>(null);
  /** Active pointers for multi-touch pinch (id → local canvas coords). */
  const pointersRef = useRef<Map<number, { x: number; y: number }>>(new Map());
  const pinchRef = useRef<{
    lastDist: number;
    lastMidX: number;
    lastMidY: number;
  } | null>(null);

  const copperLayers = useMemo(() => collectCopperLayers(board), [board]);

  // Reset layer filter when board changes: default top + bottom (if present).
  useEffect(() => {
    const layers = collectCopperLayers(board);
    const preferred = layers.filter((l) => l === 'top' || l === 'bottom');
    // If board has neither top nor bottom copper, fall back to all layers.
    setEnabledLayers(preferred.length > 0 ? new Set(preferred) : null);
    setLayerMenuOpen(false);
    setValueMenuOpen(false);
    setHoverPin(null);
  }, [board, board.boardId]);

  const syncView = useCallback((next: ViewState) => {
    viewRef.current = next;
    setView(next);
  }, []);

  // Fit once per board (when size is known). Do NOT re-fit on canvas resize —
  // hover status bar content used to change wrap height → ResizeObserver →
  // size change → centerOnBounds reset (felt like hover resets the view).
  const fittedBoardIdRef = useRef<string | null>(null);
  useEffect(() => {
    if (size.w < 2 || size.h < 2) return;
    if (fittedBoardIdRef.current === board.boardId) return;
    fittedBoardIdRef.current = board.boardId;
    syncView(centerOnBounds(board.bounds, size.w, size.h));
  }, [board.boardId, board.bounds, size.w, size.h, syncView]);

  // Center view on search result click (preserve scale/rotation/side).
  useEffect(() => {
    if (!focusPoint || focusToken === 0) return;
    const v = viewRef.current;
    if (!v) return;
    syncView({ ...v, mx: focusPoint.x, my: focusPoint.y });
  }, [focusPoint, focusToken, syncView]);

  // Resize observer
  useEffect(() => {
    const el = wrapRef.current;
    if (!el) return;
    const ro = new ResizeObserver((entries) => {
      const cr = entries[0]?.contentRect;
      if (!cr) return;
      const w = Math.max(1, Math.floor(cr.width));
      const h = Math.max(1, Math.floor(cr.height));
      setSize((prev) => (prev.w === w && prev.h === h ? prev : { w, h }));
    });
    ro.observe(el);
    return () => ro.disconnect();
  }, []);

  // Native touch listeners (non-passive) — multi-touch pinch is more reliable
  // than React pointer events alone on some browsers/tablets.
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;

    const localFromTouch = (t: Touch) => {
      const r = canvas.getBoundingClientRect();
      return { x: t.clientX - r.left, y: t.clientY - r.top };
    };

    const onTouchStart = (ev: TouchEvent) => {
      // Prevent browser page zoom / scroll while interacting with board.
      if (ev.touches.length >= 1) ev.preventDefault();
      if (ev.touches.length >= 2) {
        const a = localFromTouch(ev.touches[0]);
        const b = localFromTouch(ev.touches[1]);
        pinchRef.current = {
          lastDist: Math.max(Math.hypot(b.x - a.x, b.y - a.y), 1),
          lastMidX: (a.x + b.x) / 2,
          lastMidY: (a.y + b.y) / 2,
        };
        // Abort single-finger drag so click-select won't fire after pinch.
        if (dragRef.current) {
          dragRef.current.moved = true;
          dragRef.current = null;
        }
        // Clear pointer map so React pointer path doesn't fight touch path.
        pointersRef.current.clear();
      }
    };

    const onTouchMove = (ev: TouchEvent) => {
      if (ev.touches.length >= 2) {
        ev.preventDefault();
        const v = viewRef.current;
        if (!v) return;
        const a = localFromTouch(ev.touches[0]);
        const b = localFromTouch(ev.touches[1]);
        const dist = Math.max(Math.hypot(b.x - a.x, b.y - a.y), 1);
        const midX = (a.x + b.x) / 2;
        const midY = (a.y + b.y) / 2;
        let pinch = pinchRef.current;
        if (!pinch) {
          pinchRef.current = { lastDist: dist, lastMidX: midX, lastMidY: midY };
          return;
        }
        const factor = dist / pinch.lastDist;
        let next = v;
        if (Number.isFinite(factor) && factor > 0 && Math.abs(factor - 1) > 0.002) {
          next = zoomAt(next, midX, midY, size.w, size.h, factor);
        }
        const dsx = midX - pinch.lastMidX;
        const dsy = midY - pinch.lastMidY;
        if (Math.hypot(dsx, dsy) > 0.5) {
          next = panByScreen(next, dsx, dsy, size.w, size.h);
        }
        pinch.lastDist = dist;
        pinch.lastMidX = midX;
        pinch.lastMidY = midY;
        if (next !== v) {
          viewRef.current = next;
          setView(next);
        }
      }
    };

    const onTouchEnd = (ev: TouchEvent) => {
      if (ev.touches.length < 2) {
        pinchRef.current = null;
      }
      if (ev.touches.length === 0) {
        pointersRef.current.clear();
      }
    };

    canvas.addEventListener('touchstart', onTouchStart, { passive: false });
    canvas.addEventListener('touchmove', onTouchMove, { passive: false });
    canvas.addEventListener('touchend', onTouchEnd, { passive: false });
    canvas.addEventListener('touchcancel', onTouchEnd, { passive: false });
    return () => {
      canvas.removeEventListener('touchstart', onTouchStart);
      canvas.removeEventListener('touchmove', onTouchMove);
      canvas.removeEventListener('touchend', onTouchEnd);
      canvas.removeEventListener('touchcancel', onTouchEnd);
    };
  }, [size.w, size.h]);


  // Draw
  useEffect(() => {
    const canvas = canvasRef.current;
    const v = view;
    if (!canvas || !v) return;
    const dpr = window.devicePixelRatio || 1;
    const cssW = size.w;
    const cssH = size.h;
    canvas.width = Math.max(1, Math.floor(cssW * dpr));
    canvas.height = Math.max(1, Math.floor(cssH * dpr));
    canvas.style.width = `${cssW}px`;
    canvas.style.height = `${cssH}px`;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    const partNames = new Set<string>(highlightPartNames ?? []);
    const pinIds = new Set<string>(highlightPinIds ?? []);
    let selectedNetId: number | null = null;
    if (selectedPinId) {
      pinIds.add(selectedPinId);
      const sel = board.pins.find((p) => p.id === selectedPinId);
      if (sel?.component) partNames.add(sel.component);
      // Desktop: same-net highlight for selectable nets only.
      // GND and true NC (pin.type not_connected): no net web / same-net expand.
      if (sel?.netId != null) {
        const nets = netByIdMap(board.nets);
        const net = nets.get(sel.netId);
        if (net && !isNonSelectablePin(sel, net)) {
          selectedNetId = sel.netId;
          for (const p of board.pins) {
            if (p.netId === selectedNetId) {
              pinIds.add(p.id);
              if (p.component) partNames.add(p.component);
            }
          }
        }
      }
    }
    drawBoard(
      ctx,
      board,
      v,
      cssW,
      cssH,
      {
        selectedPinId,
        selectedNetId,
        highlightNetIds: highlightNetIds?.size ? highlightNetIds : undefined,
        partNames: partNames.size ? partNames : undefined,
        pinIds: pinIds.size ? pinIds : undefined,
      },
      undefined,
      annotations,
      overlay,
      enabledLayers,
      pinValueMode,
    );
  }, [
    board,
    view,
    size.w,
    size.h,
    selectedPinId,
    highlightPartNames,
    highlightPinIds,
    highlightNetIds,
    annotations,
    overlay,
    enabledLayers,
    pinValueMode,
  ]);

  const localPoint = (ev: { clientX: number; clientY: number }) => {
    const canvas = canvasRef.current;
    if (!canvas) return { x: 0, y: 0 };
    const r = canvas.getBoundingClientRect();
    return { x: ev.clientX - r.left, y: ev.clientY - r.top };
  };

  const onWheel = (ev: ReactWheelEvent) => {
    ev.preventDefault();
    const v = viewRef.current;
    if (!v) return;
    const { x, y } = localPoint(ev);
    // Desktop: scale *= 2^zoom; wheel down → zoom out
    const steps = -ev.deltaY / 100;
    const factor = Math.pow(2, steps * 0.25);
    syncView(zoomAt(v, x, y, size.w, size.h, factor));
  };

  const clearPinch = () => {
    pinchRef.current = null;
  };

  const onPointerDown = (ev: ReactPointerEvent) => {
    // Primary button or touch/pen.
    if (ev.pointerType === 'mouse' && ev.button !== 0) return;
    // Touch: block default gestures; needed for some WebViews.
    if (ev.pointerType === 'touch') ev.preventDefault();
    const canvas = canvasRef.current;
    if (!canvas) return;
    const pt = localPoint(ev);
    // Only capture for single-finger / mouse. Capturing the first finger can
    // prevent the second finger's pointer events on some platforms.
    if (pointersRef.current.size === 0 && ev.pointerType !== 'touch') {
      try {
        canvas.setPointerCapture(ev.pointerId);
      } catch {
        // ignore capture failures
      }
    }
    pointersRef.current.set(ev.pointerId, pt);

    if (pointersRef.current.size >= 2) {
      // Enter / refresh pinch from first two active pointers.
      const pts = [...pointersRef.current.values()];
      const a = pts[0];
      const b = pts[1];
      const dist = Math.hypot(b.x - a.x, b.y - a.y);
      pinchRef.current = {
        lastDist: Math.max(dist, 1),
        lastMidX: (a.x + b.x) / 2,
        lastMidY: (a.y + b.y) / 2,
      };
      // Cancel single-finger drag/click when second finger lands.
      if (dragRef.current) {
        dragRef.current.moved = true;
        dragRef.current = null;
      }
      return;
    }

    // Single-finger / mouse drag start.
    dragRef.current = {
      active: true,
      moved: false,
      lastX: ev.clientX,
      lastY: ev.clientY,
      pointerId: ev.pointerId,
    };
  };

  const onPointerMove = (ev: ReactPointerEvent) => {
    const v = viewRef.current;
    if (!v) return;
    const pt = localPoint(ev);
    if (pointersRef.current.has(ev.pointerId)) {
      pointersRef.current.set(ev.pointerId, pt);
    }

    // Two-finger pinch + pan.
    if (pointersRef.current.size >= 2) {
      const pts = [...pointersRef.current.values()];
      const a = pts[0];
      const b = pts[1];
      const dist = Math.max(Math.hypot(b.x - a.x, b.y - a.y), 1);
      const midX = (a.x + b.x) / 2;
      const midY = (a.y + b.y) / 2;
      let pinch = pinchRef.current;
      if (!pinch) {
        pinch = { lastDist: dist, lastMidX: midX, lastMidY: midY };
        pinchRef.current = pinch;
        return;
      }
      const factor = dist / pinch.lastDist;
      let next = v;
      // Zoom about pinch midpoint.
      if (Number.isFinite(factor) && factor > 0 && Math.abs(factor - 1) > 0.001) {
        next = zoomAt(next, midX, midY, size.w, size.h, factor);
      }
      // Pan by midpoint movement (after zoom so look-at stays under fingers).
      const dsx = midX - pinch.lastMidX;
      const dsy = midY - pinch.lastMidY;
      if (Math.hypot(dsx, dsy) > 0.5) {
        next = panByScreen(next, dsx, dsy, size.w, size.h);
      }
      pinch.lastDist = dist;
      pinch.lastMidX = midX;
      pinch.lastMidY = midY;
      if (next !== v) syncView(next);
      return;
    }

    const drag = dragRef.current;
    if (drag?.active && drag.pointerId === ev.pointerId) {
      const dsx = ev.clientX - drag.lastX;
      const dsy = ev.clientY - drag.lastY;
      if (!drag.moved && Math.hypot(dsx, dsy) < 3) return;
      drag.moved = true;
      drag.lastX = ev.clientX;
      drag.lastY = ev.clientY;
      syncView(panByScreen(v, dsx, dsy, size.w, size.h));
      return;
    }

    // Mouse hover only (touch has no useful hover).
    if (ev.pointerType !== 'mouse') return;
    // Once a pin is selected, freeze status on selection — no hover thrashing.
    if (selectedPinId) {
      if (hoverPin) setHoverPin(null);
      return;
    }
    // Hover hit-test for status bar (allow GND/NC so their info still shows).
    const pin = hitTestNearestPin(board, v, pt.x, pt.y, size.w, size.h, {
      allowNonSelectable: true,
    });
    setHoverPin((prev) => (prev?.id === pin?.id ? prev : pin));
  };

  const endPointer = (ev: ReactPointerEvent) => {
    pointersRef.current.delete(ev.pointerId);
    if (pointersRef.current.size < 2) clearPinch();

    // If one finger remains after pinch, re-arm single-finger drag from it.
    if (pointersRef.current.size === 1) {
      const [id, p] = [...pointersRef.current.entries()][0];
      const canvas = canvasRef.current;
      if (canvas) {
        const r = canvas.getBoundingClientRect();
        dragRef.current = {
          active: true,
          moved: true, // don't treat as click after pinch
          lastX: r.left + p.x,
          lastY: r.top + p.y,
          pointerId: id,
        };
      }
      return;
    }

    const drag = dragRef.current;
    if (!drag || drag.pointerId !== ev.pointerId) {
      if (pointersRef.current.size === 0) dragRef.current = null;
      return;
    }
    const wasClick = drag.active && !drag.moved;
    dragRef.current = null;
    if (!wasClick) return;
    const v = viewRef.current;
    if (!v) return;
    const { x, y } = localPoint(ev);
    // Selection still skips GND/GROUND/UNCONNECTED.
    const pin = hitTestNearestPin(board, v, x, y, size.w, size.h);
    setHoverPin(null);
    onSelectPin?.(pin);
  };

  const onPointerLeave = () => {
    if (!selectedPinId) setHoverPin(null);
  };

  const onContextMenu = (ev: ReactMouseEvent<HTMLCanvasElement>) => {
    if (!onContextAnnotate) return;
    ev.preventDefault();
    const v = viewRef.current;
    if (!v) return;
    const { x, y } = localPoint(ev);
    const boardPt = screenToBoard(v, x, y, size.w, size.h);
    const pin = hitTestNearestPin(board, v, x, y, size.w, size.h);
    onContextAnnotate({
      x: boardPt.x,
      y: boardPt.y,
      side: v.side === 'bottom' ? 1 : 0,
      pin,
    });
  };

  const onRotate = (dir: 1 | -1) => {
    const v = viewRef.current;
    if (!v) return;
    syncView(rotateView(v, dir, size.w, size.h));
  };

  const onFlip = () => {
    const v = viewRef.current;
    if (!v) return;
    syncView(flipBoard(v));
  };

  const onReset = () => {
    syncView(centerOnBounds(board.bounds, size.w, size.h));
  };

  const layerIsOn = (layer: string) =>
    enabledLayers === null ? true : enabledLayers.has(layer);

  const toggleLayer = (layer: string) => {
    setEnabledLayers((prev) => {
      // First explicit toggle: start from all-on, then flip the clicked layer.
      const next = new Set(prev ?? copperLayers);
      if (next.has(layer)) next.delete(layer);
      else next.add(layer);
      return next;
    });
  };

  const setAllLayers = (on: boolean) => {
    if (on) setEnabledLayers(null);
    else setEnabledLayers(new Set());
  };

  const onCount =
    enabledLayers === null ? copperLayers.length : enabledLayers.size;

  const nets = useMemo(() => netByIdMap(board.nets), [board.nets]);

  const selectedPin = useMemo(
    () =>
      selectedPinId
        ? (board.pins.find((p) => p.id === selectedPinId) ?? null)
        : null,
    [board.pins, selectedPinId],
  );

  /** Status bar: selection freezes display; hover only when nothing selected. */
  const statusPin = selectedPin ?? hoverPin;

  const pinStatus = useMemo(() => {
    if (!statusPin) return null;
    const net =
      statusPin.netId != null ? (nets.get(statusPin.netId) ?? null) : null;
    const values: { mode: PinValueMode; label: string; value: string; source: 'local' | 'net' | '' }[] =
      [];
    for (const mode of PIN_VALUE_MODES) {
      const netMap = buildNetPropagatedValues(board, overlay, mode);
      const value = resolvePinValue(statusPin, overlay, mode, netMap);
      const local = localPinValue(statusPin, overlay, mode);
      values.push({
        mode,
        label: PIN_VALUE_MODE_LABEL[mode],
        value,
        source: value ? (local ? 'local' : 'net') : '',
      });
    }
    const name =
      (statusPin.show_name && statusPin.show_name.trim()) ||
      (statusPin.name && statusPin.name.trim()) ||
      statusPin.number ||
      statusPin.id;
    return {
      pin: statusPin,
      name,
      part: statusPin.component || '—',
      netName: net
        ? netDisplayName(net, overlay) || net.name
        : statusPin.netId != null
          ? `#${statusPin.netId}`
          : '—',
      netId: statusPin.netId,
      isGround: !!net?.isGround,
      values,
      viaHover: hoverPin != null,
    };
  }, [statusPin, hoverPin, board, overlay, nets]);

  return (
    <div className="board-canvas-root">
      <div className="board-toolbar">
        <button type="button" onClick={() => onRotate(-1)} title="Rotate CCW">
          ↺
        </button>
        <button type="button" onClick={onFlip} title="Flip side">
          Flip ({view?.side ?? 'top'})
        </button>
        <button type="button" onClick={() => onRotate(1)} title="Rotate CW">
          ↻
        </button>
        <button type="button" onClick={onReset} title="Reset view">
          Reset
        </button>

        {copperLayers.length > 0 && (
          <div className="layer-menu">
            <button
              type="button"
              className={layerMenuOpen ? 'layer-menu-btn open' : 'layer-menu-btn'}
              onClick={() => {
                setLayerMenuOpen((o) => !o);
                setValueMenuOpen(false);
              }}
              title="Select copper layers to render"
            >
              Layers ({onCount}/{copperLayers.length})
            </button>
            {layerMenuOpen && (
              <div className="layer-menu-panel" role="group" aria-label="Copper layers">
                <div className="layer-menu-actions">
                  <button type="button" onClick={() => setAllLayers(true)}>
                    All
                  </button>
                  <button type="button" onClick={() => setAllLayers(false)}>
                    None
                  </button>
                </div>
                <ul className="layer-menu-list">
                  {copperLayers.map((layer) => {
                    const on = layerIsOn(layer);
                    const swatch = LAYER_COPPER[layer] ?? '#8ba3c7';
                    return (
                      <li key={layer}>
                        <label className="layer-menu-item">
                          <input
                            type="checkbox"
                            checked={on}
                            onChange={() => toggleLayer(layer)}
                          />
                          <span
                            className="layer-swatch"
                            style={{ background: swatch }}
                            aria-hidden
                          />
                          <span className="layer-name">{layer}</span>
                        </label>
                      </li>
                    );
                  })}
                </ul>
              </div>
            )}
          </div>
        )}

        <div className="layer-menu">
          <button
            type="button"
            className={valueMenuOpen ? 'layer-menu-btn open' : 'layer-menu-btn'}
            onClick={() => {
              setValueMenuOpen((o) => !o);
              setLayerMenuOpen(false);
            }}
            title="Pin pad value field (propagates on net)"
          >
            Pin: {PIN_VALUE_MODE_LABEL[pinValueMode]}
          </button>
          {valueMenuOpen && (
            <div className="layer-menu-panel" role="menu" aria-label="Pin value mode">
              <ul className="layer-menu-list">
                {PIN_VALUE_MODES.map((mode) => (
                  <li key={mode}>
                    <label className="layer-menu-item">
                      <input
                        type="radio"
                        name="pin-value-mode"
                        checked={pinValueMode === mode}
                        onChange={() => {
                          setPinValueMode(mode);
                          setValueMenuOpen(false);
                        }}
                      />
                      <span className="layer-name">{PIN_VALUE_MODE_LABEL[mode]}</span>
                      <span className="muted mono" style={{ marginLeft: 'auto', fontSize: '0.72rem' }}>
                        {mode}
                      </span>
                    </label>
                  </li>
                ))}
              </ul>
            </div>
          )}
        </div>

        <span className="board-toolbar-meta muted">
          scale {view ? view.scale.toFixed(3) : '—'} · rot {view?.rotation ?? 0}
        </span>
      </div>
      <div className="board-canvas-wrap" ref={wrapRef}>
        <canvas
          ref={canvasRef}
          className="board-canvas"
          onWheel={onWheel}
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={endPointer}
          onPointerCancel={endPointer}
          onPointerLeave={onPointerLeave}
          onContextMenu={onContextMenu}
        />
      </div>

      <div className="board-pin-status" role="status" aria-live="polite">
        {!pinStatus ? (
          <span className="muted">Hover or select a pin for net / diode / voltage / ohm</span>
        ) : (
          <>
            <span className="pin-status-tag" title={pinStatus.viaHover ? 'Hover' : 'Selected'}>
              {pinStatus.viaHover ? 'Hover' : 'Sel'}
            </span>
            <span className="pin-status-part mono" title={pinStatus.pin.id}>
              {pinStatus.part}
              <span className="muted">.</span>
              {pinStatus.name}
            </span>
            <span className="pin-status-sep" aria-hidden>
              ·
            </span>
            <span
              className={
                'pin-status-net mono' + (pinStatus.isGround ? ' pin-status-gnd' : '')
              }
              title={
                pinStatus.netId != null
                  ? `netId ${pinStatus.netId}`
                  : 'no net'
              }
            >
              net {pinStatus.netName}
              {pinStatus.isGround ? ' (GND)' : ''}
            </span>
            {pinStatus.values.map((v) =>
              v.value ? (
                <span
                  key={v.mode}
                  className={
                    'pin-status-val' +
                    (v.mode === pinValueMode ? ' pin-status-val-active' : '')
                  }
                  title={
                    v.source === 'net'
                      ? `${v.label} via net propagation`
                      : `${v.label} local`
                  }
                >
                  <span className="pin-status-val-k">{v.mode}</span>
                  <span className="pin-status-val-v mono">{v.value}</span>
                  {v.source === 'net' && (
                    <span className="pin-status-val-src muted">net</span>
                  )}
                </span>
              ) : null,
            )}
            {!pinStatus.values.some((v) => v.value) && (
              <span className="muted">no diode/voltage/ohm</span>
            )}
          </>
        )}
      </div>
    </div>
  );
}
