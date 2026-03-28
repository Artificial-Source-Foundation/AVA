/**
 * Error Row
 *
 * Red-bordered error display that parses provider/code/message from
 * message error with a retry button.
 *
 * For `cancelled` errors, renders a subtle muted indicator instead of
 * a red error — the session was interrupted, not failed.
 */

import { AlertCircle, Ban, Loader2, RotateCcw } from 'lucide-solid'
import { type Component, createEffect, createSignal, on, onCleanup, Show } from 'solid-js'
import type { MessageError } from '../../../types'

interface ErrorRowProps {
  error: MessageError
  isStreaming: boolean
  isRetrying: boolean
  onRetry: () => void
}

const ERROR_TYPE_LABELS: Record<string, string> = {
  rate_limit: 'Rate Limited',
  auth: 'Authentication Error',
  server: 'Server Error',
  network: 'Network Error',
  api: 'API Error',
  unknown: 'Error',
  cancelled: 'Interrupted',
}

/** Contextual hints matching the TUI's error guidance */
const getErrorHint = (error: MessageError): string | null => {
  const msg = error.message.toLowerCase()
  const type = error.type

  if (type === 'rate_limit' || msg.includes('rate limit') || msg.includes('429')) {
    return 'Wait a moment and retry, or switch to a different model with /model.'
  }
  if (
    type === 'auth' ||
    msg.includes('auth') ||
    msg.includes('401') ||
    msg.includes('403') ||
    msg.includes('credential') ||
    msg.includes('api key')
  ) {
    return 'Check your credentials with /connect or verify your API key.'
  }
  if (
    msg.includes('context') ||
    msg.includes('token limit') ||
    msg.includes('too long') ||
    msg.includes('maximum context')
  ) {
    return 'Try /compact to reduce context window usage, or start a new session.'
  }
  if (
    type === 'network' ||
    msg.includes('timeout') ||
    msg.includes('connection') ||
    msg.includes('econnrefused')
  ) {
    return 'Check your network connection and try again.'
  }
  if (type === 'server' || msg.includes('500') || msg.includes('502') || msg.includes('503')) {
    return 'The provider is experiencing issues. Try again or switch providers.'
  }
  return null
}

export const ErrorRow: Component<ErrorRowProps> = (props) => {
  const [countdown, setCountdown] = createSignal(0)

  createEffect(
    on(
      () => props.error.retryAfter,
      (retryAfter) => {
        if (!retryAfter || retryAfter <= 0) {
          setCountdown(0)
          return
        }
        setCountdown(retryAfter)
        const timer = setInterval(() => {
          setCountdown((prev) => {
            if (prev <= 1) {
              clearInterval(timer)
              return 0
            }
            return prev - 1
          })
        }, 1000)
        onCleanup(() => clearInterval(timer))
      }
    )
  )

  const typeLabel = (): string => ERROR_TYPE_LABELS[props.error.type] ?? 'Error'

  return (
    <Show
      when={props.error.type !== 'cancelled'}
      fallback={
        <div class="mt-1 flex items-center gap-1.5 text-xs text-[var(--text-muted)] opacity-60 animate-fade-in">
          <Ban class="w-3 h-3 flex-shrink-0" />
          <span class="italic">Session interrupted</span>
        </div>
      }
    >
      <div
        class="mt-2 p-3 rounded-[10px] animate-fade-in"
        style={{
          background: 'var(--error-subtle)',
          border: '1px solid var(--error-border)',
        }}
      >
        <div class="flex items-center justify-between gap-3">
          <div class="flex items-start gap-2 flex-1 min-w-0">
            <AlertCircle class="mt-0.5 h-4 w-4 flex-shrink-0 text-[var(--error)]" />
            <div class="flex-1 min-w-0">
              <span class="mb-0.5 block text-[10px] font-semibold uppercase tracking-wider text-[var(--error)]">
                {typeLabel()}
              </span>
              <span class="break-words whitespace-pre-wrap font-[var(--font-ui-mono)] text-[12px] leading-[1.6] text-[var(--error)]">
                {props.error.message}
              </span>
            </div>
          </div>
          <button
            type="button"
            onClick={() => props.onRetry()}
            disabled={props.isStreaming || props.isRetrying || countdown() > 0}
            class="flex items-center gap-1.5 rounded-[8px] bg-[var(--error)] px-3 py-1.5 text-[11px] font-medium text-[var(--text-on-accent)] transition-colors duration-[var(--duration-fast)] hover:bg-[color-mix(in_srgb,var(--error)_88%,white_12%)] disabled:cursor-not-allowed disabled:opacity-50"
            aria-label="Retry failed response"
          >
            <Show
              when={props.isRetrying}
              fallback={
                <>
                  <RotateCcw class="w-3 h-3" />
                  Retry
                </>
              }
            >
              <Loader2 class="w-3 h-3 animate-spin" />
              Retrying
            </Show>
          </button>
        </div>
        <Show when={countdown() > 0}>
          <p class="mt-2 font-[var(--font-ui-mono)] text-[11px] text-[var(--error)] opacity-75">
            Retry available in {countdown()}s
          </p>
        </Show>
        <Show when={getErrorHint(props.error)}>
          {(hint) => <p class="text-[11px] text-[var(--gray-6)] mt-2 pl-6 italic">{hint()}</p>}
        </Show>
      </div>
    </Show>
  )
}
