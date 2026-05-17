/**
 * useConfusableNegative — manages the confusable negative suggestion queue
 * and rejection toast (SPEC-censor-ui §5 + §7).
 *
 * After each censor_label_cluster call, feed the returned suggestions via
 * feedSuggestions([]).  The hook exposes the queue head and handlers for
 * Y / N / N+label interactions.
 *
 * censorApi methods consumed (gracefully absent if not yet implemented):
 *   confirmSuggestion(suggestion_id)            — §7 Y path
 *   rejectSuggestion(suggestion_id, label|null) — §5 N / N+label path
 */

import { useState, useCallback } from 'react';

export function useConfusableNegative(censorApi) {
  const [suggestions, setSuggestions] = useState([]);
  const [toast,       setToast]       = useState(null); // { message, id }

  const feedSuggestions = useCallback((list) => {
    setSuggestions(list ?? []);
  }, []);

  const _advance = useCallback(() => {
    setSuggestions(prev => prev.slice(1));
  }, []);

  const _showToast = useCallback((message) => {
    const id = Date.now();
    setToast({ message, id });
    setTimeout(() => setToast(t => t?.id === id ? null : t), 2200);
  }, []);

  const currentSuggestion = suggestions[0] ?? null;

  const handleConfirm = useCallback(() => {
    if (!currentSuggestion) return;
    censorApi?.confirmSuggestion?.(currentSuggestion.suggestion_id);
    _advance();
  }, [currentSuggestion, censorApi, _advance]);

  /* newLabel: string (e.g. "Duct") or null for plain rejection. */
  const handleReject = useCallback((newLabel = null) => {
    if (!currentSuggestion) return;
    censorApi?.rejectSuggestion?.(currentSuggestion.suggestion_id, newLabel ?? null);
    _showToast(newLabel ? `Reclassified → ${newLabel}` : 'Rejected');
    _advance();
  }, [currentSuggestion, censorApi, _advance, _showToast]);

  const handleDismiss = useCallback(() => {
    setSuggestions([]);
  }, []);

  return {
    currentSuggestion,
    remaining: suggestions.length,
    toast,
    feedSuggestions,
    handleConfirm,
    handleReject,
    handleDismiss,
  };
}
