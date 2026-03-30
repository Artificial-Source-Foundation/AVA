/**
 * Developer Settings Tab — Pencil design revamp
 *
 * Two cards:
 * 1. Developer Mode — toggle + log level dropdown
 * 2. Console Output — colored log lines with Copy All / Clear buttons
 */

import { ChevronDown, Code, Terminal } from 'lucide-solid'
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
import { clearDevLogs, getDevLogs } from '../../../services/dev-console'
import { useSettings } from '../../../stores/settings'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { formatTime, levelLabel } from './developer/dev-helpers'

/** Log level colors matching the Pencil design */
const PENCIL_LEVEL_COLORS: Record<string, string> = {
  log: '#C8C8CC',
  info: '#0A84FF',
  warn: '#F5A623',
  error: '#FF453A',
}

export const DeveloperTab: Component = () => {
  const { settings, updateSettings } = useSettings()
  const logs = getDevLogs()
  const [copied, setCopied] = createSignal(false)
  const [stickToBottom, setStickToBottom] = createSignal(true)
  const [showLevelDropdown, setShowLevelDropdown] = createSignal(false)
  let scrollRef: HTMLDivElement | undefined

  const filteredLogs = createMemo(() => logs())

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

  const handleCopy = async () => {
    const text = filteredLogs()
      .map((e) => `[${formatTime(e.timestamp)}] ${levelLabel[e.level]} ${e.message}`)
      .join('\n')
    try {
      await navigator.clipboard.writeText(text)
      setCopied(true)
      setTimeout(() => setCopied(false), 1500)
    } catch {
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
      {/* Page title */}
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
        }}
      >
        Developer
      </h1>

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
                color: '#48484A',
              }}
            >
              Toggle developer console and configure log verbosity
            </span>
          </div>
        </div>

        {/* Enable developer console row */}
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
                color: '#48484A',
              }}
            >
              Controls Developer tab visibility in settings
            </span>
          </div>
          <button
            type="button"
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
            aria-label="Toggle developer console"
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
                color: '#48484A',
              }}
            >
              DEBUG shows middleware + verbose internals
            </span>
          </div>
          {/* Dropdown styled per Pencil design */}
          <div style={{ position: 'relative' }}>
            <button
              type="button"
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
              <ChevronDown size={12} style={{ color: '#48484A' }} />
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
                  color: '#48484A',
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
                  color: '#48484A',
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
                    color: '#48484A',
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
                          color: '#48484A',
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
              color: '#48484A',
            }}
          >
            Tip: Copy all logs and paste them when reporting issues.
          </span>
        </div>
      </Show>
    </div>
  )
}
