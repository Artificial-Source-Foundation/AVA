/**
 * Diagnostics Store
 *
 * Tracks LSP diagnostic counts (errors, warnings, info).
 * Listens for `ava:diagnostics` events from the LSP extension.
 */

import { createMemo, createSignal } from 'solid-js'
import { rustBackend } from '../services/rust-bridge'

export interface DiagnosticSummary {
  errors: number
  warnings: number
  info: number
}

export interface LspStatusSummary {
  enabled: boolean
  mode: string
  activeServerCount: number
  state: string
}

const [diagnostics, setDiagnosticsRaw] = createSignal<DiagnosticSummary>({
  errors: 0,
  warnings: 0,
  info: 0,
})
const [lspStatus, setLspStatus] = createSignal<LspStatusSummary>({
  enabled: false,
  mode: 'off',
  activeServerCount: 0,
  state: 'disabled',
})

export function updateDiagnostics(summary: DiagnosticSummary) {
  setDiagnosticsRaw(summary)
}

export const hasDiagnostics = createMemo(
  () => diagnostics().errors > 0 || diagnostics().warnings > 0
)

export const hasActiveLsp = createMemo(
  () =>
    lspStatus().enabled && (lspStatus().activeServerCount > 0 || lspStatus().state === 'starting')
)

export function useDiagnostics() {
  return {
    diagnostics,
    hasDiagnostics,
    lspStatus,
    hasActiveLsp,
    updateDiagnostics,
  }
}

if (typeof window !== 'undefined') {
  const refresh = async () => {
    try {
      const snapshot = await rustBackend.getLspStatus()
      updateDiagnostics(snapshot.summary)
      const state = snapshot.enabled
        ? (snapshot.servers.find((server) => server.active)?.state ?? 'idle')
        : 'disabled'
      setLspStatus({
        enabled: snapshot.enabled,
        mode: snapshot.mode,
        activeServerCount: snapshot.activeServerCount,
        state,
      })
    } catch {
      setLspStatus({
        enabled: false,
        mode: 'off',
        activeServerCount: 0,
        state: 'disabled',
      })
    }
  }

  void refresh()
  window.setInterval(() => {
    void refresh()
  }, 3000)
}
