/**
 * Panel Error Boundary
 *
 * Per-panel error boundary with a compact, inline fallback.
 * Unlike the full-page AppErrorBoundary, this shows a small card
 * inside the panel with retry and error info.
 */

import { AlertTriangle, RotateCcw } from 'lucide-solid'
import { type Component, createSignal, ErrorBoundary, type JSX, Show } from 'solid-js'
import { log } from '../../lib/logger'

interface PanelErrorFallbackProps {
  error: Error
  reset: () => void
  panelName: string
}

const PanelErrorFallback: Component<PanelErrorFallbackProps> = (props) => {
  const [showStack, setShowStack] = createSignal(false)
  // eslint-disable-next-line solid/reactivity -- one-time log at mount
  log.error('error', `Panel error in ${props.panelName}: ${props.error.message}`, props.error.stack)

  return (
    <div class="flex flex-col items-center justify-center h-full p-6 text-center" role="alert">
      <div class="p-3 bg-[var(--error-subtle)] rounded-[var(--radius-lg)] mb-3">
        <AlertTriangle class="w-6 h-6 text-[var(--error)]" />
      </div>

      <h3 class="text-sm font-semibold text-[var(--text-primary)] mb-1">{props.panelName} Error</h3>

      <p class="text-xs text-[var(--text-muted)] mb-4 max-w-[240px]">
        {props.error.message || 'Something went wrong in this panel.'}
      </p>

      <button
        type="button"
        onClick={() => props.reset()}
        class="
          inline-flex items-center gap-1.5 px-3 py-1.5
          text-xs font-medium
          bg-[var(--accent)] text-white
          rounded-[var(--radius-md)]
          hover:opacity-90 transition-opacity
        "
        aria-label={`Retry loading ${props.panelName}`}
      >
        <RotateCcw class="w-3.5 h-3.5" />
        Retry
      </button>

      <Show when={props.error.stack}>
        <button
          type="button"
          onClick={() => setShowStack(!showStack())}
          class="mt-3 text-[10px] text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
        >
          {showStack() ? 'Hide details' : 'Show details'}
        </button>
        <Show when={showStack()}>
          <pre class="mt-2 p-2 text-[10px] text-left text-[var(--text-muted)] bg-[var(--surface-sunken)] rounded-[var(--radius-md)] max-h-24 overflow-auto w-full">
            {props.error.stack}
          </pre>
        </Show>
      </Show>
    </div>
  )
}

// ============================================================================
// Export
// ============================================================================

interface PanelErrorBoundaryProps {
  /** Display name for the panel (shown in error message) */
  panelName: string
  children: JSX.Element
}

export const PanelErrorBoundary: Component<PanelErrorBoundaryProps> = (props) => {
  return (
    <ErrorBoundary
      fallback={(err, reset) => (
        <PanelErrorFallback
          error={err instanceof Error ? err : new Error(String(err))}
          reset={reset}
          panelName={props.panelName}
        />
      )}
    >
      {props.children}
    </ErrorBoundary>
  )
}
