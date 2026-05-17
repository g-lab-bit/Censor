/**
 * useConfidenceOverlay — React hook wiring Censor's confidence overlay APIs into state.
 *
 * Manages the predictions-visible toggle and per-class label color overrides.
 * Color overrides live client-side (no round-trip to C++) — they shadow the
 * color field in CensorOverlayEntry when rendering.
 *
 * censorApi shape expected (in addition to getOverlayData / getPopoutData):
 *   getConfidenceOverlay() → { entries: CensorOverlayEntry[], completeness: number } | null
 *
 * Each CensorOverlayEntry:
 *   bounds:         [x0, y0, x1, y1]
 *   color:          [r, g, b, a]  — class RGB (0-1) + confidence alpha
 *   label:          string | null
 *   confidence:     number 0-1
 *   cluster_id:     number
 *   is_user_labeled: boolean
 *
 * Returns:
 *   confidenceData      — current snapshot for ConfidenceOverlay
 *   predictionsVisible  — global show-predictions flag
 *   togglePredictions   — flip predictionsVisible
 *   labelColors         — {[label]: [r,g,b]} 0-255 overrides for ConfidenceOverlay + GridOverlay
 *   setLabelColor       — (label, r, g, b) update color for one class (values 0-255)
 *   refresh             — pull latest snapshot from Censor
 */

import { useState, useCallback, useEffect, useRef } from 'react';

export function useConfidenceOverlay(censorApi, { pollIntervalMs = 0 } = {}) {
  const [confidenceData,     setConfidenceData]     = useState(null);
  const [predictionsVisible, setPredictionsVisible] = useState(true);
  const [labelColors,        setLabelColors]        = useState({});

  const apiRef = useRef(censorApi);
  useEffect(() => { apiRef.current = censorApi; }, [censorApi]);

  const refresh = useCallback(() => {
    const api = apiRef.current;
    if (!api) return;
    setConfidenceData(api.getConfidenceOverlay?.() ?? null);
  }, []);

  /* Optional polling for live updates as background classification runs. */
  useEffect(() => {
    if (!pollIntervalMs || pollIntervalMs <= 0) return;
    const id = setInterval(refresh, pollIntervalMs);
    return () => clearInterval(id);
  }, [refresh, pollIntervalMs]);

  const togglePredictions = useCallback(() => {
    setPredictionsVisible(prev => !prev);
  }, []);

  /* label is the human-readable class name; r/g/b are 0-255. */
  const setLabelColor = useCallback((label, r, g, b) => {
    setLabelColors(prev => ({ ...prev, [label.toLowerCase()]: [r, g, b] }));
  }, []);

  return {
    confidenceData,
    predictionsVisible,
    togglePredictions,
    labelColors,
    setLabelColor,
    refresh,
  };
}
