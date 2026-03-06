/**
 * Error Row
 *
 * Red-bordered error display that parses provider/code/message from
 * message error with a retry button.
 */

import { AlertCircle, Loader2, RotateCcw } from 'lucide-solid'
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
    <div class="mt-2 p-3 bg-[var(--error-subtle)] border border-[var(--error)] rounded-[var(--radius-md)] animate-fade-in">
      <div class="flex items-center justify-between gap-3">
        <div class="flex items-start gap-2 flex-1 min-w-0">
          <AlertCircle class="w-4 h-4 text-[var(--error)] flex-shrink-0 mt-0.5" />
          <div class="flex-1 min-w-0">
            <span class="text-[10px] font-medium text-[var(--error)] uppercase tracking-wider block mb-0.5">
              {typeLabel()}
            </span>
            <span class="text-sm text-[var(--error)] break-words whitespace-pre-wrap leading-relaxed">
              {props.error.message}
            </span>
          </div>
        </div>
        <button
          type="button"
          onClick={() => props.onRetry()}
          disabled={props.isStreaming || props.isRetrying || countdown() > 0}
          class="px-3 py-1.5 bg-[var(--error)] hover:brightness-110 text-white text-xs font-medium rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)] disabled:opacity-50 disabled:cursor-not-allowed flex items-center gap-1.5"
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
        <p class="text-xs text-[var(--error)] opacity-75 mt-2">Retry available in {countdown()}s</p>
      </Show>
    </div>
  )
}
