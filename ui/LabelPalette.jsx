/**
 * LabelPalette — Floating toolbar for label selection, seeding progress, and
 * per-class color override.
 *
 * Renders palette entries from the Censor C++ LabelPalette snapshot, with:
 *   - Hotkey letter and label name per slot
 *   - Progress bar showing count toward seeding threshold
 *   - Inline hint when below threshold ("Need N more Wall labels…")
 *   - Color swatch (native color picker) — updates labelColors for overlay
 *   - "Show predictions" toggle
 *   - "[+] Custom…" for adding user-defined label classes
 *
 * Props
 * -----
 * entries            LabelEntry[]     — snapshot from censor C++ palette
 * activeSlot         number           — currently selected slot (1-based, 0=none)
 * seedingThreshold   number           — SEEDING_THRESHOLD_PER_CLASS (default 10)
 * predictionsVisible boolean          — show predictions toggle state
 * visible            boolean          — palette visibility
 * onSlotSelect       (slot) => void
 * onColorChange      (label, r, g, b) => void  — values in 0-255
 * onTogglePredictions () => void
 * onAddCustom        (name) => void
 */

import { useState, useCallback, useRef } from 'react';

const SEEDING_THRESHOLD = 10;

/* Convert 0-1 float RGB to CSS hex string for color input value. */
function toHex(r01, g01, b01) {
  const h = v => Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16).padStart(2, '0');
  return `#${h(r01)}${h(g01)}${h(b01)}`;
}

/* Parse CSS hex string from color input to [r, g, b] 0-255. */
function hexToRgb255(hex) {
  return [
    parseInt(hex.slice(1, 3), 16),
    parseInt(hex.slice(3, 5), 16),
    parseInt(hex.slice(5, 7), 16),
  ];
}

/* ─── Single palette row ───────────────────────────────────────────────────── */

function PaletteRow({ entry, isActive, seedingThreshold, onSelect, onColorChange }) {
  const colorInputRef = useRef(null);
  const isSkip        = entry.name === 'Skip';

  const handleColorChange = useCallback((e) => {
    const [r, g, b] = hexToRgb255(e.target.value);
    onColorChange?.(entry.name, r, g, b);
  }, [entry.name, onColorChange]);

  const filled    = Math.min(entry.count, seedingThreshold);
  const total     = seedingThreshold;
  const pct       = isSkip ? 0 : (filled / total);
  const complete  = !isSkip && filled >= total;

  return (
    <div
      onClick={() => onSelect?.(entry.slot)}
      style={{
        display:        'flex',
        alignItems:     'center',
        gap:            6,
        padding:        '4px 8px',
        cursor:         'pointer',
        background:     isActive ? 'rgba(60,130,255,0.12)' : 'transparent',
        borderRadius:   3,
        userSelect:     'none',
        fontSize:       12,
        fontFamily:     'monospace',
      }}
    >
      {/* Hotkey badge */}
      <span style={{
        width:          16,
        textAlign:      'center',
        color:          '#888',
        fontSize:       10,
        background:     'rgba(0,0,0,0.06)',
        borderRadius:   2,
        padding:        '0 2px',
      }}>
        {entry.key || '·'}
      </span>

      {/* Label name */}
      <span style={{ flex: 1, color: isActive ? '#3c82ff' : '#333' }}>
        {entry.name}
      </span>

      {/* Progress bar — hidden for Skip slot */}
      {!isSkip && (
        <div style={{ display: 'flex', alignItems: 'center', gap: 4 }}>
          <div style={{
            width:        52,
            height:       6,
            background:   'rgba(0,0,0,0.08)',
            borderRadius: 3,
            overflow:     'hidden',
          }}>
            <div style={{
              width:        `${pct * 100}%`,
              height:       '100%',
              background:   complete ? 'rgba(40,180,80,0.8)' : 'rgba(60,130,255,0.6)',
              borderRadius: 3,
              transition:   'width 0.2s',
            }} />
          </div>
          <span style={{ color: '#999', fontSize: 9, width: 26 }}>
            {filled}/{total}
          </span>
        </div>
      )}

      {/* Color swatch / picker — hidden for Skip */}
      {!isSkip && (
        <div
          onClick={e => { e.stopPropagation(); colorInputRef.current?.click(); }}
          style={{
            width:        14,
            height:       14,
            borderRadius: 2,
            background:   toHex(entry.color[0], entry.color[1], entry.color[2]),
            border:       '1px solid rgba(0,0,0,0.18)',
            cursor:       'pointer',
            flexShrink:   0,
          }}
        />
      )}
      <input
        ref={colorInputRef}
        type="color"
        defaultValue={toHex(entry.color[0], entry.color[1], entry.color[2])}
        onChange={handleColorChange}
        style={{ display: 'none' }}
        onClick={e => e.stopPropagation()}
      />
    </div>
  );
}

/* ─── Seeding hint strip ───────────────────────────────────────────────────── */

function SeedingHints({ entries, activeSlot, seedingThreshold }) {
  const active = entries.find(e => e.slot === activeSlot);
  if (!active || active.name === 'Skip') return null;
  const remaining = seedingThreshold - active.count;
  if (remaining <= 0) return null;

  return (
    <div style={{
      fontSize:   10,
      color:      '#999',
      padding:    '2px 8px 4px',
      fontFamily: 'monospace',
    }}>
      Need {remaining} more {active.name} {remaining === 1 ? 'label' : 'labels'} before predictions activate.
    </div>
  );
}

/* ─── Custom label input ───────────────────────────────────────────────────── */

function CustomLabelInput({ onAddCustom }) {
  const [open,  setOpen]  = useState(false);
  const [value, setValue] = useState('');

  const commit = useCallback(() => {
    const name = value.trim();
    if (name) {
      onAddCustom?.(name);
      setValue('');
    }
    setOpen(false);
  }, [value, onAddCustom]);

  if (!open) {
    return (
      <button
        onClick={() => setOpen(true)}
        style={{
          display:    'block',
          width:      '100%',
          textAlign:  'left',
          background: 'none',
          border:     'none',
          padding:    '4px 8px',
          cursor:     'pointer',
          fontSize:   12,
          fontFamily: 'monospace',
          color:      '#555',
        }}
      >
        [+] Custom…
      </button>
    );
  }

  return (
    <div style={{ display: 'flex', padding: '4px 8px', gap: 4 }}>
      <input
        autoFocus
        value={value}
        onChange={e => setValue(e.target.value)}
        onKeyDown={e => {
          if (e.key === 'Enter')  commit();
          if (e.key === 'Escape') { setValue(''); setOpen(false); }
        }}
        placeholder="Label name…"
        style={{
          flex:       1,
          fontSize:   11,
          fontFamily: 'monospace',
          border:     '1px solid rgba(0,0,0,0.18)',
          borderRadius: 3,
          padding:    '2px 4px',
          outline:    'none',
        }}
      />
      <button
        onClick={commit}
        style={{
          background: 'rgba(60,130,255,0.9)',
          border:     'none',
          borderRadius: 3,
          color:      '#fff',
          cursor:     'pointer',
          fontSize:   11,
          padding:    '2px 6px',
        }}
      >
        Add
      </button>
    </div>
  );
}

/* ─── Component ────────────────────────────────────────────────────────────── */

export default function LabelPalette({
  entries            = [],
  activeSlot         = 0,
  seedingThreshold   = SEEDING_THRESHOLD,
  predictionsVisible = true,
  visible            = true,
  onSlotSelect,
  onColorChange,
  onTogglePredictions,
  onAddCustom,
  style,
}) {
  if (!visible) return null;

  return (
    <div
      style={{
        position:     'fixed',
        right:        12,
        top:          80,
        minWidth:     230,
        background:   '#fff',
        border:       '1px solid rgba(0,0,0,0.14)',
        borderRadius: 6,
        boxShadow:    '0 4px 16px rgba(0,0,0,0.12)',
        userSelect:   'none',
        zIndex:       9998,
        ...style,
      }}
    >
      {/* Palette rows */}
      <div style={{ paddingTop: 4 }}>
        {entries.map(entry => (
          <PaletteRow
            key={entry.slot}
            entry={entry}
            isActive={entry.slot === activeSlot}
            seedingThreshold={seedingThreshold}
            onSelect={onSlotSelect}
            onColorChange={onColorChange}
          />
        ))}
      </div>

      {/* Seeding hint for active slot */}
      <SeedingHints
        entries={entries}
        activeSlot={activeSlot}
        seedingThreshold={seedingThreshold}
      />

      {/* Divider */}
      <div style={{ borderTop: '1px solid rgba(0,0,0,0.08)', margin: '2px 0' }} />

      {/* Custom label */}
      <CustomLabelInput onAddCustom={onAddCustom} />

      {/* Show predictions toggle */}
      <div style={{ borderTop: '1px solid rgba(0,0,0,0.08)', margin: '2px 0' }} />
      <button
        onClick={onTogglePredictions}
        style={{
          display:    'block',
          width:      '100%',
          textAlign:  'left',
          background: predictionsVisible ? 'rgba(60,130,255,0.08)' : 'none',
          border:     'none',
          padding:    '5px 8px',
          cursor:     'pointer',
          fontSize:   11,
          fontFamily: 'monospace',
          color:      predictionsVisible ? '#3c82ff' : '#888',
          borderRadius: '0 0 6px 6px',
        }}
      >
        {predictionsVisible ? '● Show predictions' : '○ Show predictions'}
      </button>
    </div>
  );
}
