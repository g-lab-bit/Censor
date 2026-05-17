/**
 * useGridOverlay — React hook wiring Censor's C++ grid overlay APIs into state.
 *
 * Rapida calls this from its PDF viewer component.  `censorApi` is a thin
 * JS wrapper over the native addon (censor.dll), providing the same shape as
 * the C++ GridOverlay methods but as async/sync JS calls.
 *
 * censorApi shape expected:
 *   getOverlayData()  → GridOverlayData | null
 *   getPopoutData()   → CellPopoutData  | null
 *   setActiveCell(row, col)
 *   clearActiveCell()
 *   setHoveredCell(row, col)
 *   clearHoveredCell()
 *
 * Returns:
 *   overlayData, popoutData — current snapshots
 *   active                 — classification mode on/off
 *   toggleMode()           — toggles classification mode
 *   handleCellClick(r, c)  — sets active cell + refreshes popout
 *   handleCellHover(r, c)  — sets hovered cell (pass -1,-1 for none)
 *   refresh()              — pull latest snapshots from Censor
 */

import { useState, useCallback, useEffect, useRef } from 'react';

export function useGridOverlay(censorApi, { pollIntervalMs = 0 } = {}) {
  const [active,      setActive]      = useState(false);
  const [overlayData, setOverlayData] = useState(null);
  const [popoutData,  setPopoutData]  = useState(null);

  const apiRef = useRef(censorApi);
  useEffect(() => { apiRef.current = censorApi; }, [censorApi]);

  const refresh = useCallback(() => {
    const api = apiRef.current;
    if (!api) return;
    setOverlayData(api.getOverlayData() ?? null);
    setPopoutData(api.getPopoutData()   ?? null);
  }, []);

  /* Optional polling for live updates (e.g. background classification). */
  useEffect(() => {
    if (!pollIntervalMs || pollIntervalMs <= 0) return;
    const id = setInterval(refresh, pollIntervalMs);
    return () => clearInterval(id);
  }, [refresh, pollIntervalMs]);

  const toggleMode = useCallback(() => {
    setActive(prev => {
      const next = !prev;
      if (!next) {
        apiRef.current?.clearActiveCell();
        apiRef.current?.clearHoveredCell();
        setPopoutData(null);
      } else {
        refresh();
      }
      return next;
    });
  }, [refresh]);

  const handleCellClick = useCallback((row, col) => {
    const api = apiRef.current;
    if (!api) return;
    api.setActiveCell(row, col);
    setOverlayData(api.getOverlayData() ?? null);
    setPopoutData(api.getPopoutData()   ?? null);
  }, []);

  const handleCellHover = useCallback((row, col) => {
    const api = apiRef.current;
    if (!api) return;
    if (row < 0 || col < 0) api.clearHoveredCell();
    else                     api.setHoveredCell(row, col);
  }, []);

  const closePopout = useCallback(() => {
    apiRef.current?.clearActiveCell();
    setPopoutData(null);
    setOverlayData(prev => prev
      ? { ...prev, active_row: -1, active_col: -1 }
      : prev);
  }, []);

  return {
    active,
    overlayData,
    popoutData,
    toggleMode,
    handleCellClick,
    handleCellHover,
    closePopout,
    refresh,
  };
}
