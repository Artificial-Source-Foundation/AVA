/**
 * Error Boundary Component
 *
 * Minimalist text-only error screen matching the Pencil design.
 * Pure black background, centered error details with stack trace,
 * and simple text-only action links.
 */

import {
  type Component,
  createEffect,
  createSignal,
  For,
  type JSX,
  onCleanup,
  Show,
  ErrorBoundary as SolidErrorBoundary,
} from 'solid-js'
import { log } from '../lib/logger'
import { logFatal } from '../services/logger'

interface ErrorFallbackProps {
  error: Error
  reset: () => void
}

/** Parse stack trace into individual lines */
function parseStackLines(stack: string | undefined): string[] {
  if (!stack) return []
  return stack
    .split('\n')
    .map((l) => l.trim())
    .filter(Boolean)
    .slice(0, 15)
}

const ErrorFallback: Component<ErrorFallbackProps> = (props) => {
  const [copied, setCopied] = createSignal(false)
  let copiedResetTimer: number | undefined

  createEffect(() => {
    log.error('error', `Caught render error: ${props.error.message}`, props.error.stack)
    logFatal('ErrorBoundary', `Caught render error: ${props.error.message}`, props.error.stack)
  })

  const copyError = async (): Promise<void> => {
    try {
      const errorText = `Error: ${props.error.message}\n\nStack:\n${props.error.stack || 'No stack trace'}`
      await navigator.clipboard.writeText(errorText)
      setCopied(true)
      window.clearTimeout(copiedResetTimer)
      copiedResetTimer = window.setTimeout(() => setCopied(false), 2000)
    } catch {
      setCopied(false)
    }
  }

  onCleanup(() => {
    window.clearTimeout(copiedResetTimer)
  })

  const stackLines = (): string[] => parseStackLines(props.error.stack)

  return (
    <div class="fixed inset-0 z-50 flex flex-col items-center justify-center bg-[var(--background)]">
      {/* Inner container — vertically stacked, centered */}
      <div class="flex flex-col items-center gap-10">
        {/* Title + error message */}
        <div class="flex flex-col items-center gap-3">
          <h1 class="m-0 text-lg font-medium text-[var(--text-primary)]">Something went wrong.</h1>
          <p class="m-0 font-ui-mono text-[12px] text-[var(--text-muted)]">
            {props.error.message || 'An unexpected error occurred'}
          </p>
        </div>

        {/* Error details block */}
        <div class="w-[520px] max-w-[90vw] overflow-hidden rounded-[10px] border border-[var(--border-subtle)] bg-[var(--background-subtle)]">
          {/* Header: "Error details" + "Copy" */}
          <div class="flex h-[30px] items-center justify-between bg-[var(--alpha-white-3)] px-3">
            <span class="font-ui-mono text-[9px] font-medium tracking-[1px] text-[var(--text-muted)]">
              Error details
            </span>
            <button
              type="button"
              onClick={copyError}
              class="border-none bg-transparent p-0 font-ui-mono text-[9px] font-medium text-[var(--accent)]"
              aria-label="Copy error details"
            >
              {copied() ? 'Copied' : 'Copy'}
            </button>
          </div>

          {/* Stack trace body — capped height with scroll */}
          <div class="max-h-[200px] overflow-y-auto px-3 py-2.5">
            <Show
              when={stackLines().length > 0}
              fallback={
                <p class="m-0 font-ui-mono text-[11px] text-[var(--error)]">
                  {props.error.message || 'No stack trace available'}
                </p>
              }
            >
              <div class="flex flex-col gap-1">
                <For each={stackLines()}>
                  {(line, index) => (
                    <span
                      class="break-all font-ui-mono text-[11px] leading-[1.4]"
                      classList={{
                        'text-[var(--error)]': index() === 0,
                        'text-[var(--surface-overlay)]': index() > 0 && index() < 3,
                        'text-[var(--surface-raised)]': index() >= 3,
                      }}
                    >
                      {line}
                    </span>
                  )}
                </For>
              </div>
            </Show>
          </div>
        </div>

        {/* Actions: Reload + Report issue */}
        <div class="flex items-center gap-6">
          <button
            type="button"
            onClick={() => window.location.reload()}
            class="border-none bg-transparent p-0 text-sm font-medium text-[var(--accent)]"
          >
            Reload
          </button>
          <span class="text-sm text-[var(--surface-overlay)]">&middot;</span>
          <a
            href="https://github.com/ASF-GROUP/AVA/issues/new"
            target="_blank"
            rel="noopener noreferrer"
            class="text-sm text-[var(--text-tertiary)] no-underline"
          >
            Report issue
          </a>
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
