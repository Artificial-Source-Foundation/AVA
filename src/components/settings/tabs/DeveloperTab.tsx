/**
 * Developer Settings Tab
 *
 * Toggle dev mode to capture and display console output inline.
 * Useful for debugging OAuth, provider connections, and agent issues
 * without needing browser devtools (which aren't available in Tauri).
 */

import { Code2, Copy, FileText, Terminal, Trash2 } from 'lucide-solid'
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
import { setDebugDevMode } from '../../../lib/debug-log'
import { getFrontendLogFilePath, readFrontendLogFile } from '../../../lib/logger'
import { clearDevLogs, getDevLogs } from '../../../services/dev-console'
import { useSettings } from '../../../stores/settings'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { extractSource, formatTime, levelColor, levelLabel, Toggle } from './developer/dev-helpers'

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
  const [fileLogContent, setFileLogContent] = createSignal('')
  const [fileLogLoading, setFileLogLoading] = createSignal(false)
  const [fileLogCopied, setFileLogCopied] = createSignal(false)
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
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      {/* Developer Mode */}
      <SettingsCard
        icon={Code2}
        title="Developer Mode"
        description="Toggle developer console and configure log verbosity."
      >
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Enable developer console</span>
            <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              Console capture is always active. This toggle controls Developer tab visibility.
            </p>
          </div>
          <Toggle
            checked={settings().devMode ?? false}
            onChange={(v) => {
              updateSettings({ devMode: v })
              setDebugDevMode(v)
            }}
          />
        </div>
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Log level</span>
            <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              DEBUG shows middleware + verbose internals. INFO is the default.
            </p>
          </div>
          <select
            value={settings().logLevel}
            onChange={(e) =>
              updateSettings({
                logLevel: e.currentTarget.value as 'debug' | 'info' | 'warn' | 'error',
              })
            }
            class="px-2 py-1 text-[var(--settings-text-badge)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
          >
            <option value="debug">DEBUG</option>
            <option value="info">INFO</option>
            <option value="warn">WARN</option>
            <option value="error">ERROR</option>
          </select>
        </div>
      </SettingsCard>

      {/* Console viewer */}
      <Show when={settings().devMode}>
        <SettingsCard
          icon={Terminal}
          title="Console Output"
          description="Live console log viewer with filtering."
        >
          <div class="flex items-center justify-between mb-2">
            <div class="flex items-center gap-2">
              <span class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
                {filteredLogs().length}
                <Show when={filteredLogs().length !== logs().length}> / {logs().length}</Show>{' '}
                entries
              </span>
            </div>
            <div class="flex items-center gap-2">
              <button
                type="button"
                onClick={handleCopy}
                class="flex items-center gap-1 px-2 py-1 text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
              >
                <Copy class="w-3 h-3" />
                {copied() ? 'Copied!' : 'Copy All'}
              </button>
              <button
                type="button"
                onClick={() => clearDevLogs()}
                class="flex items-center gap-1 px-2 py-1 text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--error)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
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
              class="px-2 py-1 text-[var(--settings-text-badge)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
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
              class="px-2 py-1 text-[var(--settings-text-badge)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)]"
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
              class="flex-1 px-2 py-1 text-[var(--settings-text-badge)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] text-[var(--text-secondary)] placeholder:text-[var(--text-muted)] outline-none"
            />
            <Show when={!stickToBottom()}>
              <button
                type="button"
                onClick={() => {
                  if (!scrollRef) return
                  scrollRef.scrollTop = scrollRef.scrollHeight
                  setStickToBottom(true)
                }}
                class="px-2 py-1 text-[var(--settings-text-badge)] text-[var(--accent)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]"
              >
                Jump to latest
              </button>
            </Show>
          </div>

          <div
            ref={scrollRef}
            onScroll={handleLogScroll}
            class="bg-[var(--gray-1)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] overflow-auto font-mono text-[var(--settings-text-button)] leading-[1.6]"
            style={{ height: '320px' }}
          >
            <Show
              when={filteredLogs().length > 0}
              fallback={
                <p class="text-[var(--text-muted)] text-center py-8 text-[var(--settings-text-button)]">
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

          <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] mt-2">
            Tip: Copy all logs and paste them when reporting issues.
          </p>
        </SettingsCard>
      </Show>

      {/* File Log Viewer */}
      <Show when={settings().devMode}>
        <SettingsCard
          icon={FileText}
          title="File Logs"
          description="Persistent file-based logs that survive across sessions."
        >
          <div class="flex items-center justify-between mb-2">
            <div class="flex items-center gap-2">
              <Show when={getFrontendLogFilePath()}>
                <span class="text-[var(--settings-text-badge)] text-[var(--text-muted)] font-mono truncate max-w-[200px]">
                  {getFrontendLogFilePath()}
                </span>
              </Show>
            </div>
            <div class="flex items-center gap-2">
              <button
                type="button"
                onClick={async () => {
                  setFileLogLoading(true)
                  try {
                    const content = await readFrontendLogFile(200)
                    setFileLogContent(content)
                  } finally {
                    setFileLogLoading(false)
                  }
                }}
                class="flex items-center gap-1 px-2 py-1 text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
              >
                {fileLogLoading() ? 'Loading...' : 'View Logs'}
              </button>
              <Show when={fileLogContent()}>
                <button
                  type="button"
                  onClick={async () => {
                    try {
                      await navigator.clipboard.writeText(fileLogContent())
                      setFileLogCopied(true)
                      setTimeout(() => setFileLogCopied(false), 1500)
                    } catch {
                      // ignore
                    }
                  }}
                  class="flex items-center gap-1 px-2 py-1 text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
                >
                  <Copy class="w-3 h-3" />
                  {fileLogCopied() ? 'Copied!' : 'Copy'}
                </button>
              </Show>
            </div>
          </div>

          <Show when={fileLogContent()}>
            <div
              class="bg-[var(--gray-1)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] overflow-auto font-mono text-[var(--settings-text-button)] leading-[1.6] whitespace-pre-wrap p-2"
              style={{ height: '280px' }}
            >
              {fileLogContent()}
            </div>
          </Show>

          <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] mt-2">
            File logs persist across sessions. Debug-level entries only written when Developer Mode
            is on.
          </p>
        </SettingsCard>
      </Show>
    </div>
  )
}
