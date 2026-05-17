/**
 * SessionToast — End-of-session feedback (SPEC-censor-ui.md §11).
 *
 * Shown when the user exits classification mode.  Reports how many elements
 * were labeled and how much mean confidence improved this session.  Auto-
 * dismisses after autoCloseMs (default 6 s) or on close button click.
 *
 * Props
 * -----
 * stats        {labels_this_session: number, confidence_delta_pct: number}
 * onClose      () => void
 * autoCloseMs  number (default 6000)
 */

import { useEffect } from 'react';

export default function SessionToast({ stats, onClose, autoCloseMs = 6000 }) {
  if (!stats) return null;

  /* Auto-dismiss. */
  useEffect(() => {
    const id = setTimeout(() => onClose?.(), autoCloseMs);
    return () => clearTimeout(id);
  }, [autoCloseMs, onClose]);

  const count = stats.labels_this_session;
  const delta = stats.confidence_delta_pct;

  /* "47 elements classified today, model confidence up 12%." */
  const countStr = count != null
    ? `${count} element${count !== 1 ? 's' : ''} classified today`
    : 'Classification session complete';

  const deltaStr = delta != null && Math.abs(delta) >= 0.5
    ? `, model confidence ${delta >= 0 ? 'up' : 'down'} ${Math.abs(Math.round(delta))}%`
    : '';

  return (
    <div
      style={{
        position:     'fixed',
        bottom:       20,
        right:        20,
        background:   'rgba(28,28,32,0.93)',
        color:        '#e8e8e8',
        borderRadius: 6,
        padding:      '10px 14px',
        fontSize:     12,
        fontFamily:   'monospace',
        boxShadow:    '0 4px 16px rgba(0,0,0,0.28)',
        zIndex:       9200,
        display:      'flex',
        alignItems:   'center',
        gap:          12,
        maxWidth:     360,
      }}
    >
      <span>{countStr}{deltaStr}.</span>
      <button
        onClick={onClose}
        style={{
          background: 'none',
          border:     'none',
          color:      '#888',
          cursor:     'pointer',
          fontSize:   13,
          padding:    0,
          lineHeight: 1,
          flexShrink: 0,
        }}
        aria-label="Dismiss"
      >
        ✕
      </button>
    </div>
  );
}
