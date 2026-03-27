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
  createEffect,
  createSignal,
  type JSX,
  Show,
  ErrorBoundary as SolidErrorBoundary,
} from 'solid-js'
import { log } from '../lib/logger'
import { logFatal } from '../services/logger'

interface ErrorFallbackProps {
  error: Error
  reset: () => void
}

const ErrorFallback: Component<ErrorFallbackProps> = (props) => {
  const [showDetails, setShowDetails] = createSignal(false)
  const [copied, setCopied] = createSignal(false)

  createEffect(() => {
    log.error('error', `Caught render error: ${props.error.message}`, props.error.stack)
    logFatal('ErrorBoundary', `Caught render error: ${props.error.message}`, props.error.stack)
  })

  const copyError = async () => {
    const errorText = `Error: ${props.error.message}\n\nStack:\n${props.error.stack || 'No stack trace'}`
    await navigator.clipboard.writeText(errorText)
    setCopied(true)
    setTimeout(() => setCopied(false), 2000)
  }

  return (
    <div
      class="fixed inset-0 z-50 flex flex-col items-center justify-center"
      style={{ background: 'var(--background)' }}
    >
      {/* Error icon */}
      <div
        class="flex items-center justify-center mb-6"
        style={{
          width: '72px',
          height: '72px',
          'border-radius': '18px',
          background: 'var(--error-subtle)',
        }}
      >
        <AlertTriangle class="w-9 h-9" style={{ color: 'var(--error)' }} />
      </div>

      {/* Title */}
      <h1 class="text-xl font-bold tracking-tight mb-2" style={{ color: 'var(--text-primary)' }}>
        Something went wrong
      </h1>
      <p class="text-sm mb-6" style={{ color: 'var(--text-muted)' }}>
        AVA encountered an unexpected error
      </p>

      {/* Error message */}
      <div
        class="w-full max-w-lg mb-8 text-left"
        style={{
          background: 'var(--surface-raised)',
          border: '1px solid var(--gray-5)',
          'border-radius': '12px',
          padding: '14px 18px',
        }}
      >
        <p class="text-sm font-mono break-all leading-relaxed" style={{ color: 'var(--error)' }}>
          {props.error.message || 'An unexpected error occurred'}
        </p>
      </div>

      {/* Action buttons */}
      <div class="flex items-center gap-3 mb-8">
        <button
          type="button"
          onClick={() => props.reset()}
          class="inline-flex items-center gap-2 px-7 py-3 text-sm font-semibold text-white transition-colors"
          style={{ background: 'var(--accent)', 'border-radius': '12px' }}
        >
          <RotateCcw class="w-4 h-4" />
          Try Again
        </button>
        <button
          type="button"
          onClick={() => window.location.reload()}
          class="inline-flex items-center gap-2 px-7 py-3 text-sm font-medium transition-colors"
          style={{
            background: 'var(--surface-raised)',
            border: '1px solid var(--gray-5)',
            'border-radius': '12px',
            color: 'var(--gray-9)',
          }}
        >
          <RefreshCw class="w-4 h-4" />
          Reload
        </button>
      </div>

      {/* Expandable details */}
      <button
        type="button"
        onClick={() => setShowDetails(!showDetails())}
        class="flex items-center gap-1.5 text-xs transition-colors"
        style={{ color: 'var(--text-tertiary)' }}
      >
        <Bug class="w-3.5 h-3.5" />
        Technical details
        {showDetails() ? <ChevronUp class="w-3.5 h-3.5" /> : <ChevronDown class="w-3.5 h-3.5" />}
      </button>

      <Show when={showDetails()}>
        <div
          class="mt-4 w-full max-w-lg text-left"
          style={{
            background: 'var(--surface-raised)',
            border: '1px solid var(--gray-5)',
            'border-radius': '12px',
            padding: '16px',
          }}
        >
          <div class="flex items-center justify-between mb-2">
            <span class="text-xs font-mono" style={{ color: 'var(--text-tertiary)' }}>
              Stack Trace
            </span>
            <button
              type="button"
              onClick={copyError}
              class="inline-flex items-center gap-1 px-2 py-1 text-xs transition-colors"
              style={{
                color: 'var(--text-muted)',
                background: 'var(--gray-5)',
                'border-radius': '6px',
              }}
            >
              <Copy class="w-3 h-3" />
              {copied() ? 'Copied' : 'Copy'}
            </button>
          </div>
          <pre
            class="text-xs font-mono whitespace-pre-wrap overflow-x-auto max-h-48 overflow-y-auto leading-relaxed"
            style={{ color: 'var(--text-muted)' }}
          >
            {props.error.stack || 'No stack trace available'}
          </pre>
        </div>
      </Show>

      {/* Report link */}
      <div class="mt-6">
        <a
          href="https://github.com/g0dxn4/AVA/issues/new"
          target="_blank"
          rel="noopener noreferrer"
          class="inline-flex items-center gap-1.5 text-xs transition-colors"
          style={{ color: 'var(--text-tertiary)' }}
        >
          Report this issue
          <ExternalLink class="w-3.5 h-3.5" />
        </a>
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
