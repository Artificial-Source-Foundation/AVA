/**
 * Developer Settings Tab
 *
 * Toggle dev mode to capture and display console output inline.
 * Useful for debugging OAuth, provider connections, and agent issues
 * without needing browser devtools (which aren't available in Tauri).
 */

import { Copy, Trash2 } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import { clearDevLogs, getDevLogs } from '../../../services/dev-console'
import { useSettings } from '../../../stores/settings'

// ============================================================================
// Helpers
// ============================================================================

const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
    {props.title}
  </h3>
)

const Toggle: Component<{ checked: boolean; onChange: (v: boolean) => void }> = (props) => (
  <button
    type="button"
    onClick={() => props.onChange(!props.checked)}
    class={`
      relative w-8 h-[18px] rounded-full transition-colors
      ${props.checked ? 'bg-[var(--accent)]' : 'bg-[var(--border-strong)]'}
    `}
  >
    <span
      class="absolute top-[2px] left-[2px] w-[14px] h-[14px] rounded-full bg-white transition-transform"
      style={{
        transform: props.checked ? 'translateX(14px)' : 'translateX(0)',
      }}
    />
  </button>
)

const levelColor: Record<string, string> = {
  log: 'var(--text-secondary)',
  info: 'var(--accent)',
  warn: '#e5a00d',
  error: 'var(--error)',
}

const levelLabel: Record<string, string> = {
  log: 'LOG',
  info: 'INF',
  warn: 'WRN',
  error: 'ERR',
}

function formatTime(ts: number): string {
  const d = new Date(ts)
  return `${d.getHours().toString().padStart(2, '0')}:${d.getMinutes().toString().padStart(2, '0')}:${d.getSeconds().toString().padStart(2, '0')}.${d.getMilliseconds().toString().padStart(3, '0')}`
}

function extractSource(message: string): string {
  const match = /^\[(.*?)\]/.exec(message)
  return match?.[1] ? match[1] : 'unknown'
}

// ============================================================================
// Main Tab
// ============================================================================

export const DeveloperTab: Component = () => {
  const { settings, updateSettings } = useSettings()
  const logs = getDevLogs()
  const [copied, setCopied] = createSignal(false)
  const [levelFilter, setLevelFilter] = createSignal<'all' | 'log' | 'info' | 'warn' | 'error'>(
    'all'
  )
  const [sourceFilter, setSourceFilter] = createSignal('all')
  const [textFilter, setTextFilter] = createSignal('')
  const [stickToBottom, setStickToBottom] = createSignal(true)
  let scrollRef: HTMLDivElement | undefined

  const availableSources = createMemo(() => {
    const unique = new Set<string>()
    for (const entry of logs()) {
      unique.add(extractSource(entry.message))
    }
    return ['all', ...Array.from(unique).sort()]
  })

  const filteredLogs = createMemo(() => {
    const query = textFilter().trim().toLowerCase()
    const level = levelFilter()
    const source = sourceFilter()
    return logs().filter((entry) => {
      if (level !== 'all' && entry.level !== level) return false
      if (source !== 'all' && extractSource(entry.message) !== source) return false
      if (!query) return true
      return entry.message.toLowerCase().includes(query)
    })
  })

  // Console capture is always active (installed in App.tsx).
  // devMode toggle only controls visibility of this tab.

  // Auto-scroll to bottom when new entries arrive
  createEffect(
    on(
      () => filteredLogs().length,
      () => {
        if (scrollRef && stickToBottom()) {
          scrollRef.scrollTop = scrollRef.scrollHeight
        }
      }
    )
  )

  let scrollRaf: number | undefined
  const handleLogScroll = () => {
    if (scrollRaf) return
    scrollRaf = requestAnimationFrame(() => {
      scrollRaf = undefined
      if (!scrollRef) return
      const distanceFromBottom =
        scrollRef.scrollHeight - scrollRef.scrollTop - scrollRef.clientHeight
      setStickToBottom(distanceFromBottom < 16)
    })
  }
  onCleanup(() => {
    if (scrollRaf) cancelAnimationFrame(scrollRaf)
  })

  const handleCopy = async () => {
    const text = filteredLogs()
      .map((e) => `[${formatTime(e.timestamp)}] ${levelLabel[e.level]} ${e.message}`)
      .join('\n')
    try {
      await navigator.clipboard.writeText(text)
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    } catch {
      // Fallback: select-all in a textarea
      const ta = document.createElement('textarea')
      ta.value = text
      document.body.appendChild(ta)
      ta.select()
      document.execCommand('copy')
      document.body.removeChild(ta)
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    }
  }

  return (
    <div class="space-y-5">
      {/* Toggle */}
      <div>
        <SectionHeader title="Developer Mode" />
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Enable developer console</span>
            <p class="text-[10px] text-[var(--text-muted)]">
              Console capture is always active. This toggle controls Developer tab visibility.
            </p>
          </div>
          <Toggle
            checked={settings().devMode ?? false}
            onChange={(v) => updateSettings({ devMode: v })}
          />
        </div>
      </div>

      {/* Console viewer */}
      <Show when={settings().devMode}>
        <div class="pt-2 border-t border-[var(--border-subtle)]">
          <div class="flex items-center justify-between mb-2">
            <SectionHeader title="Console Output" />
            <div class="flex items-center gap-2">
              <span class="text-[10px] text-[var(--text-muted)]">
                {filteredLogs().length}
                <Show when={filteredLogs().length !== logs().length}> / {logs().length}</Show>{' '}
                entries
              </span>
              <button
                type="button"
                onClick={handleCopy}
                class="flex items-center gap-1 px-2 py-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
              >
                <Copy class="w-3 h-3" />
                {copied() ? 'Copied!' : 'Copy All'}
              </button>
              <button
                type="button"
                onClick={() => clearDevLogs()}
                class="flex items-center gap-1 px-2 py-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--error)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
              >
                <Trash2 class="w-3 h-3" />
                Clear
              </button>
            </div>
          </div>

          <div class="flex items-center gap-2 mb-2">
            <select
              value={levelFilter()}
              onChange={(e) =>
                setLevelFilter(e.currentTarget.value as 'all' | 'log' | 'info' | 'warn' | 'error')
              }
              class="px-2 py-1 text-[10px] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
            >
              <option value="all">All levels</option>
              <option value="log">LOG</option>
              <option value="info">INF</option>
              <option value="warn">WRN</option>
              <option value="error">ERR</option>
            </select>
            <select
              value={sourceFilter()}
              onChange={(e) => setSourceFilter(e.currentTarget.value)}
              class="px-2 py-1 text-[10px] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
            >
              <For each={availableSources()}>
                {(source) => <option value={source}>{source}</option>}
              </For>
            </select>
            <input
              type="text"
              value={textFilter()}
              onInput={(e) => setTextFilter(e.currentTarget.value)}
              placeholder="Filter text..."
              class="flex-1 px-2 py-1 text-[10px] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)] placeholder:text-[var(--text-muted)] outline-none"
            />
            <Show when={!stickToBottom()}>
              <button
                type="button"
                onClick={() => {
                  if (!scrollRef) return
                  scrollRef.scrollTop = scrollRef.scrollHeight
                  setStickToBottom(true)
                }}
                class="px-2 py-1 text-[10px] text-[var(--accent)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]"
              >
                Jump to latest
              </button>
            </Show>
          </div>

          <div
            ref={scrollRef}
            onScroll={handleLogScroll}
            class="bg-[var(--gray-1)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] overflow-auto font-mono text-[11px] leading-[1.6]"
            style={{ height: '320px' }}
          >
            <Show
              when={filteredLogs().length > 0}
              fallback={
                <p class="text-[var(--text-muted)] text-center py-8 text-[11px]">
                  No logs match current filters.
                </p>
              }
            >
              <div class="p-2">
                <For each={filteredLogs()}>
                  {(entry) => (
                    <div class="flex gap-2 py-0.5 hover:bg-[var(--alpha-white-3)]">
                      <span class="text-[var(--text-muted)] flex-shrink-0 select-none">
                        {formatTime(entry.timestamp)}
                      </span>
                      <span
                        class="flex-shrink-0 font-semibold select-none"
                        style={{ color: levelColor[entry.level], width: '28px' }}
                      >
                        {levelLabel[entry.level]}
                      </span>
                      <span
                        class="flex-1 break-all whitespace-pre-wrap"
                        style={{
                          color:
                            entry.level === 'error'
                              ? 'var(--error)'
                              : entry.level === 'warn'
                                ? '#e5a00d'
                                : 'var(--text-secondary)',
                        }}
                      >
                        {entry.message}
                      </span>
                    </div>
                  )}
                </For>
              </div>
            </Show>
          </div>

          <p class="text-[10px] text-[var(--text-muted)] mt-2">
            Tip: Copy all logs and paste them when reporting issues.
          </p>
        </div>
      </Show>
    </div>
  )
}
