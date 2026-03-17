/**
 * Thinking Row
 *
 * Collapsible reasoning display matching the TUI style:
 * - Grey/dimmed italic text
 * - "* Thinking ▶" (collapsed) / "* Thinking ▼" (expanded) header
 * - Collapsed: first 2 lines preview + "▶ ... (N more lines)" hint
 * - Auto-expands during streaming, auto-collapses when complete
 * - Shimmer pulse effect while streaming
 */

import { Check, Copy } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, For, Show } from 'solid-js'
import { useNotification } from '../../../contexts/notification'
import { useSettings } from '../../../stores/settings'

interface ThinkingRowProps {
  thinking: string
  isStreaming: boolean
}

const MAX_COLLAPSED_LINES = 2

export const ThinkingRow: Component<ThinkingRowProps> = (props) => {
  const { settings } = useSettings()
  const hidden = () => settings().ui.hideThinking
  const [expanded, setExpanded] = createSignal(false)
  const [copied, setCopied] = createSignal(false)
  const [wasStreaming, setWasStreaming] = createSignal(false)
  const { success } = useNotification()

  const lines = createMemo(() => {
    const text = props.thinking || ''
    return text.split('\n')
  })

  const totalLines = createMemo(() => lines().length)
  const isCollapsible = createMemo(() => totalLines() > MAX_COLLAPSED_LINES)

  const previewLines = createMemo(() => {
    if (expanded() || !isCollapsible()) return lines()
    return lines().slice(0, MAX_COLLAPSED_LINES)
  })

  // Auto-expand while streaming, auto-collapse when streaming ends
  createEffect(() => {
    if (props.isStreaming && props.thinking) {
      setExpanded(true)
      setWasStreaming(true)
    }
  })

  createEffect(() => {
    if (!props.isStreaming && wasStreaming()) {
      setExpanded(false)
      setWasStreaming(false)
    }
  })

  const handleCopy = async (e: MouseEvent): Promise<void> => {
    e.stopPropagation()
    try {
      await navigator.clipboard.writeText(props.thinking)
      setCopied(true)
      success('Copied')
      setTimeout(() => setCopied(false), 2000)
    } catch {
      // Clipboard API may fail
    }
  }

  const headerLabel = createMemo(() => {
    if (props.isStreaming && !props.thinking) return 'Thinking...'
    if (!isCollapsible()) return 'Thinking'
    return expanded() ? 'Thinking \u25BC' : 'Thinking \u25B6'
  })

  return (
    <Show when={!hidden()}>
      <div class="mb-2 animate-fade-in group/thinking">
        {/* Header row: bullet + label + copy button */}
        <div class="flex items-center gap-1.5">
          <button
            type="button"
            onClick={() => setExpanded((v) => !v)}
            aria-expanded={expanded()}
            class="flex items-center gap-1.5 text-xs transition-colors"
            style={{
              color: 'var(--text-muted)',
              'font-style': 'italic',
            }}
          >
            <span
              style={{
                color: 'var(--accent)',
                'font-style': 'normal',
                'font-size': '0.7em',
              }}
            >
              {'\u25CF'}
            </span>
            <span>{headerLabel()}</span>
          </button>
          <Show when={props.thinking && !props.isStreaming}>
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
          </Show>
        </div>

        {/* Content: preview lines when collapsed, all lines when expanded */}
        <Show when={props.thinking}>
          <div
            class={`mt-1 pl-3 border-l-2 border-[var(--border-subtle)] whitespace-pre-wrap leading-relaxed max-h-[400px] overflow-y-auto scrollbar-thin ${props.isStreaming ? 'thinking-shimmer' : ''}`}
            style={{
              color: 'var(--text-muted)',
              'font-style': 'italic',
              'font-size': 'calc(var(--chat-font-size, 14px) * 0.92)',
              opacity: '0.75',
            }}
          >
            <For each={previewLines()}>
              {(line) => <div style={{ 'min-height': '1.4em' }}>{line || '\u00A0'}</div>}
            </For>
            {/* Collapsed hint */}
            <Show when={!expanded() && isCollapsible()}>
              <button
                type="button"
                style={{
                  color: 'var(--text-muted)',
                  'font-style': 'italic',
                  opacity: '0.6',
                  cursor: 'pointer',
                  'margin-top': '2px',
                  background: 'none',
                  border: 'none',
                  padding: '0',
                  font: 'inherit',
                  'text-align': 'left',
                }}
                onClick={() => setExpanded(true)}
              >
                {`\u25B6 ... (${totalLines() - MAX_COLLAPSED_LINES} more lines)`}
              </button>
            </Show>
            {/* Streaming cursor */}
            <Show when={props.isStreaming}>
              <span class="streaming-cursor">{'\u2589'}</span>
            </Show>
          </div>
        </Show>
      </div>
    </Show>
  )
}
