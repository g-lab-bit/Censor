/**
 * RejectionToast — brief fade-in/out notification for rejection events.
 *
 * Props
 * -----
 * toast   { message: string, id: number } | null  — from useConfusableNegative
 *
 * Shows "Rejected" or "Reclassified → Duct" for ~2 s then fades out.
 * Positioned bottom-right so it doesn't overlap the CellPopout or
 * ConfusablePopover.  Non-interactive (pointerEvents none).
 */

import { useEffect, useState } from 'react';

export default function RejectionToast({ toast }) {
  const [visible, setVisible] = useState(false);

  useEffect(() => {
    if (!toast) { setVisible(false); return; }
    const t = setTimeout(() => setVisible(true), 10);
    return () => clearTimeout(t);
  }, [toast?.id]);

  if (!toast) return null;

  return (
    <div
      style={{
        position:      'fixed',
        bottom:        24,
        right:         24,
        background:    'rgba(30,30,30,0.92)',
        color:         '#fff',
        padding:       '8px 16px',
        borderRadius:  6,
        fontSize:      13,
        fontFamily:    'sans-serif',
        letterSpacing: '0.01em',
        boxShadow:     '0 4px 16px rgba(0,0,0,0.28)',
        zIndex:        10001,
        opacity:       visible ? 1 : 0,
        transition:    'opacity 0.18s ease',
        pointerEvents: 'none',
        userSelect:    'none',
      }}
    >
      {toast.message}
    </div>
  );
}
