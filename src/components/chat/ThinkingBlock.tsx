/**
 * Thinking Block Component
 *
 * Collapsible display for extended thinking content.
 * Auto-expands while streaming, stays collapsed for completed messages.
 */

import { Brain, Check, ChevronRight, Copy } from 'lucide-solid'
import { type Component, createEffect, createSignal, Show } from 'solid-js'
import { useNotification } from '../../contexts/notification'
import { useSettings } from '../../stores/settings'

interface ThinkingBlockProps {
  thinking: string
  isStreaming: boolean
}

const formatCharCount = (len: number): string => {
  if (len >= 1000) return `${(len / 1000).toFixed(1)}k chars`
  return `${len} chars`
}

export const ThinkingBlock: Component<ThinkingBlockProps> = (props) => {
  const { settings } = useSettings()

  // Global hide: render nothing when ui.hideThinking is true
  const hidden = () => settings().ui.hideThinking
  const [expanded, setExpanded] = createSignal(false)
  const [copied, setCopied] = createSignal(false)
  const { success } = useNotification()

  // Auto-expand while streaming thinking
  createEffect(() => {
    if (props.isStreaming && props.thinking) setExpanded(true)
  })

  const handleCopy = async (e: MouseEvent) => {
    e.stopPropagation()
    try {
      await navigator.clipboard.writeText(props.thinking)
      setCopied(true)
      success('Copied')
      setTimeout(() => setCopied(false), 2000)
    } catch {
      // Clipboard API may fail in some contexts
    }
  }

  return (
    <Show when={!hidden()}>
      <div class="mb-2 animate-fade-in group/thinking">
        <div class="flex items-center gap-1.5">
          <button
            type="button"
            onClick={() => setExpanded((v) => !v)}
            aria-expanded={expanded()}
            class="flex items-center gap-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
          >
            <Brain class="w-3 h-3" />
            <span>
              {props.isStreaming
                ? 'Thinking...'
                : expanded()
                  ? 'Hide thinking'
                  : `Show thinking (${formatCharCount(props.thinking.length)})`}
            </span>
            <ChevronRight
              class={`w-3 h-3 transition-transform duration-[var(--duration-fast)] ${expanded() ? 'rotate-90' : ''}`}
            />
          </button>
          <button
            type="button"
            onClick={handleCopy}
            class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)] transition-all opacity-0 group-hover/thinking:opacity-100"
            title="Copy thinking"
            aria-label="Copy thinking content"
          >
            <Show when={copied()} fallback={<Copy class="w-3 h-3" />}>
              <Check class="w-3 h-3 text-[var(--success)]" />
            </Show>
          </button>
        </div>
        <Show when={expanded()}>
          <div
            class="mt-1.5 pl-3 border-l-2 border-[var(--border-subtle)] text-sm text-[var(--text-secondary)] opacity-80 whitespace-pre-wrap leading-relaxed max-h-[400px] overflow-y-auto scrollbar-thin"
            style={{ 'font-size': 'var(--chat-font-size)' }}
          >
            {props.thinking}
            <Show when={props.isStreaming}>
              <span class="streaming-cursor">▍</span>
            </Show>
          </div>
        </Show>
      </div>
    </Show>
  )
}
