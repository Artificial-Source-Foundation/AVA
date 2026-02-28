/**
 * Diagnostics Store
 *
 * Tracks LSP diagnostic counts (errors, warnings, info).
 * Listens for `ava:diagnostics` events from the LSP extension.
 */

import { createMemo, createSignal } from 'solid-js'

export interface DiagnosticSummary {
  errors: number
  warnings: number
  info: number
}

const [diagnostics, setDiagnosticsRaw] = createSignal<DiagnosticSummary>({
  errors: 0,
  warnings: 0,
  info: 0,
})

export function updateDiagnostics(summary: DiagnosticSummary) {
  setDiagnosticsRaw(summary)
}

export const hasDiagnostics = createMemo(
  () => diagnostics().errors > 0 || diagnostics().warnings > 0
)

export function useDiagnostics() {
  return {
    diagnostics,
    hasDiagnostics,
    updateDiagnostics,
  }
}

// Listen for diagnostic events from the LSP extension
if (typeof window !== 'undefined') {
  window.addEventListener('ava:diagnostics', ((e: CustomEvent<DiagnosticSummary>) => {
    updateDiagnostics(e.detail)
  }) as EventListener)
}
