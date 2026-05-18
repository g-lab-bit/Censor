/**
 * useDebugViz — React hook wiring Censor's debug visualization APIs into state.
 *
 * Manages three debug overlays backed by DebugVizData (SPEC-censor-ui.md §8,
 * SPEC-censor-core.md §10 Phase 2):
 *   1. Feature vector overlay  — click cluster → 12 named feature values
 *   2. Stroke weight band viz  — color clusters by band index on demand
 *   3. Cluster boundary highlight — bounding rects for hovered/selected clusters
 *
 * censorApi shape expected:
 *   getFeatureOverlay(cluster_id)          → FeatureOverlayData | null
 *   getClusterBounds(x0, y0, x1, y1)      → ClusterBoundsEntry[] | null
 *
 * Returns:
 *   debugVizData        — data snapshot for DebugVizOverlay
 *   weightBandMode      — boolean, stroke weight band coloring enabled
 *   toggleWeightBandMode() — flip weightBandMode
 *   handleClusterClick(cluster_id) — select cluster, load feature overlay
 *   handleClusterHover(cluster_id | null) — track hovered cluster
 *   clearSelection()    — deselect current cluster
 *   refresh(viewport)   — re-fetch cluster bounds for viewport [x0, y0, x1, y1]
 */

import { useState, useCallback, useRef, useEffect } from 'react';

export function useDebugViz(censorApi) {
  const [weightBandMode,         setWeightBandMode]         = useState(false);
  const [clusterBounds,          setClusterBounds]          = useState([]);
  const [selectedClusterId,      setSelectedClusterId]      = useState(null);
  const [hoveredClusterId,       setHoveredClusterId]       = useState(null);
  const [selectedFeatureOverlay, setSelectedFeatureOverlay] = useState(null);

  const apiRef = useRef(censorApi);
  useEffect(() => { apiRef.current = censorApi; }, [censorApi]);

  /* Refresh cluster bounds for the given viewport [x0, y0, x1, y1].
   * When weightBandMode is active, each entry is enriched with band_index
   * from feature dim STROKE_WEIGHT_BAND (index 3). */
  const refresh = useCallback((viewport = [0, 0, Infinity, Infinity]) => {
    const api = apiRef.current;
    if (!api) return;

    const [x0, y0, x1, y1] = viewport;
    const bounds = api.getClusterBounds?.(x0, y0, x1, y1) ?? [];

    if (weightBandMode) {
      const enriched = bounds.map(b => {
        const fov = api.getFeatureOverlay?.(b.cluster_id);
        const band_index = (fov?.found && fov.features?.[3])
          ? Math.round(fov.features[3].value)
          : -1;
        return { ...b, band_index };
      });
      setClusterBounds(enriched);
    } else {
      setClusterBounds(bounds.map(b => ({ ...b, band_index: -1 })));
    }
  }, [weightBandMode]);

  const handleClusterClick = useCallback((cluster_id) => {
    setSelectedClusterId(cluster_id);
    if (cluster_id == null) {
      setSelectedFeatureOverlay(null);
      return;
    }
    const fov = apiRef.current?.getFeatureOverlay?.(cluster_id) ?? null;
    setSelectedFeatureOverlay(fov?.found ? fov : null);
  }, []);

  const handleClusterHover = useCallback((cluster_id) => {
    setHoveredClusterId(cluster_id ?? null);
  }, []);

  const toggleWeightBandMode = useCallback(() => {
    setWeightBandMode(prev => !prev);
  }, []);

  const clearSelection = useCallback(() => {
    setSelectedClusterId(null);
    setSelectedFeatureOverlay(null);
  }, []);

  return {
    debugVizData: {
      clusterBounds,
      selectedClusterId,
      hoveredClusterId,
      selectedFeatureOverlay,
    },
    weightBandMode,
    toggleWeightBandMode,
    handleClusterClick,
    handleClusterHover,
    clearSelection,
    refresh,
  };
}
