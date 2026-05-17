/**
 * ConfusablePopover — "Also a Wall?" confusable negative surfacing (SPEC §7).
 *
 * Shown after each label event.  Displays the head of the confusable suggestion
 * queue (lowest confidence first) and lets the user confirm (Y), reject (N), or
 * reject + reclassify (N then label key).  Dismissible via Esc or the × button.
 *
 * Props
 * -----
 * currentSuggestion  { suggestion_id, cluster_id, bounds, confidence, label? }
 *                    | null  — from useConfusableNegative
 * activeLabel        string  — label that was just applied ("Wall", "Duct" …)
 * remaining          number  — queue length including currentSuggestion
 * onConfirm          () => void
 * onReject           (newLabel: string | null) => void
 * onDismiss          () => void
 *
 * Keyboard shortcuts (only active while popover is visible):
 *   Y          — confirm (positive + contextual negatives)
 *   N          — enter "awaiting reclassification" mode
 *   [label key]— W/D/P/F/C/G/T/M/E while awaiting → reject + reclassify
 *   N (again)  — while awaiting → plain rejection
 *   Esc        — dismiss all remaining suggestions
 */

import { useRef, useEffect, useState, useCallback } from 'react';

/* ---------------------------------------------------------------------------
 * Label key → name mapping (mirrors the label palette hotkeys)
 * -------------------------------------------------------------------------*/
const LABEL_KEYS = {
  w: 'Wall',
  d: 'Duct',
  p: 'Pipe',
  f: 'Furniture',
  c: 'Ceiling',
  g: 'Grid line',
  t: 'Text',
  m: 'Dimension',
  e: 'Equipment',
};

/* ---------------------------------------------------------------------------
 * Thumbnail canvas — draws the cluster bounds rectangle
 * -------------------------------------------------------------------------*/
function drawThumbnail(ctx, canvas, bounds) {
  const { width, height } = canvas;
  ctx.clearRect(0, 0, width, height);

  ctx.fillStyle = '#f0f0f0';
  ctx.fillRect(0, 0, width, height);

  if (!bounds || bounds.length < 4) return;

  const [bx0, by0, bx1, by1] = bounds;
  const bw = bx1 - bx0;
  const bh = by1 - by0;
  if (bw <= 0 || bh <= 0) return;

  const PADDING = 10;
  const scale   = Math.min(
    (width  - PADDING * 2) / bw,
    (height - PADDING * 2) / bh,
  );
  const tx = width  / 2 - (bx0 + bw / 2) * scale;
  const ty = height / 2 - (by0 + bh / 2) * scale;

  const sx = x => x * scale + tx;
  const sy = y => y * scale + ty;

  ctx.fillStyle   = 'rgba(100,140,220,0.18)';
  ctx.fillRect(sx(bx0), sy(by0), bw * scale, bh * scale);

  ctx.strokeStyle = 'rgba(60,130,255,0.85)';
  ctx.lineWidth   = 1.5;
  ctx.strokeRect(sx(bx0) + 0.75, sy(by0) + 0.75, bw * scale - 1.5, bh * scale - 1.5);
}

/* ---------------------------------------------------------------------------
 * Drag hook for window repositioning
 * -------------------------------------------------------------------------*/
function useDrag(initial) {
  const [pos, setPos]   = useState(initial);
  const dragRef         = useRef(null);

  const onMouseDown = useCallback((e) => {
    if (e.button !== 0) return;
    dragRef.current = { mx: e.clientX, my: e.clientY, px: pos.x, py: pos.y };
    e.preventDefault();
  }, [pos]);

  useEffect(() => {
    const onMove = (e) => {
      if (!dragRef.current) return;
      setPos({
        x: dragRef.current.px + (e.clientX - dragRef.current.mx),
        y: dragRef.current.py + (e.clientY - dragRef.current.my),
      });
    };
    const onUp = () => { dragRef.current = null; };
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
export default function ConfusablePopover({
  currentSuggestion,
  activeLabel,
  remaining,
  onConfirm,
  onReject,
  onDismiss,
}) {
  const [awaitingReclassification, setAwaiting] = useState(false);

  /* Reset awaiting state when suggestion changes. */
  useEffect(() => {
    setAwaiting(false);
  }, [currentSuggestion?.suggestion_id]);

  const canvasRef = useRef(null);
  const { pos, onMouseDown: onTitleDrag } = useDrag({ x: window.innerWidth - 292, y: 80 });

  /* Draw thumbnail whenever bounds change. */
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas || !currentSuggestion) return;
    const ctx = canvas.getContext('2d');
    drawThumbnail(ctx, canvas, currentSuggestion.bounds);
  }, [currentSuggestion?.bounds]);

  /* Keyboard handler — only active while popover is visible. */
  useEffect(() => {
    if (!currentSuggestion) return;

    const handler = (e) => {
      if (e.target.tagName === 'INPUT' || e.target.tagName === 'TEXTAREA') return;

      const key = e.key.toLowerCase();

      if (awaitingReclassification) {
        if (LABEL_KEYS[key]) {
          e.preventDefault();
          setAwaiting(false);
          onReject?.(LABEL_KEYS[key]);
          return;
        }
        if (key === 'n' || key === 'enter') {
          e.preventDefault();
          setAwaiting(false);
          onReject?.(null);
          return;
        }
        if (key === 'escape') {
          e.preventDefault();
          setAwaiting(false);
          onDismiss?.();
          return;
        }
      } else {
        if (key === 'y') {
          e.preventDefault();
          onConfirm?.();
          return;
        }
        if (key === 'n') {
          e.preventDefault();
          setAwaiting(true);
          return;
        }
        if (key === 'escape') {
          e.preventDefault();
          onDismiss?.();
          return;
        }
      }
    };

    window.addEventListener('keydown', handler);
    return () => window.removeEventListener('keydown', handler);
  }, [currentSuggestion, awaitingReclassification, onConfirm, onReject, onDismiss]);

  if (!currentSuggestion) return null;

  const confPct = currentSuggestion.confidence != null
    ? Math.round(currentSuggestion.confidence * 100)
    : null;

  /* ── Button styles ── */
  const btnBase = {
    border:       'none',
    borderRadius: 4,
    cursor:       'pointer',
    fontSize:     12,
    fontFamily:   'sans-serif',
    padding:      '4px 14px',
    fontWeight:   600,
  };
  const btnY = { ...btnBase, background: 'rgba(60,180,80,0.15)',  color: '#1a7a32' };
  const btnN = { ...btnBase, background: 'rgba(220,60,60,0.12)',  color: '#a02020' };

  return (
    <div
      style={{
        position:     'fixed',
        left:         pos.x,
        top:          pos.y,
        width:        268,
        background:   '#fff',
        border:       '1px solid rgba(0,0,0,0.16)',
        borderRadius: 7,
        boxShadow:    '0 8px 28px rgba(0,0,0,0.16)',
        zIndex:       10000,
        userSelect:   'none',
        fontFamily:   'sans-serif',
      }}
    >
      {/* Title bar */}
      <div
        onMouseDown={onTitleDrag}
        style={{
          padding:      '7px 10px 6px',
          background:   'rgba(60,130,255,0.08)',
          borderBottom: '1px solid rgba(0,0,0,0.07)',
          cursor:       'move',
          display:      'flex',
          alignItems:   'center',
          justifyContent: 'space-between',
          borderRadius: '7px 7px 0 0',
        }}
      >
        <span style={{ fontSize: 12, fontWeight: 700, color: '#2a5fd6' }}>
          Also a {activeLabel ?? '…'}?
        </span>
        <div style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          {remaining > 1 && (
            <span style={{ fontSize: 10, color: '#888' }}>
              1 of {remaining}
            </span>
          )}
          <button
            onClick={onDismiss}
            style={{
              background: 'none', border: 'none', cursor: 'pointer',
              fontSize: 13, color: '#aaa', padding: '0 2px', lineHeight: 1,
            }}
            aria-label="Dismiss suggestions"
          >
            ✕
          </button>
        </div>
      </div>

      {/* Body */}
      <div style={{ padding: '10px 12px 8px', display: 'flex', gap: 10 }}>
        {/* Thumbnail */}
        <canvas
          ref={canvasRef}
          width={80}
          height={80}
          style={{
            border:       '1px solid rgba(0,0,0,0.10)',
            borderRadius: 4,
            flexShrink:   0,
            display:      'block',
          }}
        />

        {/* Info */}
        <div style={{ flex: 1, display: 'flex', flexDirection: 'column', justifyContent: 'center', gap: 4 }}>
          <div style={{ fontSize: 11, color: '#555' }}>
            Cluster {currentSuggestion.cluster_id}
          </div>
          {confPct != null && (
            <div style={{ fontSize: 11, color: '#888' }}>
              Confidence: {confPct}%
            </div>
          )}
          {currentSuggestion.bounds && (
            <div style={{ fontSize: 10, color: '#aaa', fontFamily: 'monospace' }}>
              {currentSuggestion.bounds.map(v => Math.round(v)).join(', ')}
            </div>
          )}
        </div>
      </div>

      {/* Awaiting reclassification overlay */}
      {awaitingReclassification ? (
        <div style={{
          padding:    '6px 12px 10px',
          borderTop:  '1px solid rgba(0,0,0,0.06)',
          background: 'rgba(255,240,200,0.55)',
        }}>
          <div style={{ fontSize: 11, color: '#8a5f00', marginBottom: 6, fontWeight: 600 }}>
            Press a label key to reclassify:
          </div>
          <div style={{ fontSize: 10, color: '#7a5a10', lineHeight: 1.7 }}>
            {Object.entries(LABEL_KEYS).map(([k, lbl]) => (
              <span key={k} style={{ marginRight: 8 }}>
                <kbd style={{
                  background: '#fff', border: '1px solid #ccc',
                  borderRadius: 3, padding: '1px 4px', fontFamily: 'monospace', fontSize: 10,
                }}>{k.toUpperCase()}</kbd> {lbl}
              </span>
            ))}
          </div>
          <div style={{ fontSize: 10, color: '#888', marginTop: 6 }}>
            Press N again to confirm plain rejection.
          </div>
        </div>
      ) : (
        /* Y / N buttons */
        <div style={{
          padding:    '0 12px 10px',
          display:    'flex',
          gap:        8,
          borderTop:  '1px solid rgba(0,0,0,0.06)',
          paddingTop: 8,
        }}>
          <button style={btnY} onClick={onConfirm}>
            Y — Yes, label it
          </button>
          <button style={btnN} onClick={() => setAwaiting(true)}>
            N — Not this one
          </button>
        </div>
      )}

      {/* Footer hint */}
      <div style={{
        fontSize:   10,
        color:      '#bbb',
        padding:    '2px 10px 5px',
        fontFamily: 'monospace',
        borderTop:  '1px solid rgba(0,0,0,0.05)',
      }}>
        Y: confirm · N: reject · N+key: reclassify · Esc: dismiss
      </div>
    </div>
  );
}
