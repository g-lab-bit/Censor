/**
 * DiscoveryQueuePanel — Side panel showing top-10 unknown clusters (SPEC-censor-ui.md §10).
 *
 * Clusters are sorted by max_distance from all labeled examples in feature
 * space (furthest = most novel = most interesting to label next).
 *
 * Clicking an entry navigates the viewport to that cluster.  Clicking "name"
 * opens an inline text input; submitting creates a new palette class and
 * labels that cluster, which moves completeness.
 *
 * Props
 * -----
 * entries    DiscoveryEntry[]  — [{cluster_id, page, bounds, max_distance}]
 * onNavigate (cluster_id) => void
 * onLabel    (cluster_id, labelName) => void
 * onClose    () => void
 */

import { useState, useCallback } from 'react';

export default function DiscoveryQueuePanel({ entries = [], onNavigate, onLabel, onClose }) {
  const [labelingId, setLabelingId] = useState(null);
  const [labelText,  setLabelText]  = useState('');

  const handleNavigate = useCallback((entry) => {
    onNavigate?.(entry.cluster_id);
  }, [onNavigate]);

  const startLabel = useCallback((cluster_id) => {
    setLabelingId(cluster_id);
    setLabelText('');
  }, []);

  const commitLabel = useCallback((cluster_id) => {
    const name = labelText.trim();
    if (!name) { setLabelingId(null); return; }
    onLabel?.(cluster_id, name);
    setLabelingId(null);
    setLabelText('');
  }, [labelText, onLabel]);

  const cancelLabel = useCallback(() => {
    setLabelingId(null);
    setLabelText('');
  }, []);

  return (
    <div
      style={{
        position:     'fixed',
        right:        12,
        top:          '50%',
        transform:    'translateY(-50%)',
        width:        240,
        maxHeight:    '60vh',
        background:   'rgba(255,255,255,0.97)',
        border:       '1px solid rgba(0,0,0,0.12)',
        borderRadius: 6,
        boxShadow:    '0 4px 20px rgba(0,0,0,0.15)',
        display:      'flex',
        flexDirection:'column',
        zIndex:       9100,
        fontFamily:   'monospace',
        userSelect:   'none',
        overflow:     'hidden',
      }}
    >
      {/* Header */}
      <div
        style={{
          padding:        '8px 10px',
          borderBottom:   '1px solid rgba(0,0,0,0.08)',
          display:        'flex',
          alignItems:     'center',
          justifyContent: 'space-between',
          background:     'rgba(60,130,255,0.06)',
          flexShrink:     0,
        }}
      >
        <span style={{ fontSize: 11, color: '#444', fontWeight: 'bold' }}>
          Unknown Clusters
        </span>
        <button
          onClick={onClose}
          style={{
            background: 'none',
            border:     'none',
            cursor:     'pointer',
            fontSize:   13,
            color:      '#888',
            padding:    '0 2px',
            lineHeight: 1,
          }}
          aria-label="Close discovery queue"
        >
          ✕
        </button>
      </div>

      {/* Subtitle */}
      <div
        style={{
          padding:      '4px 10px',
          fontSize:     9,
          color:        '#aaa',
          borderBottom: '1px solid rgba(0,0,0,0.04)',
          flexShrink:   0,
        }}
      >
        Furthest from labeled examples — name one to grow the palette.
      </div>

      {/* Entry list */}
      <div style={{ overflowY: 'auto', flex: 1 }}>
        {entries.length === 0 && (
          <div style={{ padding: '16px 10px', fontSize: 11, color: '#bbb', textAlign: 'center' }}>
            No unknown clusters found.
          </div>
        )}

        {entries.map((entry, i) => (
          <div
            key={entry.cluster_id}
            style={{
              padding:      '6px 10px',
              borderBottom: '1px solid rgba(0,0,0,0.04)',
              background:   labelingId === entry.cluster_id
                ? 'rgba(60,130,255,0.06)'
                : 'transparent',
            }}
          >
            {/* Row: index · cluster label · distance · name button */}
            <div style={{ display: 'flex', alignItems: 'center' }}>
              <span
                style={{
                  fontSize:  10,
                  color:     '#ccc',
                  minWidth:  16,
                }}
              >
                {i + 1}.
              </span>

              <span
                style={{ fontSize: 10, color: '#555', flex: 1, cursor: 'pointer' }}
                onClick={() => handleNavigate(entry)}
                title="Navigate to cluster"
              >
                Cluster {entry.cluster_id}
                <span style={{ color: '#bbb', marginLeft: 4 }}>
                  p.{entry.page}
                </span>
              </span>

              <span
                style={{
                  fontSize:  9,
                  color:     '#ccc',
                  marginRight: 6,
                  fontVariantNumeric: 'tabular-nums',
                }}
                title="Distance from labeled examples"
              >
                {entry.max_distance != null ? entry.max_distance.toFixed(2) : '--'}
              </span>

              <button
                onClick={() =>
                  labelingId === entry.cluster_id
                    ? cancelLabel()
                    : startLabel(entry.cluster_id)
                }
                style={{
                  fontSize:     9,
                  padding:      '1px 5px',
                  border:       '1px solid rgba(60,130,255,0.4)',
                  borderRadius: 3,
                  background:   'none',
                  color:        'rgba(60,130,255,0.8)',
                  cursor:       'pointer',
                  flexShrink:   0,
                }}
              >
                {labelingId === entry.cluster_id ? 'cancel' : 'name'}
              </button>
            </div>

            {/* Inline label input (shown when naming this entry) */}
            {labelingId === entry.cluster_id && (
              <div style={{ display: 'flex', gap: 4, marginTop: 5, marginLeft: 16 }}>
                <input
                  autoFocus
                  type="text"
                  value={labelText}
                  onChange={e => setLabelText(e.target.value)}
                  onKeyDown={e => {
                    if (e.key === 'Enter')  commitLabel(entry.cluster_id);
                    if (e.key === 'Escape') cancelLabel();
                  }}
                  placeholder="New class name…"
                  style={{
                    flex:         1,
                    fontSize:     10,
                    padding:      '2px 5px',
                    border:       '1px solid rgba(60,130,255,0.4)',
                    borderRadius: 3,
                    fontFamily:   'monospace',
                    outline:      'none',
                  }}
                />
                <button
                  onClick={() => commitLabel(entry.cluster_id)}
                  style={{
                    fontSize:     10,
                    padding:      '2px 6px',
                    border:       '1px solid rgba(40,180,80,0.5)',
                    borderRadius: 3,
                    background:   'rgba(40,180,80,0.08)',
                    color:        'rgba(30,150,60,0.9)',
                    cursor:       'pointer',
                    flexShrink:   0,
                  }}
                >
                  Add
                </button>
              </div>
            )}
          </div>
        ))}
      </div>
    </div>
  );
}
