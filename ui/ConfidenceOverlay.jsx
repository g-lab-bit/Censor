/**
 * ConfidenceOverlay — Canvas overlay rendering CensorOverlayData prediction rects.
 *
 * Draws one semi-transparent filled rect per cluster entry from the Censor
 * confidence overlay API.  Each entry carries:
 *   color[0-2]  class RGB in [0,1] (label_to_rgb hash or user-overridden via labelColors)
 *   color[3]    confidence as alpha (high = saturated, low = washed out)
 *
 * When predictionsVisible is false, only user-labeled entries are drawn.
 * Never intercepts pointer events — markup tools work normally.
 *
 * Props
 * -----
 * overlayData        {entries: CensorOverlayEntry[], completeness: number} | null
 * transform          {scale, tx, ty}  — page-space → canvas-space
 * active             boolean          — is classification mode on?
 * predictionsVisible boolean          — global show predictions toggle
 * labelColors        {[label]: [r,g,b]}  — user color overrides (0-255 per channel)
 * style              CSSProperties    — forwarded to canvas element
 */

import { useRef, useEffect } from 'react';

function draw(ctx, canvas, overlayData, transform, predictionsVisible, labelColors) {
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);

  if (!overlayData?.entries?.length) return;

  const { scale, tx, ty } = transform;
  const sx = x => x * scale + tx;
  const sy = y => y * scale + ty;

  for (const entry of overlayData.entries) {
    if (!entry.is_user_labeled && !predictionsVisible) continue;

    const [x0, y0, x1, y1] = entry.bounds;
    const alpha = entry.color[3];

    /* User color override (0-255) takes precedence over Censor's computed color (0-1). */
    let r255, g255, b255;
    const lc = entry.label ? entry.label.toLowerCase() : '';
    const ov = labelColors?.[lc];
    if (ov) {
      [r255, g255, b255] = ov;
    } else {
      r255 = Math.round(entry.color[0] * 255);
      g255 = Math.round(entry.color[1] * 255);
      b255 = Math.round(entry.color[2] * 255);
    }

    const cx0 = sx(x0), cy0 = sy(y0);
    const cw  = (x1 - x0) * scale;
    const ch  = (y1 - y0) * scale;

    ctx.fillStyle = `rgba(${r255},${g255},${b255},${alpha.toFixed(3)})`;
    ctx.fillRect(cx0, cy0, cw, ch);
  }
}

export default function ConfidenceOverlay({
  overlayData,
  transform          = { scale: 1, tx: 0, ty: 0 },
  active             = false,
  predictionsVisible = true,
  labelColors,
  style,
}) {
  const canvasRef = useRef(null);

  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (active) {
      draw(ctx, canvas, overlayData, transform, predictionsVisible, labelColors);
    } else {
      ctx.clearRect(0, 0, canvas.width, canvas.height);
    }
  }, [overlayData, transform, active, predictionsVisible, labelColors]);

  /* Resize canvas to match CSS layout size. */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ro = new ResizeObserver(([entry]) => {
      const { width, height } = entry.contentRect;
      canvas.width  = width;
      canvas.height = height;
      const ctx = canvas.getContext('2d');
      if (active) {
        draw(ctx, canvas, overlayData, transform, predictionsVisible, labelColors);
      }
    });
    ro.observe(canvas);
    return () => ro.disconnect();
  }, [overlayData, transform, active, predictionsVisible, labelColors]);

  return (
    <canvas
      ref={canvasRef}
      style={{
        position:      'absolute',
        inset:         0,
        pointerEvents: 'none',
        ...style,
      }}
    />
  );
}
