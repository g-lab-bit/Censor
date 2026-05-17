/**
 * CompletenessHUD — Persistent completeness metric bar (SPEC-censor-ui.md §9).
 *
 * Props
 * -----
 * hudData            {pct_understood, labeled_count, auto_classified,
 *                     total_clusters, est_seconds, visible}
 * plateaued          boolean  — labeling rate has stalled; show "Find unknowns"
 * onRequestDiscovery () => void
 */
export default function CompletenessHUD({ hudData, plateaued, onRequestDiscovery }) {
  if (!hudData || !hudData.visible) return null;

  const pct        = Math.round((hudData.pct_understood ?? 0) * 100);
  const understood = (hudData.labeled_count ?? 0) + (hudData.auto_classified ?? 0);
  const total      = hudData.total_clusters ?? 0;

  const fillColor = pct >= 80
    ? 'rgba(40,180,80,0.85)'
    : 'rgba(60,130,255,0.75)';

  return (
    <div
      style={{
        position:     'absolute',
        bottom:       12,
        left:         '50%',
        transform:    'translateX(-50%)',
        background:   'rgba(255,255,255,0.95)',
        border:       '1px solid rgba(0,0,0,0.12)',
        borderRadius: 6,
        padding:      '6px 12px',
        boxShadow:    '0 2px 10px rgba(0,0,0,0.12)',
        minWidth:     180,
        fontFamily:   'monospace',
        zIndex:       9000,
        userSelect:   'none',
      }}
    >
      <div
        style={{
          display:        'flex',
          justifyContent: 'space-between',
          alignItems:     'center',
          marginBottom:   4,
        }}
      >
        <span style={{ fontSize: 12, color: '#333', fontWeight: 'bold' }}>
          {pct}% understood
        </span>
        {plateaued && onRequestDiscovery && (
          <button
            onClick={onRequestDiscovery}
            style={{
              marginLeft:   8,
              fontSize:     10,
              color:        'rgba(60,130,255,1)',
              background:   'none',
              border:       'none',
              cursor:       'pointer',
              padding:      '1px 4px',
              borderRadius: 3,
            }}
          >
            Find unknowns →
          </button>
        )}
      </div>

      {/* Progress track */}
      <div
        style={{
          background:   'rgba(0,0,0,0.08)',
          borderRadius: 3,
          height:       6,
          overflow:     'hidden',
        }}
      >
        <div
          style={{
            width:        `${Math.max(0, Math.min(100, pct))}%`,
            height:       '100%',
            background:   fillColor,
            borderRadius: 3,
            transition:   'width 0.3s ease, background 0.3s ease',
          }}
        />
      </div>

      {total > 0 && (
        <div style={{ fontSize: 10, color: '#999', marginTop: 3 }}>
          {understood} / {total} clusters
        </div>
      )}
    </div>
  );
}
