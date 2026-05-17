/**
 * CellPopout — Independent draggable/resizable window showing one grid cell's
 * contents at larger scale.
 *
 * Opened when the user clicks a grid cell in GridOverlay.  Shows the clusters
 * inside that cell as labelled rectangles; each cluster is clickable for
 * labeling.  Dismiss with Esc or by clicking the close button.
 *
 * Props
 * -----
 * popoutData     {valid, row, col, cell_bounds, clusters}  — from censor API
 * onClose        () => void
 * onClusterClick (cluster_id) => void
 * labelColors    {[label]: [r,g,b]}  — same map as GridOverlay
 */

import { useRef, useEffect, useState, useCallback } from 'react';

/* ---------------------------------------------------------------------------
 * Color helpers (same table as GridOverlay; kept local to avoid coupling)
 * -------------------------------------------------------------------------*/
const DEFAULT_COLORS = {
  wall:      [120, 140, 200],
  duct:      [200, 160,  80],
  pipe:      [ 80, 180, 120],
  furniture: [200, 120, 160],
  ceiling:   [140, 200, 200],
  'grid line':[160, 160, 160],
  text:      [200, 200, 120],
  dimension: [180, 180, 100],
  equipment: [160, 100, 200],
};

function labelRgb(label, labelColors) {
  if (labelColors && labelColors[label]) return labelColors[label];
  const lc = label ? label.toLowerCase() : '';
  return DEFAULT_COLORS[lc] || [150, 150, 150];
}

/* ---------------------------------------------------------------------------
 * Canvas drawing for the popout
 * -------------------------------------------------------------------------*/
function drawPopout(ctx, canvas, clusters, cellBounds, zoom, pan, labelColors) {
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);

  const [bx0, by0, bx1, by1] = cellBounds;
  const cellW = bx1 - bx0;
  const cellH = by1 - by0;
  if (cellW <= 0 || cellH <= 0) return;

  /* Page-space → canvas-space transform.
   * Fit cell to canvas with padding, then apply user zoom/pan. */
  const PADDING = 24;
  const baseScale = Math.min(
    (width  - PADDING * 2) / cellW,
    (height - PADDING * 2) / cellH,
  );
  const scale = baseScale * zoom;
  const tx = (width  / 2) - (bx0 + cellW / 2) * scale + pan.x;
  const ty = (height / 2) - (by0 + cellH / 2) * scale + pan.y;

  const sx = x => x * scale + tx;
  const sy = y => y * scale + ty;

  /* Cell background */
  ctx.fillStyle = 'rgba(240,240,240,1)';
  ctx.fillRect(sx(bx0), sy(by0), cellW * scale, cellH * scale);

  /* Cell border */
  ctx.strokeStyle = 'rgba(60,130,255,0.8)';
  ctx.lineWidth = 2;
  ctx.strokeRect(sx(bx0) + 1, sy(by0) + 1, cellW * scale - 2, cellH * scale - 2);

  /* Clusters */
  for (const cluster of clusters) {
    const [cx0, cy0, cx1, cy1] = cluster.bounds;
    const cw = (cx1 - cx0) * scale;
    const ch = (cy1 - cy0) * scale;

    if (cluster.label) {
      const [r, g, b] = labelRgb(cluster.label, labelColors);
      ctx.fillStyle = `rgba(${r},${g},${b},0.25)`;
      ctx.fillRect(sx(cx0), sy(cy0), cw, ch);
      ctx.strokeStyle = `rgba(${r},${g},${b},0.9)`;
    } else {
      ctx.fillStyle = 'rgba(180,180,180,0.15)';
      ctx.fillRect(sx(cx0), sy(cy0), cw, ch);
      ctx.strokeStyle = 'rgba(100,100,100,0.6)';
    }

    ctx.lineWidth = cluster.is_user_labeled ? 2 : 1;
    ctx.strokeRect(sx(cx0) + 0.5, sy(cy0) + 0.5, cw - 1, ch - 1);

    /* Label text */
    if (cluster.label && cw > 20) {
      const [r, g, b] = labelRgb(cluster.label, labelColors);
      ctx.fillStyle = `rgb(${r},${g},${b})`;
      ctx.font = `${Math.max(9, Math.min(13, ch * 0.4))}px sans-serif`;
      ctx.textBaseline = 'top';
      ctx.fillText(cluster.label, sx(cx0) + 3, sy(cy0) + 2, cw - 6);
    }
  }

  return { scale, tx, ty };
}

/* ---------------------------------------------------------------------------
 * Drag hook for moving the popout window
 * -------------------------------------------------------------------------*/
function useDrag(initialPos) {
  const [pos, setPos]       = useState(initialPos);
  const dragStart           = useRef(null);

  const onMouseDown = useCallback((e) => {
    if (e.button !== 0) return;
    dragStart.current = { mx: e.clientX, my: e.clientY, px: pos.x, py: pos.y };
    e.preventDefault();
  }, [pos]);

  useEffect(() => {
    const onMove = (e) => {
      if (!dragStart.current) return;
      const dx = e.clientX - dragStart.current.mx;
      const dy = e.clientY - dragStart.current.my;
      setPos({ x: dragStart.current.px + dx, y: dragStart.current.py + dy });
    };
    const onUp = () => { dragStart.current = null; };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup',   onUp);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup',   onUp);
    };
  }, []);

  return { pos, onMouseDown };
}

/* ---------------------------------------------------------------------------
 * Component
 * -------------------------------------------------------------------------*/
export default function CellPopout({
  popoutData,
  onClose,
  onClusterClick,
  labelColors,
}) {
  if (!popoutData?.valid) return null;

  const canvasRef = useRef(null);
  const transformRef = useRef({ scale: 1, tx: 0, ty: 0 });
  const [zoom, setZoom] = useState(1);
  const [pan,  setPan]  = useState({ x: 0, y: 0 });
  const { pos, onMouseDown: onTitleDrag } = useDrag({ x: 80, y: 80 });

  const { row, col, cell_bounds, clusters } = popoutData;

  /* Redraw when data/zoom/pan changes. */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    const result = drawPopout(ctx, canvas, clusters, cell_bounds, zoom, pan, labelColors);
    if (result) transformRef.current = result;
  }, [clusters, cell_bounds, zoom, pan, labelColors]);

  /* Esc closes the popout. */
  useEffect(() => {
    const handler = (e) => {
      if (e.key === 'Escape') onClose?.();
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [onClose]);

  /* Scroll to zoom. */
  const handleWheel = useCallback((e) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.1 : 0.9;
    setZoom(z => Math.max(0.2, Math.min(10, z * factor)));
  }, []);

  /* Click on a cluster in the canvas. */
  const handleCanvasClick = useCallback((e) => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const rect   = canvas.getBoundingClientRect();
    const cx     = e.clientX - rect.left;
    const cy     = e.clientY - rect.top;
    const { scale, tx, ty } = transformRef.current;
    const px = (cx - tx) / scale;
    const py = (cy - ty) / scale;

    for (const cluster of clusters) {
      const [x0, y0, x1, y1] = cluster.bounds;
      if (px >= x0 && px <= x1 && py >= y0 && py <= y1) {
        onClusterClick?.(cluster.cluster_id);
        break;
      }
    }
  }, [clusters, onClusterClick]);

  /* Pan by mouse drag on canvas. */
  const panStart = useRef(null);

  const handleCanvasMouseDown = useCallback((e) => {
    if (e.button !== 0) return;
    panStart.current = { mx: e.clientX, my: e.clientY, px: pan.x, py: pan.y };
  }, [pan]);

  useEffect(() => {
    const onMove = (e) => {
      if (!panStart.current) return;
      setPan({
        x: panStart.current.px + (e.clientX - panStart.current.mx),
        y: panStart.current.py + (e.clientY - panStart.current.my),
      });
    };
    const onUp = (e) => {
      if (panStart.current) {
        const dx = Math.abs(e.clientX - panStart.current.mx);
        const dy = Math.abs(e.clientY - panStart.current.my);
        /* Treat as click (not pan) when movement < 4 px. */
        if (dx < 4 && dy < 4) handleCanvasClick(e);
        panStart.current = null;
      }
    };
    window.addEventListener('mousemove', onMove);
    window.addEventListener('mouseup',   onUp);
    return () => {
      window.removeEventListener('mousemove', onMove);
      window.removeEventListener('mouseup',   onUp);
    };
  }, [handleCanvasClick]);

  return (
    <div
      style={{
        position:     'fixed',
        left:         pos.x,
        top:          pos.y,
        width:        400,
        height:       340,
        background:   '#fff',
        border:       '1px solid rgba(0,0,0,0.18)',
        borderRadius: 6,
        boxShadow:    '0 8px 32px rgba(0,0,0,0.18)',
        display:      'flex',
        flexDirection:'column',
        zIndex:       9999,
        userSelect:   'none',
        resize:       'both',
        overflow:     'hidden',
        minWidth:     200,
        minHeight:    160,
      }}
    >
      {/* Title bar — drag handle */}
      <div
        onMouseDown={onTitleDrag}
        style={{
          padding:    '6px 10px',
          background: 'rgba(60,130,255,0.12)',
          cursor:     'move',
          display:    'flex',
          alignItems: 'center',
          justifyContent: 'space-between',
          borderBottom: '1px solid rgba(0,0,0,0.08)',
          flexShrink: 0,
        }}
      >
        <span style={{ fontSize: 12, color: '#444', fontFamily: 'monospace' }}>
          Cell ({row},{col}) — {clusters.length} cluster{clusters.length !== 1 ? 's' : ''}
        </span>
        <button
          onClick={onClose}
          style={{
            background: 'none',
            border:     'none',
            cursor:     'pointer',
            fontSize:   14,
            color:      '#888',
            padding:    '0 2px',
            lineHeight: 1,
          }}
          aria-label="Close"
        >
          ✕
        </button>
      </div>

      {/* Canvas */}
      <canvas
        ref={canvasRef}
        width={400}
        height={300}
        onWheel={handleWheel}
        onMouseDown={handleCanvasMouseDown}
        style={{
          flex:   1,
          cursor: 'grab',
          width:  '100%',
          height: '100%',
          display:'block',
        }}
      />

      {/* Footer hint */}
      <div style={{
        fontSize:   10,
        color:      '#aaa',
        padding:    '2px 8px',
        borderTop:  '1px solid rgba(0,0,0,0.06)',
        flexShrink: 0,
        fontFamily: 'monospace',
      }}>
        Scroll: zoom · Drag: pan · Click cluster: label · Esc: close
      </div>
    </div>
  );
}
