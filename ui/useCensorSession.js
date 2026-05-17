/**
 * useCensorSession — Extends useGridOverlay with completeness HUD, discovery
 * queue, and session toast state. (SPEC-censor-ui.md §9-§11)
 *
 * Additional censorApi methods expected beyond useGridOverlay:
 *   getCompletenessData()           → CompletenessHUDData | null
 *   getDiscoveryQueue()             → DiscoveryEntry[]    | null
 *   getSessionStats()               → {labels_this_session, confidence_delta_pct} | null
 *   navigateToCluster(cluster_id)   — pan/zoom viewport to cluster bounds
 *   labelDiscoveryEntry(cluster_id, label) — assigns label, adds class to palette
 *
 * Returned state (extends useGridOverlay):
 *   hudData               CompletenessHUDData | null
 *   plateaued             boolean  — rate stalled, suggest opening discovery queue
 *   discoveryEntries      DiscoveryEntry[]
 *   discoveryPanelOpen    boolean
 *   openDiscoveryPanel()  () => void
 *   closeDiscoveryPanel() () => void
 *   sessionToastStats     {labels_this_session, confidence_delta_pct} | null
 *   closeSessionToast()   () => void
 *   handleDiscoveryNavigate(cluster_id) — navigate to entry
 *   handleDiscoveryLabel(cluster_id, label) — label entry + refresh HUD/queue
 *   refreshHUD()          () => void  — manual pull of HUD snapshot
 */

import { useState, useCallback, useEffect, useRef } from 'react';
import { useGridOverlay } from './useGridOverlay.js';

export function useCensorSession(censorApi, options = {}) {
  const { pollIntervalMs = 0 } = options;

  const gridState = useGridOverlay(censorApi, { pollIntervalMs });

  const [hudData,            setHudData]            = useState(null);
  const [discoveryEntries,   setDiscoveryEntries]   = useState([]);
  const [discoveryPanelOpen, setDiscoveryPanelOpen] = useState(false);
  const [sessionToastStats,  setSessionToastStats]  = useState(null);

  const apiRef     = useRef(censorApi);
  const prevActive = useRef(gridState.active);

  useEffect(() => { apiRef.current = censorApi; }, [censorApi]);

  /* Pull latest HUD snapshot from Censor. */
  const refreshHUD = useCallback(() => {
    const api = apiRef.current;
    if (!api?.getCompletenessData) return;
    setHudData(api.getCompletenessData() ?? null);
  }, []);

  /* Pull latest discovery queue from Censor. */
  const refreshDiscovery = useCallback(() => {
    const api = apiRef.current;
    if (!api?.getDiscoveryQueue) return;
    setDiscoveryEntries(api.getDiscoveryQueue() ?? []);
  }, []);

  /* Poll HUD while in classification mode. */
  useEffect(() => {
    if (!gridState.active) return;
    refreshHUD();
    if (!pollIntervalMs || pollIntervalMs <= 0) return;
    const id = setInterval(refreshHUD, pollIntervalMs);
    return () => clearInterval(id);
  }, [gridState.active, refreshHUD, pollIntervalMs]);

  /* Populate discovery queue whenever the panel is opened. */
  useEffect(() => {
    if (discoveryPanelOpen) refreshDiscovery();
  }, [discoveryPanelOpen, refreshDiscovery]);

  /* Detect classification mode exit → capture session stats for toast. */
  useEffect(() => {
    const wasActive = prevActive.current;
    prevActive.current = gridState.active;

    if (wasActive && !gridState.active) {
      const stats = apiRef.current?.getSessionStats?.() ?? null;
      if (stats && stats.labels_this_session > 0) {
        setSessionToastStats(stats);
      }
      /* Close HUD + discovery panel when mode exits. */
      setHudData(null);
      setDiscoveryPanelOpen(false);
    }
  }, [gridState.active]);

  const openDiscoveryPanel  = useCallback(() => setDiscoveryPanelOpen(true),  []);
  const closeDiscoveryPanel = useCallback(() => setDiscoveryPanelOpen(false), []);
  const closeSessionToast   = useCallback(() => setSessionToastStats(null),   []);

  const handleDiscoveryNavigate = useCallback((cluster_id) => {
    apiRef.current?.navigateToCluster?.(cluster_id);
  }, []);

  const handleDiscoveryLabel = useCallback((cluster_id, label) => {
    apiRef.current?.labelDiscoveryEntry?.(cluster_id, label);
    refreshHUD();
    refreshDiscovery();
  }, [refreshHUD, refreshDiscovery]);

  /* Plateau: labeling rate has stalled (est_seconds == -1 from C++) and some
   * progress exists.  Signals Rapida to show "Find unknowns" affordance. */
  const plateaued = Boolean(
    hudData &&
    hudData.total_clusters > 0 &&
    hudData.pct_understood  > 0 &&
    hudData.est_seconds     === -1,
  );

  return {
    ...gridState,
    hudData,
    plateaued,
    discoveryEntries,
    discoveryPanelOpen,
    openDiscoveryPanel,
    closeDiscoveryPanel,
    sessionToastStats,
    closeSessionToast,
    handleDiscoveryNavigate,
    handleDiscoveryLabel,
    refreshHUD,
  };
}
