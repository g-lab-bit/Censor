/**
 * CellPopout — Independent draggable/resizable window showing one grid cell's
 * contents at larger scale.
 *
 * Phase 2 debug features (ce-gur):
 *   - Cluster hover  → bright boundary highlight + element count
 *   - Cluster click  → feature vector overlay (12 values drawn on canvas)
 *   - Shift+click    → label flow (passes through to onClusterClick)
 *   - [bands] button → toggle stroke-weight-band coloring
 *
 * Cluster data shape — Phase 2 additions (all optional/nullable):
 *   cluster.weight_band         'thin' | 'medium' | 'heavy' | null
 *   cluster.element_count       number | null
 *   cluster.feature_vector      number[12] | null
 *   cluster.stroke_weight_mean  number | null
 *
 * Props
 * -----
 * popoutData     {valid, row, col, cell_bounds, clusters}  — from censor API
 * onClose        () => void
 * onClusterClick (cluster_id) => void   — called only on Shift+click (label flow)
 * labelColors    {[label]: [r,g,b]}
 */

import { useRef, useEffect, useState, useCallback } from 'react';

/* ---------------------------------------------------------------------------
 * Color tables
 * -------------------------------------------------------------------------*/
const DEFAULT_COLORS = {
  wall:       [120, 140, 200],
  duct:       [200, 160,  80],
  pipe:       [ 80, 180, 120],
  furniture:  [200, 120, 160],
  ceiling:    [140, 200, 200],
  'grid line':[160, 160, 160],
  text:       [200, 200, 120],
  dimension:  [180, 180, 100],
  equipment:  [160, 100, 200],
};

// Matches SPEC-censor-core.md §2 (thin/medium/heavy from histogram bands)
const WEIGHT_BAND_COLORS = {
  thin:   [ 80, 180, 220],  // sky blue  — annotations/text
  medium: [220, 160,  80],  // amber     — MEP/partitions
  heavy:  [ 80, 100, 200],  // indigo    — walls/structure
};

// Feature names matching SPEC-censor-core.md §4 (12 features, order-sensitive)
const FEATURE_NAMES = [
  'stroke_len', 'orient',    'wt_mean',  'wt_band',
  'loops',      'loop_area', 'adjacency','fill',
  'aspect',     'bbox_area', 'elements', 'par_offset',
];

function labelRgb(label, labelColors) {
  if (labelColors && labelColors[label]) return labelColors[label];
  const lc = label ? label.toLowerCase() : '';
  return DEFAULT_COLORS[lc] || [150, 150, 150];
}

function weightBandRgb(band) {
  return WEIGHT_BAND_COLORS[band] || [150, 150, 150];
}

/* ---------------------------------------------------------------------------
 * Feature vector text overlay — drawn in screen space on the popout canvas.
 * Shows 12 feature values with their names next to the selected cluster.
 * -------------------------------------------------------------------------*/
function drawFeatureVector(ctx, canvas, cluster, sx, sy) {
  const fv = cluster.feature_vector;
  if (!fv || fv.length === 0) return;

  const [cx0, cy0, cx1, cy1] = cluster.bounds;

  const LINE_H = 13;
  const PAD    = 5;
  const count  = Math.min(fv.length, FEATURE_NAMES.length);
  const BOX_W  = 148;
  const BOX_H  = (count + 2) * LINE_H + PAD * 2;  // +2: header row + separator

  // Position right of cluster; fall back to left if near canvas right edge
  let bx = sx(cx1) + 8;
  let by = sy(cy0);
  if (bx + BOX_W > canvas.width  - 4) bx = Math.max(4, sx(cx0) - BOX_W - 8);
  if (by + BOX_H > canvas.height - 4) by = canvas.height - BOX_H - 4;
  if (by < 4) by = 4;

  // Background panel
  ctx.fillStyle = 'rgba(12, 14, 26, 0.90)';
  ctx.fillRect(bx, by, BOX_W, BOX_H);
  ctx.strokeStyle = 'rgba(80, 160, 255, 0.55)';
  ctx.lineWidth   = 1;
  ctx.strokeRect(bx + 0.5, by + 0.5, BOX_W - 1, BOX_H - 1);

  ctx.font         = '10px monospace';
  ctx.textBaseline = 'top';

  let row = 0;
  const rowY = () => by + PAD + row * LINE_H;

  // Header: element count + weight band
  ctx.textAlign = 'left';
  ctx.fillStyle = 'rgba(100, 200, 255, 1)';
  const nel  = cluster.element_count ?? '?';
  const band = cluster.weight_band ?? '?';
  ctx.fillText(`${nel} el · ${band}`, bx + PAD, rowY());
  row++;

  // Thin separator line
  ctx.fillStyle = 'rgba(80, 160, 255, 0.30)';
  ctx.fillRect(bx + PAD, rowY() - 1, BOX_W - PAD * 2, 1);
  row++;

  // Feature rows: name left, value right
  for (let i = 0; i < count; i++, row++) {
    const name   = FEATURE_NAMES[i];
    const raw    = fv[i];
    const valStr = raw == null        ? '—'
                 : Number.isInteger(raw) ? String(raw)
                 : raw.toFixed(3);

    ctx.textAlign = 'left';
    ctx.fillStyle = 'rgba(120, 180, 255, 0.85)';
    ctx.fillText(name, bx + PAD, rowY());

    ctx.textAlign = 'right';
    ctx.fillStyle = 'rgba(255, 240, 160, 0.95)';
    ctx.fillText(valStr, bx + BOX_W - PAD, rowY());
  }

  // Reset alignment so callers aren't affected
  ctx.textAlign = 'left';
}

/* ---------------------------------------------------------------------------
 * Canvas drawing
 * -------------------------------------------------------------------------*/
function drawPopout(ctx, canvas, clusters, cellBounds, zoom, pan, labelColors,
                    hoveredId, selectedId, showWeightBands) {
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);

  const [bx0, by0, bx1, by1] = cellBounds;
  const cellW = bx1 - bx0;
  const cellH = by1 - by0;
  if (cellW <= 0 || cellH <= 0) return;

  const PADDING   = 24;
  const baseScale = Math.min(
    (width  - PADDING * 2) / cellW,
    (height - PADDING * 2) / cellH,
  );
  const scale = baseScale * zoom;
  const tx = (width  / 2) - (bx0 + cellW / 2) * scale + pan.x;
  const ty = (height / 2) - (by0 + cellH / 2) * scale + pan.y;

  const sx = x => x * scale + tx;
  const sy = y => y * scale + ty;

  // Cell background
  ctx.fillStyle = 'rgba(240,240,240,1)';
  ctx.fillRect(sx(bx0), sy(by0), cellW * scale, cellH * scale);

  // Cell border
  ctx.strokeStyle = 'rgba(60,130,255,0.8)';
  ctx.lineWidth   = 2;
  ctx.strokeRect(sx(bx0) + 1, sy(by0) + 1, cellW * scale - 2, cellH * scale - 2);

  // Pass 1: cluster fills
  for (const cluster of clusters) {
    const [cx0, cy0, cx1, cy1] = cluster.bounds;
    const cw = (cx1 - cx0) * scale;
    const ch = (cy1 - cy0) * scale;

    let fillRgb = null;
    if (showWeightBands && cluster.weight_band) {
      fillRgb = weightBandRgb(cluster.weight_band);
    } else if (cluster.label) {
      fillRgb = labelRgb(cluster.label, labelColors);
    }

    if (fillRgb) {
      const [r, g, b] = fillRgb;
      ctx.fillStyle = `rgba(${r},${g},${b},0.22)`;
    } else {
      ctx.fillStyle = 'rgba(180,180,180,0.15)';
    }
    ctx.fillRect(sx(cx0), sy(cy0), cw, ch);
  }

  // Pass 2: cluster borders, labels, hover/selection highlights
  for (const cluster of clusters) {
    const [cx0, cy0, cx1, cy1] = cluster.bounds;
    const cw = (cx1 - cx0) * scale;
    const ch = (cy1 - cy0) * scale;

    const isSelected = cluster.cluster_id === selectedId;
    const isHovered  = !isSelected && cluster.cluster_id === hoveredId;

    // Border style: selection > hover > label/band > default
    if (isSelected) {
      ctx.strokeStyle = 'rgba(255, 100, 20, 1.0)';
      ctx.lineWidth   = 3;
    } else if (isHovered) {
      ctx.strokeStyle = 'rgba(40, 200, 255, 1.0)';
      ctx.lineWidth   = 2;
    } else {
      let strokeRgb = null;
      if (showWeightBands && cluster.weight_band) {
        strokeRgb = weightBandRgb(cluster.weight_band);
      } else if (cluster.label) {
        strokeRgb = labelRgb(cluster.label, labelColors);
      }
      if (strokeRgb) {
        const [r, g, b] = strokeRgb;
        ctx.strokeStyle = `rgba(${r},${g},${b},0.9)`;
        ctx.lineWidth   = cluster.is_user_labeled ? 2 : 1;
      } else {
        ctx.strokeStyle = 'rgba(100,100,100,0.6)';
        ctx.lineWidth   = 1;
      }
    }
    ctx.strokeRect(sx(cx0) + 0.5, sy(cy0) + 0.5, cw - 1, ch - 1);

    // Label text (suppressed in weight-band mode)
    if (!showWeightBands && cluster.label && cw > 20) {
      const [r, g, b] = labelRgb(cluster.label, labelColors);
      ctx.fillStyle    = `rgb(${r},${g},${b})`;
      ctx.font         = `${Math.max(9, Math.min(13, ch * 0.4))}px sans-serif`;
      ctx.textBaseline = 'top';
      ctx.textAlign    = 'left';
      ctx.fillText(cluster.label, sx(cx0) + 3, sy(cy0) + 2, cw - 6);
    }

    // Element count tooltip on hover
    if (isHovered && cluster.element_count != null) {
      ctx.fillStyle    = 'rgba(0,0,0,0.70)';
      ctx.font         = '10px monospace';
      ctx.textBaseline = 'bottom';
      ctx.textAlign    = 'left';
      ctx.fillText(`${cluster.element_count}el`, sx(cx0) + 2, sy(cy1) - 1);
    }
  }

  // Feature vector overlay for selected cluster (drawn last — on top)
  if (selectedId) {
    const sel = clusters.find(c => c.cluster_id === selectedId);
    if (sel) drawFeatureVector(ctx, canvas, sel, sx, sy);
  }

  return { scale, tx, ty };
}

/* ---------------------------------------------------------------------------
 * Drag hook for moving the popout window
 * -------------------------------------------------------------------------*/
function useDrag(initialPos) {
  const [pos, setPos] = useState(initialPos);
  const dragStart     = useRef(null);

  const onMouseDown = useCallback((e) => {
    if (e.button !== 0) return;
    dragStart.current = { mx: e.clientX, my: e.clientY, px: pos.x, py: pos.y };
    e.preventDefault();
  }, [pos]);

  useEffect(() => {
    const onMove = (e) => {
      if (!dragStart.current) return;
      setPos({
        x: dragStart.current.px + (e.clientX - dragStart.current.mx),
        y: dragStart.current.py + (e.clientY - dragStart.current.my),
      });
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
  const canvasRef    = useRef(null);
  const transformRef = useRef({ scale: 1, tx: 0, ty: 0 });
  const [zoom,           setZoom]           = useState(1);
  const [pan,            setPan]            = useState({ x: 0, y: 0 });
  const [hoveredId,      setHoveredId]      = useState(null);
  const [selectedId,     setSelectedId]     = useState(null);
  const [showWeightBands,setShowWeightBands]= useState(false);
  const { pos, onMouseDown: onTitleDrag }   = useDrag({ x: 80, y: 80 });

  const valid      = popoutData?.valid ?? false;
  const clusters   = popoutData?.clusters   ?? [];
  const cellBounds = popoutData?.cell_bounds ?? [0, 0, 1, 1];
  const row        = popoutData?.row ?? 0;
  const col        = popoutData?.col ?? 0;

  // Redraw when anything affecting the canvas changes
  useEffect(() => {
    if (!valid) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx    = canvas.getContext('2d');
    const result = drawPopout(
      ctx, canvas, clusters, cellBounds, zoom, pan,
      labelColors, hoveredId, selectedId, showWeightBands,
    );
    if (result) transformRef.current = result;
  }, [valid, clusters, cellBounds, zoom, pan, labelColors,
      hoveredId, selectedId, showWeightBands]);

  // Esc closes the popout
  useEffect(() => {
    const handler = (e) => { if (e.key === 'Escape') onClose?.(); };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [onClose]);

  // Clear hover/selection when the cell changes
  useEffect(() => {
    setSelectedId(null);
    setHoveredId(null);
  }, [popoutData]);

  // Scroll to zoom
  const handleWheel = useCallback((e) => {
    e.preventDefault();
    const factor = e.deltaY < 0 ? 1.1 : 0.9;
    setZoom(z => Math.max(0.2, Math.min(10, z * factor)));
  }, []);

  // Hit-test: return the cluster under canvas-event coordinates, or null
  const hitCluster = useCallback((e) => {
    const canvas = canvasRef.current;
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const cx   = e.clientX - rect.left;
    const cy   = e.clientY - rect.top;
    const { scale, tx, ty } = transformRef.current;
    const px = (cx - tx) / scale;
    const py = (cy - ty) / scale;
    for (const c of clusters) {
      const [x0, y0, x1, y1] = c.bounds;
      if (px >= x0 && px <= x1 && py >= y0 && py <= y1) return c;
    }
    return null;
  }, [clusters]);

  // Hover — updates highlighted cluster border and element count tooltip
  const handleCanvasHover = useCallback((e) => {
    const c = hitCluster(e);
    setHoveredId(c ? c.cluster_id : null);
  }, [hitCluster]);

  const handleCanvasLeave = useCallback(() => {
    setHoveredId(null);
  }, []);

  // Click: plain → toggle feature-vector inspect; Shift+click → label flow
  const handleCanvasClick = useCallback((e) => {
    const c = hitCluster(e);
    if (c) {
      setSelectedId(prev => prev === c.cluster_id ? null : c.cluster_id);
      if (e.shiftKey) onClusterClick?.(c.cluster_id);
    } else {
      setSelectedId(null);
    }
  }, [hitCluster, onClusterClick]);

  // Pan by dragging on the canvas
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

  if (!valid) return null;

  return (
    <div
      style={{
        position:      'fixed',
        left:          pos.x,
        top:           pos.y,
        width:         400,
        height:        340,
        background:    '#fff',
        border:        '1px solid rgba(0,0,0,0.18)',
        borderRadius:  6,
        boxShadow:     '0 8px 32px rgba(0,0,0,0.18)',
        display:       'flex',
        flexDirection: 'column',
        zIndex:        9999,
        userSelect:    'none',
        resize:        'both',
        overflow:      'hidden',
        minWidth:      200,
        minHeight:     160,
      }}
    >
      {/* Title bar — drag handle */}
      <div
        onMouseDown={onTitleDrag}
        style={{
          padding:        '6px 10px',
          background:     'rgba(60,130,255,0.12)',
          cursor:         'move',
          display:        'flex',
          alignItems:     'center',
          justifyContent: 'space-between',
          borderBottom:   '1px solid rgba(0,0,0,0.08)',
          flexShrink:     0,
          gap:            6,
        }}
      >
        <span style={{ fontSize: 12, color: '#444', fontFamily: 'monospace' }}>
          Cell ({row},{col}) — {clusters.length} cluster{clusters.length !== 1 ? 's' : ''}
        </span>
        <div style={{ display: 'flex', gap: 4, alignItems: 'center' }}>
          {/* Stroke weight band toggle */}
          <button
            onClick={(e) => { e.stopPropagation(); setShowWeightBands(b => !b); }}
            title="Toggle stroke weight band coloring (thin / medium / heavy)"
            style={{
              background:   showWeightBands ? 'rgba(60,130,255,0.22)' : 'rgba(0,0,0,0.05)',
              border:       `1px solid ${showWeightBands ? 'rgba(60,130,255,0.55)' : 'rgba(0,0,0,0.14)'}`,
              borderRadius: 3,
              cursor:       'pointer',
              fontSize:     10,
              color:        showWeightBands ? '#1a6fcc' : '#777',
              padding:      '1px 6px',
              fontFamily:   'monospace',
              lineHeight:   '16px',
            }}
          >
            bands
          </button>
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
      </div>

      {/* Canvas */}
      <canvas
        ref={canvasRef}
        width={400}
        height={300}
        onWheel={handleWheel}
        onMouseDown={handleCanvasMouseDown}
        onMouseMove={handleCanvasHover}
        onMouseLeave={handleCanvasLeave}
        style={{
          flex:    1,
          cursor:  'crosshair',
          width:   '100%',
          height:  '100%',
          display: 'block',
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
        Scroll: zoom · Drag: pan · Click: inspect · Shift+click: label · Esc: close
      </div>
    </div>
  );
}
