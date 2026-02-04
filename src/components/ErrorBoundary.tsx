/**
 * Error Boundary Component
 *
 * Catches errors in child components and displays a friendly error page.
 * Prevents the entire app from crashing on errors.
 */

import { AlertTriangle, Bug, Copy, RefreshCw } from 'lucide-solid'
import {
  type Component,
  createSignal,
  type JSX,
  Show,
  ErrorBoundary as SolidErrorBoundary,
} from 'solid-js'
import { Button } from './ui/Button'

interface ErrorFallbackProps {
  error: Error
  reset: () => void
}

const ErrorFallback: Component<ErrorFallbackProps> = (props) => {
  const [showDetails, setShowDetails] = createSignal(false)
  const [copied, setCopied] = createSignal(false)

  const copyError = async () => {
    const errorText = `Error: ${props.error.message}\n\nStack:\n${props.error.stack || 'No stack trace'}`
    await navigator.clipboard.writeText(errorText)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  return (
    <div class="flex h-screen items-center justify-center bg-[var(--background)] p-6">
      <div class="max-w-lg w-full text-center">
        {/* Icon */}
        <div class="flex justify-center mb-6">
          <div class="p-4 rounded-full bg-[var(--error-subtle)]">
            <AlertTriangle class="w-12 h-12 text-[var(--error)]" />
          </div>
        </div>

        {/* Title */}
        <h1 class="text-2xl font-bold text-[var(--text-primary)] mb-2">Something went wrong</h1>

        {/* Description */}
        <p class="text-[var(--text-secondary)] mb-6">
          An unexpected error occurred. You can try reloading the page or report this issue.
        </p>

        {/* Error message preview */}
        <div class="mb-6 p-3 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)] text-left">
          <p class="text-sm font-mono text-[var(--error)] break-all">
            {props.error.message || 'Unknown error'}
          </p>
        </div>

        {/* Actions */}
        <div class="flex flex-col sm:flex-row items-center justify-center gap-3 mb-6">
          <Button variant="primary" onClick={props.reset} icon={<RefreshCw class="w-4 h-4" />}>
            Try Again
          </Button>
          <Button
            variant="secondary"
            onClick={() => window.location.reload()}
            icon={<RefreshCw class="w-4 h-4" />}
          >
            Reload Page
          </Button>
        </div>

        {/* Show details toggle */}
        <button
          type="button"
          onClick={() => setShowDetails(!showDetails())}
          class="text-sm text-[var(--text-tertiary)] hover:text-[var(--text-secondary)] transition-colors"
        >
          {showDetails() ? 'Hide' : 'Show'} technical details
        </button>

        {/* Technical details */}
        <Show when={showDetails()}>
          <div class="mt-4 p-4 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)] text-left">
            <div class="flex items-center justify-between mb-2">
              <span class="text-xs font-medium text-[var(--text-tertiary)] flex items-center gap-1.5">
                <Bug class="w-3.5 h-3.5" />
                Stack Trace
              </span>
              <Button
                variant="ghost"
                size="sm"
                onClick={copyError}
                icon={<Copy class="w-3.5 h-3.5" />}
              >
                {copied() ? 'Copied!' : 'Copy'}
              </Button>
            </div>
            <pre class="text-xs font-mono text-[var(--text-secondary)] whitespace-pre-wrap overflow-x-auto max-h-48 overflow-y-auto">
              {props.error.stack || 'No stack trace available'}
            </pre>
          </div>
        </Show>

        {/* Report link */}
        <p class="mt-6 text-sm text-[var(--text-muted)]">
          If this problem persists,{' '}
          <a
            href="https://github.com/estela/issues/new"
            target="_blank"
            rel="noopener noreferrer"
            class="text-[var(--accent)] hover:underline"
          >
            report an issue
          </a>
        </p>
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
