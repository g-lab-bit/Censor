/**
 * GridOverlay — Canvas overlay component for the Censor grid + classification mode.
 *
 * Rapida mounts this on top of its PDF canvas.  The component reads
 * `overlayData` (shaped like GridOverlayData from the C++ API) and draws:
 *   - 1 px grid lines at 30 % opacity (zoom-independent screen-space)
 *   - Subtle tint on cells with classified clusters (or weight bands when showWeightBands)
 *   - Highlighted border on the active (clicked) cell
 *
 * Hotkey 'C' toggles classification mode on/off.
 *
 * Props
 * -----
 * overlayData     {lines, cells, active_row, active_col}  — from censor API
 * transform       {scale, tx, ty}  — maps page-space px to canvas-space px
 * active          boolean          — is classification mode on?
 * onToggle        () => void       — called when C key pressed
 * onCellClick     (row, col) => void
 * onCellHover     (row, col) => void  — called with (-1,-1) on mouse-leave
 * labelColors     {[label]: [r,g,b]}  — color overrides; falls back to defaults
 * showWeightBands boolean          — color cells by dominant_weight_band instead of label
 *
 * Cell data shape — Phase 2 addition (optional):
 *   cell.dominant_weight_band  'thin' | 'medium' | 'heavy' | null
 */

import { useRef, useEffect, useCallback } from 'react';

/* ---------------------------------------------------------------------------
 * Default AEC label colors (RGB 0-255)
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

// Matches SPEC-censor-core.md §2 stroke weight bands
const WEIGHT_BAND_COLORS = {
  thin:   [ 80, 180, 220],  // sky blue  — annotations/text
  medium: [220, 160,  80],  // amber     — MEP/partitions
  heavy:  [ 80, 100, 200],  // indigo    — walls/structure
};

function labelRgb(label, labelColors) {
  if (labelColors && labelColors[label]) return labelColors[label];
  const lc = label ? label.toLowerCase() : '';
  return DEFAULT_COLORS[lc] || [150, 150, 150];
}

function weightBandRgb(band) {
  return WEIGHT_BAND_COLORS[band] || [150, 150, 150];
}

/* ---------------------------------------------------------------------------
 * Hit testing: given canvas-relative (cx, cy) and the overlay cells (already
 * in canvas space via transform), return the first matching {row, col} or null.
 * -------------------------------------------------------------------------*/
function hitCell(cx, cy, cells, transform) {
  const { scale, tx, ty } = transform;
  // Convert canvas px → page px
  const px = (cx - tx) / scale;
  const py = (cy - ty) / scale;

  for (const cell of cells) {
    const [x0, y0, x1, y1] = cell.bounds;
    if (px >= x0 && px <= x1 && py >= y0 && py <= y1) {
      return { row: cell.row, col: cell.col };
    }
  }
  return null;
}

/* ---------------------------------------------------------------------------
 * Drawing
 * -------------------------------------------------------------------------*/
function draw(ctx, canvas, overlayData, transform, labelColors, showWeightBands) {
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);

  if (!overlayData) return;

  const { lines, cells, active_row, active_col } = overlayData;
  const { scale, tx, ty } = transform;

  /* Map a page-space coordinate to canvas space. */
  const sx = x => x * scale + tx;
  const sy = y => y * scale + ty;

  /* ── Cell fills — label tint or weight-band tint; active/hover borders ── */
  for (const cell of cells) {
    const [x0, y0, x1, y1] = cell.bounds;
    const cx0 = sx(x0), cy0 = sy(y0), cx1 = sx(x1), cy1 = sy(y1);
    const cw = cx1 - cx0, ch = cy1 - cy0;

    if (showWeightBands && cell.dominant_weight_band) {
      const [r, g, b] = weightBandRgb(cell.dominant_weight_band);
      ctx.fillStyle = `rgba(${r},${g},${b},0.15)`;
      ctx.fillRect(cx0, cy0, cw, ch);
    } else if (cell.has_clusters && cell.dominant_label) {
      const [r, g, b] = labelRgb(cell.dominant_label, labelColors);
      ctx.fillStyle = `rgba(${r},${g},${b},0.12)`;
      ctx.fillRect(cx0, cy0, cw, ch);
    }

    const isActive = cell.row === active_row && cell.col === active_col;
    if (isActive) {
      ctx.strokeStyle = 'rgba(60,130,255,0.9)';
      ctx.lineWidth   = 2;
      ctx.strokeRect(cx0 + 1, cy0 + 1, cw - 2, ch - 2);
    } else if (cell.is_hovered) {
      ctx.strokeStyle = 'rgba(60,130,255,0.4)';
      ctx.lineWidth   = 1;
      ctx.strokeRect(cx0 + 0.5, cy0 + 0.5, cw - 1, ch - 1);
    }
  }

  /* ── Grid lines — 1 px, 30 % opacity ── */
  ctx.strokeStyle = 'rgba(0,0,0,0.30)';
  ctx.lineWidth   = 1;
  ctx.beginPath();

  for (const x of lines.v_lines) {
    const cx = Math.round(sx(x)) + 0.5;
    ctx.moveTo(cx, 0);
    ctx.lineTo(cx, height);
  }
  for (const y of lines.h_lines) {
    const cy = Math.round(sy(y)) + 0.5;
    ctx.moveTo(0, cy);
    ctx.lineTo(width, cy);
  }
  ctx.stroke();
}

/* ---------------------------------------------------------------------------
 * Component
 * -------------------------------------------------------------------------*/
export default function GridOverlay({
  overlayData,
  transform       = { scale: 1, tx: 0, ty: 0 },
  active          = false,
  onToggle,
  onCellClick,
  onCellHover,
  labelColors,
  showWeightBands = false,
  style,
}) {
  const canvasRef = useRef(null);

  /* Redraw whenever overlay data or transform changes. */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (active && overlayData) {
      draw(ctx, canvas, overlayData, transform, labelColors, showWeightBands);
    } else {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
    }
  }, [overlayData, transform, active, labelColors, showWeightBands]);

  /* Resize canvas to match its CSS layout size. */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ro = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      canvas.width  = width;
      canvas.height = height;
      const ctx = canvas.getContext('2d');
      if (active && overlayData) draw(ctx, canvas, overlayData, transform, labelColors, showWeightBands);
    });
    ro.observe(canvas);
    return () => ro.disconnect();
  }, [overlayData, transform, active, labelColors, showWeightBands]);

  /* Hotkey 'C' toggles classification mode. */
  useEffect(() => {
    const handler = (e) => {
      if (e.key === 'c' || e.key === 'C') {
        if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;
        onToggle?.();
      }
    };
    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [onToggle]);

  const handleClick = useCallback((e) => {
    if (!active || !overlayData) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const cx   = e.clientX - rect.left;
    const cy   = e.clientY - rect.top;
    const hit  = hitCell(cx, cy, overlayData.cells, transform);
    if (hit) onCellClick?.(hit.row, hit.col);
  }, [active, overlayData, transform, onCellClick]);

  const handleMouseMove = useCallback((e) => {
    if (!active || !overlayData) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const cx   = e.clientX - rect.left;
    const cy   = e.clientY - rect.top;
    const hit  = hitCell(cx, cy, overlayData.cells, transform);
    if (hit) onCellHover?.(hit.row, hit.col);
    else     onCellHover?.(-1, -1);
  }, [active, overlayData, transform, onCellHover]);

  const handleMouseLeave = useCallback(() => {
    onCellHover?.(-1, -1);
  }, [onCellHover]);

  return (
    <canvas
      ref={canvasRef}
      onClick={handleClick}
      onMouseMove={handleMouseMove}
      onMouseLeave={handleMouseLeave}
      style={{
        position:      'absolute',
        inset:         0,
        pointerEvents: active ? 'auto' : 'none',
        cursor:        active ? 'crosshair' : 'default',
        ...style,
      }}
    />
  );
}
