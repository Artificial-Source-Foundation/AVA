/**
 * Developer Settings Tab — Pencil design revamp
 *
 * Two cards:
 * 1. Developer Mode — toggle + log level dropdown
 * 2. Console Output — colored log lines with Copy All / Clear buttons
 */

import { ChevronDown, Code, Copy, RefreshCw, Terminal } from 'lucide-solid'
import {
  type Component,
  createEffect,
  createMemo,
  createSignal,
  For,
  mergeProps,
  on,
  onCleanup,
  Show,
} from 'solid-js'
import { useAgent } from '../../../hooks/useAgent'
import { setDebugDevMode } from '../../../lib/debug-log'
import { clearDevLogs, getDevLogs } from '../../../services/dev-console'
import {
  getBackendLogFilePath,
  getLogDirectory,
  readLatestBackendLogs,
} from '../../../services/logger'
import { useSettings } from '../../../stores/settings'
import type { AgentEvent } from '../../../types/rust-ipc'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { formatTime, levelLabel } from './developer/dev-helpers'

/** Log level colors matching the Pencil design */
const PENCIL_LEVEL_COLORS: Record<string, string> = {
  log: '#C8C8CC',
  info: '#0A84FF',
  warn: '#F5A623',
  error: '#FF453A',
}

export const DeveloperTab: Component<{ showToggle?: boolean }> = (props) => {
  const merged = mergeProps({ showToggle: true }, props)
  const { settings, updateSettings } = useSettings()
  const agent = useAgent()
  const logs = getDevLogs()
  const [copied, setCopied] = createSignal(false)
  const [copiedDiagnostics, setCopiedDiagnostics] = createSignal(false)
  const [stickToBottom, setStickToBottom] = createSignal(true)
  const [showLevelDropdown, setShowLevelDropdown] = createSignal(false)
  const [backendLogTail, setBackendLogTail] = createSignal('')
  const [isRefreshingBackendLogs, setIsRefreshingBackendLogs] = createSignal(false)
  let latestBackendLogRefreshRequestId = 0
  let scrollRef: HTMLDivElement | undefined

  const filteredLogs = createMemo(() => logs())
  const currentRunId = createMemo(() => agent.currentRunId())
  const currentError = createMemo(() => agent.lastError())
  const progressMessage = createMemo(() => agent.progressMessage())
  const allEvents = createMemo(() => agent.eventTimeline())
  const eventCount = createMemo(() => allEvents().length)
  const recentEvents = createMemo(() => allEvents().slice(-12))
  const logDirectory = createMemo(() => getLogDirectory() || '(logger not initialized yet)')
  const backendLogFilePath = createMemo(
    () => getBackendLogFilePath() || '(backend logger not initialized yet)'
  )
  const runState = createMemo(() => {
    if (agent.isRunning()) return 'running'
    if (agent.pendingApproval()) return 'waiting-for-approval'
    if (agent.pendingQuestion()) return 'waiting-for-question'
    if (agent.pendingPlan()) return 'waiting-for-plan'
    if (currentError()) return 'error'
    return 'idle'
  })

  const formatAgentEvent = (event: AgentEvent): string => {
    const parts: string[] = [event.type]
    const correlatedRunId = event.runId ?? event.run_id
    if (correlatedRunId) parts.push(`run=${correlatedRunId}`)
    if ('message' in event && typeof event.message === 'string' && event.message) {
      parts.push(event.message)
    }
    if ('tool_name' in event && typeof event.tool_name === 'string') {
      parts.push(`tool=${event.tool_name}`)
    }
    if ('name' in event && typeof event.name === 'string') {
      parts.push(`tool=${event.name}`)
    }
    return parts.join(' | ')
  }

  const summarizeAgentEvent = (event: AgentEvent) => {
    const eventWithTimestamp = event as AgentEvent & { timestamp?: number }
    return {
      type: event.type,
      runId: event.runId ?? event.run_id ?? null,
      timestamp: eventWithTimestamp.timestamp ?? null,
      summary: formatAgentEvent(event),
    }
  }

  const diagnosticsPayload = createMemo(() =>
    JSON.stringify(
      {
        runId: currentRunId(),
        runState: runState(),
        isRunning: agent.isRunning(),
        progressMessage: progressMessage(),
        lastError: currentError(),
        eventCount: eventCount(),
        logDirectory: logDirectory(),
        backendLogFile: backendLogFilePath(),
        recentAgentEvents: recentEvents().map((event) => summarizeAgentEvent(event)),
        backendLogTail: backendLogTail() || null,
      },
      null,
      2
    )
  )

  const refreshBackendLogTail = async (): Promise<void> => {
    const refreshRequestId = ++latestBackendLogRefreshRequestId
    setIsRefreshingBackendLogs(true)
    const nextBackendLogTail = await readLatestBackendLogs(120).catch(
      () => '(failed to read backend logs)'
    )

    if (refreshRequestId === latestBackendLogRefreshRequestId) {
      setBackendLogTail(nextBackendLogTail)
      setIsRefreshingBackendLogs(false)
    }
  }

  createEffect(
    on(
      () => settings().devMode,
      (enabled) => {
        if (enabled) {
          void refreshBackendLogTail()
        }
      }
    )
  )

  // Auto-scroll
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
      const dist = scrollRef.scrollHeight - scrollRef.scrollTop - scrollRef.clientHeight
      setStickToBottom(dist < 16)
    })
  }
  onCleanup(() => {
    if (scrollRaf) cancelAnimationFrame(scrollRaf)
  })

  const copyText = async (text: string, onCopied: () => void) => {
    try {
      await navigator.clipboard.writeText(text)
      onCopied()
      return
    } catch {
      const ta = document.createElement('textarea')
      ta.value = text
      document.body.appendChild(ta)
      ta.select()
      document.execCommand('copy')
      document.body.removeChild(ta)
      onCopied()
    }
  }

  const handleCopy = async () => {
    const text = filteredLogs()
      .map((e) => `[${formatTime(e.timestamp)}] ${levelLabel[e.level]} ${e.message}`)
      .join('\n')
    await copyText(text, () => {
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    })
  }

  const handleCopyDiagnostics = async () => {
    await copyText(diagnosticsPayload(), () => {
      setCopiedDiagnostics(true)
      setTimeout(() => setCopiedDiagnostics(false), 1500)
    })
  }

  const logLevelOptions = ['debug', 'info', 'warn', 'error'] as const
  const logLevelDisplayMap: Record<string, string> = {
    debug: 'DEBUG',
    info: 'INFO',
    warn: 'WARN',
    error: 'ERROR',
  }

  // Close dropdown on outside click
  const handleOutsideClick = (_e: MouseEvent) => {
    if (showLevelDropdown()) setShowLevelDropdown(false)
  }

  return (
    // biome-ignore lint/a11y/useKeyWithClickEvents: dismiss dropdown on outside click
    // biome-ignore lint/a11y/noStaticElementInteractions: container dismisses dropdown
    <div
      style={{ display: 'flex', 'flex-direction': 'column', gap: SETTINGS_CARD_GAP }}
      onClick={handleOutsideClick}
    >
      {/* ===== Dev Mode Card ===== */}
      <div
        style={{
          background: '#111114',
          border: '1px solid #ffffff08',
          'border-radius': '12px',
          padding: '20px',
          display: 'flex',
          'flex-direction': 'column',
          gap: '16px',
        }}
      >
        {/* Card header */}
        <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
          <Code size={16} style={{ color: '#C8C8CC' }} />
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '14px',
                'font-weight': '500',
                color: '#F5F5F7',
              }}
            >
              Developer Mode
            </span>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#8E8E93',
              }}
            >
              Toggle developer console and configure log verbosity
            </span>
          </div>
        </div>

        {/* Enable developer console row - hidden when rendered inside AdvancedTab */}
        <Show when={merged.showToggle}>
          <div
            style={{
              display: 'flex',
              'align-items': 'center',
              'justify-content': 'space-between',
            }}
          >
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: '#C8C8CC',
                }}
              >
                Enable developer console
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#8E8E93',
                }}
              >
                Enables additional developer diagnostics and console output
              </span>
            </div>
            <button
              type="button"
              role="switch"
              aria-checked={settings().devMode ?? false}
              onClick={() => {
                const next = !(settings().devMode ?? false)
                updateSettings({ devMode: next })
                setDebugDevMode(next)
              }}
              style={{
                width: '44px',
                height: '24px',
                'border-radius': '12px',
                background: settings().devMode ? '#0A84FF' : '#2C2C2E',
                border: 'none',
                cursor: 'pointer',
                position: 'relative',
                'flex-shrink': '0',
                transition: 'background 0.15s',
              }}
              aria-label="Developer console"
            >
              <span
                style={{
                  position: 'absolute',
                  width: '20px',
                  height: '20px',
                  'border-radius': '50%',
                  background: '#FFFFFF',
                  top: '2px',
                  left: settings().devMode ? '22px' : '2px',
                  transition: 'left 0.15s',
                }}
              />
            </button>
          </div>
        </Show>

        {/* Log level row */}
        <div
          style={{
            display: 'flex',
            'align-items': 'center',
            'justify-content': 'space-between',
          }}
        >
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                color: '#C8C8CC',
              }}
            >
              Log level
            </span>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#8E8E93',
              }}
            >
              DEBUG shows middleware + verbose internals
            </span>
          </div>
          {/* Dropdown styled per Pencil design */}
          <div style={{ position: 'relative' }}>
            <button
              type="button"
              aria-haspopup="listbox"
              aria-expanded={showLevelDropdown()}
              aria-label={`Log level ${logLevelDisplayMap[settings().logLevel] ?? 'INFO'}`}
              onClick={(e) => {
                e.stopPropagation()
                setShowLevelDropdown(!showLevelDropdown())
              }}
              style={{
                display: 'flex',
                'align-items': 'center',
                gap: '6px',
                padding: '6px 12px',
                background: '#ffffff08',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                cursor: 'pointer',
              }}
            >
              <span
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '11px',
                  color: '#F5F5F7',
                }}
              >
                {logLevelDisplayMap[settings().logLevel] ?? 'INFO'}
              </span>
              <ChevronDown size={12} style={{ color: '#8E8E93' }} aria-hidden="true" />
            </button>
            <Show when={showLevelDropdown()}>
              <div
                style={{
                  position: 'absolute',
                  top: '100%',
                  right: '0',
                  'margin-top': '4px',
                  background: '#1C1C1E',
                  border: '1px solid #ffffff0a',
                  'border-radius': '8px',
                  overflow: 'hidden',
                  'z-index': '10',
                  'min-width': '100px',
                }}
              >
                <For each={logLevelOptions}>
                  {(level) => (
                    <button
                      type="button"
                      onClick={(e) => {
                        e.stopPropagation()
                        updateSettings({ logLevel: level })
                        setShowLevelDropdown(false)
                      }}
                      style={{
                        display: 'block',
                        width: '100%',
                        padding: '6px 12px',
                        background: settings().logLevel === level ? '#ffffff08' : 'transparent',
                        border: 'none',
                        cursor: 'pointer',
                        'text-align': 'left',
                        'font-family': 'Geist Mono, monospace',
                        'font-size': '11px',
                        color: settings().logLevel === level ? '#0A84FF' : '#C8C8CC',
                      }}
                    >
                      {logLevelDisplayMap[level]}
                    </button>
                  )}
                </For>
              </div>
            </Show>
          </div>
        </div>
      </div>

      {/* ===== Console Output Card ===== */}
      <Show when={settings().devMode}>
        <div
          style={{
            background: '#111114',
            border: '1px solid #ffffff08',
            'border-radius': '12px',
            padding: '20px',
            display: 'flex',
            'flex-direction': 'column',
            gap: '14px',
          }}
        >
          <div
            style={{ display: 'flex', 'align-items': 'center', 'justify-content': 'space-between' }}
          >
            <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
              <Code size={16} style={{ color: '#C8C8CC' }} />
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '14px',
                  'font-weight': '500',
                  color: '#F5F5F7',
                }}
              >
                Run Diagnostics
              </span>
            </div>
            <div style={{ display: 'flex', 'align-items': 'center', gap: '8px' }}>
              <button
                type="button"
                onClick={() => void refreshBackendLogTail()}
                style={{
                  display: 'inline-flex',
                  'align-items': 'center',
                  gap: '6px',
                  padding: '4px 8px',
                  background: 'transparent',
                  border: '1px solid #ffffff0a',
                  'border-radius': '6px',
                  cursor: 'pointer',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '11px',
                  color: '#8E8E93',
                }}
              >
                <RefreshCw size={12} classList={{ 'animate-spin': isRefreshingBackendLogs() }} />
                Refresh
              </button>
              <button
                type="button"
                onClick={() => void handleCopyDiagnostics()}
                style={{
                  display: 'inline-flex',
                  'align-items': 'center',
                  gap: '6px',
                  padding: '4px 8px',
                  background: 'transparent',
                  border: '1px solid #ffffff0a',
                  'border-radius': '6px',
                  cursor: 'pointer',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '11px',
                  color: '#8E8E93',
                }}
              >
                <Copy size={12} />
                {copiedDiagnostics() ? 'Copied!' : 'Copy Diagnostics'}
              </button>
            </div>
          </div>

          <div
            style={{
              display: 'grid',
              'grid-template-columns': 'repeat(2, minmax(0, 1fr))',
              gap: '10px',
            }}
          >
            <div
              style={{
                background: '#0A0A0C',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                padding: '10px',
              }}
            >
              <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '4px' }}>
                Current run
              </div>
              <div
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '11px',
                  color: '#F5F5F7',
                  'word-break': 'break-all',
                }}
              >
                {currentRunId() ?? 'none'}
              </div>
            </div>
            <div
              style={{
                background: '#0A0A0C',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                padding: '10px',
              }}
            >
              <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '4px' }}>
                Run state
              </div>
              <div
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '11px',
                  color: runState() === 'running' ? '#0A84FF' : '#C8C8CC',
                }}
              >
                {runState()}
              </div>
            </div>
            <div
              style={{
                background: '#0A0A0C',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                padding: '10px',
              }}
            >
              <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '4px' }}>
                Progress
              </div>
              <div
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '11px',
                  color: '#C8C8CC',
                  'word-break': 'break-word',
                }}
              >
                {progressMessage() ?? 'none'}
              </div>
            </div>
            <div
              style={{
                background: '#0A0A0C',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                padding: '10px',
              }}
            >
              <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '4px' }}>
                Last error
              </div>
              <div
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '11px',
                  color: currentError() ? '#FF453A' : '#C8C8CC',
                  'word-break': 'break-word',
                }}
              >
                {currentError() ?? 'none'}
              </div>
            </div>
          </div>

          <div
            style={{
              background: '#0A0A0C',
              border: '1px solid #ffffff0a',
              'border-radius': '8px',
              padding: '10px',
            }}
          >
            <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '4px' }}>
              Log directory
            </div>
            <div
              style={{
                'font-family': 'Geist Mono, monospace',
                'font-size': '11px',
                color: '#C8C8CC',
                'word-break': 'break-all',
              }}
            >
              {logDirectory()}
            </div>
          </div>

          <div
            style={{
              display: 'grid',
              'grid-template-columns': 'repeat(2, minmax(0, 1fr))',
              gap: '10px',
            }}
          >
            <div
              style={{
                background: '#0A0A0C',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                padding: '10px',
                'min-height': '180px',
              }}
            >
              <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '8px' }}>
                Recent agent events ({eventCount()})
              </div>
              <Show
                when={recentEvents().length > 0}
                fallback={
                  <div style={{ 'font-size': '10px', color: '#8E8E93' }}>
                    No agent events captured yet.
                  </div>
                }
              >
                <div
                  style={{
                    display: 'flex',
                    'flex-direction': 'column',
                    gap: '4px',
                    'max-height': '220px',
                    overflow: 'auto',
                  }}
                >
                  <For each={recentEvents()}>
                    {(event: AgentEvent) => (
                      <div
                        style={{
                          'font-family': 'Geist Mono, monospace',
                          'font-size': '10px',
                          color: '#C8C8CC',
                          'white-space': 'pre-wrap',
                          'word-break': 'break-word',
                        }}
                      >
                        {formatAgentEvent(event)}
                      </div>
                    )}
                  </For>
                </div>
              </Show>
            </div>

            <div
              style={{
                background: '#0A0A0C',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                padding: '10px',
                'min-height': '180px',
              }}
            >
              <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '8px' }}>
                Latest backend log tail
              </div>
              <div
                style={{
                  'max-height': '220px',
                  overflow: 'auto',
                }}
              >
                <pre
                  style={{
                    margin: '0',
                    padding: '0',
                    background: 'transparent',
                    'font-family': 'Geist Mono, monospace',
                    'font-size': '10px',
                    color: '#C8C8CC',
                    overflow: 'visible',
                    'white-space': 'pre-wrap',
                    'word-break': 'break-word',
                  }}
                >
                  {backendLogTail() || '(no backend log data loaded)'}
                </pre>
              </div>
            </div>
          </div>

          <div
            style={{
              background: '#0A0A0C',
              border: '1px solid #ffffff0a',
              'border-radius': '8px',
              padding: '10px',
            }}
          >
            <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '4px' }}>
              Backend log file
            </div>
            <div
              style={{
                'font-family': 'Geist Mono, monospace',
                'font-size': '11px',
                color: '#C8C8CC',
                'word-break': 'break-all',
              }}
            >
              {backendLogFilePath()}
            </div>
          </div>

          <div
            style={{
              background: '#0A0A0C',
              border: '1px solid #ffffff0a',
              'border-radius': '8px',
              padding: '10px',
            }}
          >
            <div style={{ 'font-size': '11px', color: '#8E8E93', 'margin-bottom': '8px' }}>
              Diagnostics payload
            </div>
            <div
              style={{
                'max-height': '180px',
                overflow: 'auto',
              }}
            >
              <pre
                style={{
                  margin: '0',
                  padding: '0',
                  background: 'transparent',
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '10px',
                  color: '#C8C8CC',
                  overflow: 'visible',
                  'white-space': 'pre-wrap',
                  'word-break': 'break-word',
                }}
              >
                {diagnosticsPayload()}
              </pre>
            </div>
          </div>
        </div>

        <div
          style={{
            background: '#111114',
            border: '1px solid #ffffff08',
            'border-radius': '12px',
            padding: '20px',
            display: 'flex',
            'flex-direction': 'column',
            gap: '12px',
          }}
        >
          {/* Card header with buttons */}
          <div
            style={{
              display: 'flex',
              'align-items': 'center',
              'justify-content': 'space-between',
            }}
          >
            <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
              <Terminal size={16} style={{ color: '#C8C8CC' }} />
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '14px',
                  'font-weight': '500',
                  color: '#F5F5F7',
                }}
              >
                Console Output
              </span>
            </div>
            <div style={{ display: 'flex', 'align-items': 'center', gap: '8px' }}>
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation()
                  void handleCopy()
                }}
                style={{
                  padding: '4px 8px',
                  background: 'transparent',
                  border: '1px solid #ffffff0a',
                  'border-radius': '6px',
                  cursor: 'pointer',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '11px',
                  color: '#8E8E93',
                }}
              >
                {copied() ? 'Copied!' : 'Copy All'}
              </button>
              <button
                type="button"
                onClick={(e) => {
                  e.stopPropagation()
                  clearDevLogs()
                }}
                style={{
                  padding: '4px 8px',
                  background: 'transparent',
                  border: '1px solid #ffffff0a',
                  'border-radius': '6px',
                  cursor: 'pointer',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '11px',
                  color: '#8E8E93',
                }}
              >
                Clear
              </button>
            </div>
          </div>

          {/* Log viewer */}
          <div
            ref={scrollRef}
            onScroll={handleLogScroll}
            style={{
              background: '#0A0A0C',
              border: '1px solid #ffffff0a',
              'border-radius': '8px',
              height: '200px',
              overflow: 'auto',
              padding: '8px',
              display: 'flex',
              'flex-direction': 'column',
              gap: '2px',
            }}
          >
            <Show
              when={filteredLogs().length > 0}
              fallback={
                <div
                  style={{
                    display: 'flex',
                    'align-items': 'center',
                    'justify-content': 'center',
                    height: '100%',
                    'font-family': 'Geist Mono, monospace',
                    'font-size': '11px',
                    color: '#8E8E93',
                  }}
                >
                  No log entries yet.
                </div>
              }
            >
              <For each={filteredLogs()}>
                {(entry) => {
                  const color = () => PENCIL_LEVEL_COLORS[entry.level] ?? '#C8C8CC'
                  return (
                    <div
                      style={{
                        display: 'flex',
                        'align-items': 'center',
                        gap: '8px',
                      }}
                    >
                      <span
                        style={{
                          'font-family': 'Geist Mono, monospace',
                          'font-size': '10px',
                          color: '#8E8E93',
                          'flex-shrink': '0',
                        }}
                      >
                        {formatTime(entry.timestamp).slice(0, 8)}
                      </span>
                      <span
                        style={{
                          'font-family': 'Geist Mono, monospace',
                          'font-size': '10px',
                          'font-weight': '600',
                          color: color(),
                          'flex-shrink': '0',
                          width: '24px',
                        }}
                      >
                        {levelLabel[entry.level]}
                      </span>
                      <span
                        style={{
                          'font-family': 'Geist Mono, monospace',
                          'font-size': '10px',
                          color:
                            entry.level === 'error' || entry.level === 'warn' ? color() : '#C8C8CC',
                          'word-break': 'break-all',
                          'white-space': 'pre-wrap',
                        }}
                      >
                        {entry.message}
                      </span>
                    </div>
                  )
                }}
              </For>
            </Show>
          </div>

          {/* Tip */}
          <span
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '11px',
              color: '#8E8E93',
            }}
          >
            Tip: Copy all logs and paste them when reporting issues.
          </span>
        </div>
      </Show>
    </div>
  )
}
