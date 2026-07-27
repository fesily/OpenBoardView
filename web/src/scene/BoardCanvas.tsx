import {
  useCallback,
  useEffect,
  useRef,
  useState,
  type PointerEvent as ReactPointerEvent,
  type WheelEvent as ReactWheelEvent,
} from 'react';
import type { BoardDocument, Pin } from '../types/board';
import { drawBoard } from './draw';
import { hitTestNearestPin } from './hitTest';
import {
  centerOnBounds,
  flipBoard,
  panByScreen,
  rotateView,
  zoomAt,
  type ViewState,
} from './transform';

export interface BoardCanvasProps {
  board: BoardDocument;
  onSelectPin?: (pin: Pin | null) => void;
  selectedPinId?: string | null;
  /** Search / selection highlight sets (parts + pins). */
  highlightPartNames?: ReadonlySet<string>;
  highlightPinIds?: ReadonlySet<string>;
  /** Board-space point to pan to (search result click). */
  focusPoint?: { x: number; y: number } | null;
  /** Changes on each explicit center request. */
  focusToken?: number;
}

export default function BoardCanvas({
  board,
  onSelectPin,
  selectedPinId = null,
  highlightPartNames,
  highlightPinIds,
  focusPoint = null,
  focusToken = 0,
}: BoardCanvasProps) {
  const wrapRef = useRef<HTMLDivElement>(null);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const viewRef = useRef<ViewState | null>(null);
  const [view, setView] = useState<ViewState | null>(null);
  const [size, setSize] = useState({ w: 640, h: 480 });
  const dragRef = useRef<{
    active: boolean;
    moved: boolean;
    lastX: number;
    lastY: number;
    pointerId: number;
  } | null>(null);

  const syncView = useCallback((next: ViewState) => {
    viewRef.current = next;
    setView(next);
  }, []);

  // Fit view when board or canvas size changes (reset rotation/side)
  useEffect(() => {
    const v = centerOnBounds(board.bounds, size.w, size.h);
    syncView(v);
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
    if (selectedPinId) {
      pinIds.add(selectedPinId);
      for (const p of board.pins) {
        if (p.id === selectedPinId && p.component) partNames.add(p.component);
      }
    }
    drawBoard(ctx, board, v, cssW, cssH, {
      selectedPinId,
      partNames: partNames.size ? partNames : undefined,
      pinIds: pinIds.size ? pinIds : undefined,
    });
  }, [board, view, size.w, size.h, selectedPinId, highlightPartNames, highlightPinIds]);

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

  const onPointerDown = (ev: ReactPointerEvent) => {
    if (ev.button !== 0) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    canvas.setPointerCapture(ev.pointerId);
    dragRef.current = {
      active: true,
      moved: false,
      lastX: ev.clientX,
      lastY: ev.clientY,
      pointerId: ev.pointerId,
    };
  };

  const onPointerMove = (ev: ReactPointerEvent) => {
    const drag = dragRef.current;
    const v = viewRef.current;
    if (!drag?.active || !v) return;
    const dsx = ev.clientX - drag.lastX;
    const dsy = ev.clientY - drag.lastY;
    if (!drag.moved && Math.hypot(dsx, dsy) < 3) return;
    drag.moved = true;
    drag.lastX = ev.clientX;
    drag.lastY = ev.clientY;
    syncView(panByScreen(v, dsx, dsy, size.w, size.h));
  };

  const endDrag = (ev: ReactPointerEvent) => {
    const drag = dragRef.current;
    if (!drag || drag.pointerId !== ev.pointerId) return;
    const wasClick = drag.active && !drag.moved;
    dragRef.current = null;
    if (!wasClick) return;
    const v = viewRef.current;
    if (!v) return;
    const { x, y } = localPoint(ev);
    const pin = hitTestNearestPin(board, v, x, y, size.w, size.h);
    onSelectPin?.(pin);
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
          onPointerUp={endDrag}
          onPointerCancel={endDrag}
        />
      </div>
    </div>
  );
}
