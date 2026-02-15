/**
 * Error Boundary Component
 *
 * Premium error page matching the AVA design language.
 * Shows a full-screen error with glass card, animations, and helpful actions.
 */

import {
  AlertTriangle,
  Bug,
  ChevronDown,
  ChevronUp,
  Copy,
  ExternalLink,
  RefreshCw,
  RotateCcw,
} from 'lucide-solid'
import {
  type Component,
  createSignal,
  type JSX,
  Show,
  ErrorBoundary as SolidErrorBoundary,
} from 'solid-js'
import { logFatal } from '../services/logger'

interface ErrorFallbackProps {
  error: Error
  reset: () => void
}

const ErrorFallback: Component<ErrorFallbackProps> = (props) => {
  const [showDetails, setShowDetails] = createSignal(false)
  const [copied, setCopied] = createSignal(false)

  // eslint-disable-next-line solid/reactivity -- one-time log at mount
  logFatal('ErrorBoundary', `Caught render error: ${props.error.message}`, props.error.stack)

  const copyError = async () => {
    const errorText = `Error: ${props.error.message}\n\nStack:\n${props.error.stack || 'No stack trace'}`
    await navigator.clipboard.writeText(errorText)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  return (
    <div class="fixed inset-0 z-50 flex items-center justify-center bg-[var(--background)]">
      <div class="onboarding-card w-full max-w-md mx-4 p-8">
        <div class="step-enter flex flex-col items-center text-center">
          {/* Error icon with pulse */}
          <div class="stagger-child w-20 h-20 mb-6 rounded-2xl bg-[var(--error-subtle)] flex items-center justify-center">
            <AlertTriangle class="w-10 h-10 text-[var(--error)]" />
          </div>

          {/* Title */}
          <div class="stagger-child">
            <h1 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-2">
              Something went wrong
            </h1>
          </div>

          {/* Error message */}
          <div class="stagger-child w-full mb-6">
            <div class="px-4 py-3 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-xl text-left">
              <p class="text-sm font-mono text-[var(--error)] break-all leading-relaxed">
                {props.error.message || 'An unexpected error occurred'}
              </p>
            </div>
          </div>

          {/* Action buttons */}
          <div class="stagger-child flex items-center gap-3 mb-6">
            <button
              type="button"
              onClick={() => props.reset()}
              class="onboarding-btn-primary inline-flex items-center gap-2 px-6 py-2.5 bg-[var(--accent)] text-white font-medium rounded-xl text-sm"
            >
              <RotateCcw class="w-4 h-4" />
              Try Again
            </button>
            <button
              type="button"
              onClick={() => window.location.reload()}
              class="inline-flex items-center gap-2 px-6 py-2.5 border border-[var(--border-default)] text-[var(--text-secondary)] font-medium rounded-xl text-sm hover:bg-[var(--surface-raised)] transition-colors"
            >
              <RefreshCw class="w-4 h-4" />
              Reload
            </button>
          </div>

          {/* Expandable details */}
          <div class="stagger-child w-full">
            <button
              type="button"
              onClick={() => setShowDetails(!showDetails())}
              class="flex items-center gap-1.5 mx-auto text-sm text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
            >
              <Bug class="w-3.5 h-3.5" />
              Technical details
              {showDetails() ? (
                <ChevronUp class="w-3.5 h-3.5" />
              ) : (
                <ChevronDown class="w-3.5 h-3.5" />
              )}
            </button>

            <Show when={showDetails()}>
              <div class="mt-4 p-4 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-xl text-left">
                <div class="flex items-center justify-between mb-2">
                  <span class="text-xs font-mono text-[var(--text-muted)]">Stack Trace</span>
                  <button
                    type="button"
                    onClick={copyError}
                    class="inline-flex items-center gap-1 px-2 py-1 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] bg-[var(--surface-raised)] rounded-md transition-colors"
                  >
                    <Copy class="w-3 h-3" />
                    {copied() ? 'Copied' : 'Copy'}
                  </button>
                </div>
                <pre class="text-xs font-mono text-[var(--text-tertiary)] whitespace-pre-wrap overflow-x-auto max-h-40 overflow-y-auto leading-relaxed">
                  {props.error.stack || 'No stack trace available'}
                </pre>
              </div>
            </Show>
          </div>

          {/* Report link */}
          <div class="stagger-child mt-6">
            <a
              href="https://github.com/ava/issues/new"
              target="_blank"
              rel="noopener noreferrer"
              class="inline-flex items-center gap-1.5 text-sm text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            >
              Report this issue
              <ExternalLink class="w-3.5 h-3.5" />
            </a>
          </div>
        </div>
      </div>
    </div>
  )
}

// ============================================================================
// Error Boundary Wrapper
// ============================================================================

interface AppErrorBoundaryProps {
  children: JSX.Element
}

export const AppErrorBoundary: Component<AppErrorBoundaryProps> = (props) => {
  return (
    <SolidErrorBoundary
      fallback={(err, reset) => (
        <ErrorFallback error={err instanceof Error ? err : new Error(String(err))} reset={reset} />
      )}
    >
      {props.children}
    </SolidErrorBoundary>
  )
}

export { ErrorFallback }
