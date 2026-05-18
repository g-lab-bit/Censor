/**
 * DebugVizOverlay — Canvas overlay for debug visualization in classification mode.
 *
 * Implements three observation tools (SPEC-censor-ui.md §8, SPEC-censor-core.md §10 Phase 2):
 *   1. Cluster boundary highlight — bounding rects for all clusters in viewport;
 *      hovered = blue accent, selected = green accent.
 *   2. Stroke weight band visualization — cluster fills colored by band index when
 *      weightBandMode is true.
 *   3. Feature vector overlay — selected cluster displays 12 named feature values
 *      as a text panel adjacent to the cluster bounds.
 *
 * Props
 * -----
 * debugVizData   {clusterBounds, selectedClusterId, hoveredClusterId, selectedFeatureOverlay}
 * transform      {scale, tx, ty}  — page-space → canvas-space
 * active         boolean  — debug overlay active (classification mode on)
 * weightBandMode boolean  — color clusters by stroke weight band
 * onClusterClick  (cluster_id | null) => void
 * onClusterHover  (cluster_id | null) => void
 * style          CSSProperties
 */

import { useRef, useEffect, useCallback } from 'react';

/* Up to 4 stroke weight bands in AEC drawings (SPEC-censor-core §2). */
const BAND_COLORS = [
  [100, 160, 220],   /* band 0 — thin lines (blue)   */
  [220, 160,  80],   /* band 1 — medium (amber)      */
  [ 80, 200, 120],   /* band 2 — heavy (green)       */
  [180,  80, 200],   /* band 3 — hairline (purple)   */
];

const PANEL_W  = 220;
const LINE_H   = 16;
const PAD      = 8;
const FONT     = '11px monospace';

/* ---------------------------------------------------------------------------
 * Drawing helpers
 * -------------------------------------------------------------------------*/

function drawBounds(ctx, clusterBounds, selectedClusterId, hoveredClusterId,
                    weightBandMode, transform) {
  const { scale, tx, ty } = transform;
  const sx = x => x * scale + tx;
  const sy = y => y * scale + ty;

  for (const entry of clusterBounds) {
    const { cluster_id, bounds, band_index } = entry;
    const [x0, y0, x1, y1] = bounds;
    const cx0 = sx(x0);
    const cy0 = sy(y0);
    const cw  = (x1 - x0) * scale;
    const ch  = (y1 - y0) * scale;

    const isSelected = cluster_id === selectedClusterId;
    const isHovered  = cluster_id === hoveredClusterId;

    /* Weight band fill (TASK-CUI-008) */
    if (weightBandMode && band_index >= 0 && band_index < BAND_COLORS.length) {
      const [r, g, b] = BAND_COLORS[band_index];
      ctx.fillStyle = `rgba(${r},${g},${b},0.18)`;
      ctx.fillRect(cx0, cy0, cw, ch);
    }

    /* Cluster boundary rect (TASK-CUI-009) */
    if (isSelected) {
      ctx.strokeStyle = 'rgba(60,200,100,0.95)';
      ctx.lineWidth   = 2;
      ctx.strokeRect(cx0 + 1, cy0 + 1, cw - 2, ch - 2);
    } else if (isHovered) {
      ctx.strokeStyle = 'rgba(60,130,255,0.80)';
      ctx.lineWidth   = 1.5;
      ctx.strokeRect(cx0 + 0.75, cy0 + 0.75, cw - 1.5, ch - 1.5);
    } else {
      ctx.strokeStyle = 'rgba(100,100,100,0.25)';
      ctx.lineWidth   = 1;
      ctx.strokeRect(cx0 + 0.5, cy0 + 0.5, cw - 1, ch - 1);
    }
  }
}

/* Feature vector panel (TASK-CUI-007) — drawn adjacent to the selected cluster. */
function drawFeaturePanel(ctx, canvas, featureOverlay, clusterBounds,
                           selectedClusterId, transform) {
  if (!featureOverlay?.found || !featureOverlay.features?.length) return;

  const { scale, tx, ty } = transform;
  const sx = x => x * scale + tx;
  const sy = y => y * scale + ty;

  const entry = clusterBounds.find(b => b.cluster_id === selectedClusterId);

  /* Panel height: header row + 12 feature rows */
  const PANEL_H = PAD * 2 + LINE_H * (featureOverlay.features.length + 1);

  /* Position panel to the right of the cluster; flip left if near right edge. */
  let px, py;
  if (entry) {
    const [x0, y0, , y1] = entry.bounds;
    const cx1 = sx(entry.bounds[2]);
    const cy0  = sy(y0);
    px = cx1 + 6;
    if (px + PANEL_W > canvas.width) px = sx(x0) - PANEL_W - 6;
    px = Math.max(4, Math.min(px, canvas.width - PANEL_W - 4));
    py = cy0;
    if (py + PANEL_H > canvas.height) py = canvas.height - PANEL_H - 4;
    py = Math.max(4, py);
  } else {
    /* Cluster not in viewport; draw panel at top-left corner. */
    px = 8;
    py = 8;
  }

  /* Panel background */
  ctx.fillStyle = 'rgba(20,20,20,0.84)';
  ctx.fillRect(px, py, PANEL_W, PANEL_H);

  ctx.font         = FONT;
  ctx.textBaseline = 'top';
  ctx.textAlign    = 'left';

  /* Header */
  ctx.fillStyle = 'rgba(160,230,110,1.0)';
  ctx.fillText(`cluster #${featureOverlay.cluster_id}`, px + PAD, py + PAD);

  /* Feature rows: name (left) + value (right-aligned) */
  featureOverlay.features.forEach(({ name, value }, i) => {
    const rowY   = py + PAD + LINE_H * (i + 1);
    const valStr = value.toFixed(3);

    ctx.fillStyle = 'rgba(190,190,190,0.90)';
    ctx.fillText(name, px + PAD, rowY);

    ctx.fillStyle  = 'rgba(255,210,100,1.0)';
    ctx.textAlign  = 'right';
    ctx.fillText(valStr, px + PANEL_W - PAD, rowY);
    ctx.textAlign  = 'left';
  });
}

function draw(ctx, canvas, debugVizData, transform, weightBandMode) {
  ctx.clearRect(0, 0, canvas.width, canvas.height);
  if (!debugVizData?.clusterBounds?.length) return;

  const { clusterBounds, selectedClusterId, hoveredClusterId, selectedFeatureOverlay } = debugVizData;

  drawBounds(ctx, clusterBounds, selectedClusterId, hoveredClusterId, weightBandMode, transform);
  drawFeaturePanel(ctx, canvas, selectedFeatureOverlay, clusterBounds, selectedClusterId, transform);
}

/* ---------------------------------------------------------------------------
 * Hit testing — find cluster_id at canvas-space (cx, cy)
 * -------------------------------------------------------------------------*/
function hitCluster(cx, cy, clusterBounds, transform) {
  const { scale, tx, ty } = transform;
  const px = (cx - tx) / scale;
  const py = (cy - ty) / scale;
  for (const entry of clusterBounds) {
    const [x0, y0, x1, y1] = entry.bounds;
    if (px >= x0 && px <= x1 && py >= y0 && py <= y1) {
      return entry.cluster_id;
    }
  }
  return null;
}

/* ---------------------------------------------------------------------------
 * Component
 * -------------------------------------------------------------------------*/
export default function DebugVizOverlay({
  debugVizData,
  transform      = { scale: 1, tx: 0, ty: 0 },
  active         = false,
  weightBandMode = false,
  onClusterClick,
  onClusterHover,
  style,
}) {
  const canvasRef = useRef(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (active && debugVizData) {
      draw(ctx, canvas, debugVizData, transform, weightBandMode);
    } else {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
    }
  }, [debugVizData, transform, active, weightBandMode]);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ro = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      canvas.width  = width;
      canvas.height = height;
      const ctx = canvas.getContext('2d');
      if (active && debugVizData) {
        draw(ctx, canvas, debugVizData, transform, weightBandMode);
      }
    });
    ro.observe(canvas);
    return () => ro.disconnect();
  }, [debugVizData, transform, active, weightBandMode]);

  const handleClick = useCallback((e) => {
    if (!active || !debugVizData?.clusterBounds?.length) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const id   = hitCluster(e.clientX - rect.left, e.clientY - rect.top,
                             debugVizData.clusterBounds, transform);
    onClusterClick?.(id);
  }, [active, debugVizData, transform, onClusterClick]);

  const handleMouseMove = useCallback((e) => {
    if (!active || !debugVizData?.clusterBounds?.length) return;
    const rect = canvasRef.current.getBoundingClientRect();
    const id   = hitCluster(e.clientX - rect.left, e.clientY - rect.top,
                             debugVizData.clusterBounds, transform);
    onClusterHover?.(id);
  }, [active, debugVizData, transform, onClusterHover]);

  const handleMouseLeave = useCallback(() => {
    onClusterHover?.(null);
  }, [onClusterHover]);

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
