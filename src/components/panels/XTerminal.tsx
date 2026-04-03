/**
 * XTerminal Component
 *
 * Full interactive terminal powered by xterm.js + Tauri PTY backend.
 * Handles lifecycle (spawn, write, resize, cleanup), theming, and
 * passthrough of app shortcuts.
 */

import { isTauri } from '@tauri-apps/api/core'
import type { IResizeEvent } from '@xterm/xterm'
import { type Component, onCleanup, onMount } from 'solid-js'
import {
  cleanupPty,
  type PtySession,
  resizePty,
  spawnPty,
  writePty,
} from '../../services/pty-bridge'
import { useProject } from '../../stores/project'
import { useTerminal } from '../../stores/terminal'

// ============================================================================
// Theme (matches AVA dark tokens)
// ============================================================================

const TERMINAL_THEME = {
  background: '#0a0a0c',
  foreground: '#e4e4e7',
  cursor: '#a78bfa',
  cursorAccent: '#0a0a0c',
  selectionBackground: '#a78bfa33',
  selectionForeground: '#e4e4e7',
  black: '#18181b',
  red: '#f87171',
  green: '#4ade80',
  yellow: '#facc15',
  blue: '#60a5fa',
  magenta: '#c084fc',
  cyan: '#22d3ee',
  white: '#e4e4e7',
  brightBlack: '#3f3f46',
  brightRed: '#fca5a5',
  brightGreen: '#86efac',
  brightYellow: '#fde68a',
  brightBlue: '#93c5fd',
  brightMagenta: '#d8b4fe',
  brightCyan: '#67e8f9',
  brightWhite: '#fafafa',
}

// Shortcuts that should pass through to the app (not consumed by terminal)
const APP_SHORTCUT_KEYS = new Set(['b', ',', 'm', 'n', 'k', 'e', 'j', 'o', '`'])

// ============================================================================
// Component
// ============================================================================

export const XTerminal: Component = () => {
  let containerRef: HTMLDivElement | undefined
  let ptySession: PtySession | null = null

  const { currentProject } = useProject()
  const { setTerminalStatus, setTerminalSession, setTerminalExitCode } = useTerminal()

  onMount(async () => {
    if (!containerRef || !isTauri()) return

    setTerminalStatus('connecting')

    // Dynamic imports for xterm (large bundle, code-split)
    const [{ Terminal }, { FitAddon }, { WebLinksAddon }] = await Promise.all([
      import('@xterm/xterm'),
      import('@xterm/addon-fit'),
      import('@xterm/addon-web-links'),
    ])

    const terminal = new Terminal({
      theme: TERMINAL_THEME,
      fontFamily: "'JetBrains Mono', 'Fira Code', 'Cascadia Code', monospace",
      fontSize: 13,
      lineHeight: 1.3,
      cursorBlink: true,
      cursorStyle: 'bar',
      scrollback: 5000,
      allowProposedApi: true,
    })

    const fitAddon = new FitAddon()
    terminal.loadAddon(fitAddon)
    terminal.loadAddon(new WebLinksAddon())

    // Try WebGL addon (GPU-accelerated rendering)
    try {
      const { WebglAddon } = await import('@xterm/addon-webgl')
      terminal.loadAddon(new WebglAddon())
    } catch {
      /* fallback to canvas renderer */
    }

    terminal.open(containerRef)
    fitAddon.fit()

    // Pass through app shortcuts
    terminal.attachCustomKeyEventHandler((event: KeyboardEvent) => {
      if (event.ctrlKey || event.metaKey) {
        let key = event.key.toLowerCase()
        // Ctrl remaps some letters to control-char names (Ctrl+M → "Enter", etc.)
        // Recover the real letter from event.code.
        if (event.code?.startsWith('Key')) key = event.code.slice(3).toLowerCase()
        if (APP_SHORTCUT_KEYS.has(key)) return false // Let the app handle it
      }
      return true // Terminal handles it
    })

    // Spawn PTY
    const sessionId = `pty-${Date.now()}`
    const cwd = currentProject()?.directory

    try {
      ptySession = await spawnPty(
        {
          id: sessionId,
          cols: terminal.cols,
          rows: terminal.rows,
          cwd: cwd && cwd !== '~' ? cwd : undefined,
        },
        // onOutput: PTY → terminal
        (data: string) => {
          terminal.write(data)
        },
        // onExit
        (code) => {
          setTerminalStatus('exited')
          setTerminalExitCode(code)
          terminal.write(`\r\n\x1b[90m[Process exited with code ${code}]\x1b[0m\r\n`)
        }
      )

      setTerminalSession(ptySession)
      setTerminalStatus('connected')
    } catch (err) {
      setTerminalStatus('exited')
      terminal.write(
        `\r\n\x1b[31mFailed to spawn PTY: ${err instanceof Error ? err.message : String(err)}\x1b[0m\r\n`
      )
    }

    // terminal → PTY (keystrokes)
    const dataDisposable = terminal.onData((data: string) => {
      if (ptySession) {
        void writePty(ptySession.id, data)
      }
    })

    // Terminal resize → PTY resize
    const resizeDisposable = terminal.onResize(({ cols, rows }: IResizeEvent) => {
      if (ptySession) {
        void resizePty(ptySession.id, cols, rows)
      }
    })

    // Container resize → fit addon
    const resizeObserver = new ResizeObserver(() => {
      fitAddon.fit()
    })
    resizeObserver.observe(containerRef)

    // Cleanup
    onCleanup(() => {
      resizeObserver.disconnect()
      dataDisposable.dispose()
      resizeDisposable.dispose()
      terminal.dispose()

      if (ptySession) {
        void cleanupPty(ptySession)
        ptySession = null
        setTerminalSession(null)
        setTerminalStatus('idle')
      }
    })
  })

  return (
    <div
      ref={containerRef}
      class="w-full h-full xterm-container"
      style={{ padding: '4px', background: TERMINAL_THEME.background }}
    />
  )
}
