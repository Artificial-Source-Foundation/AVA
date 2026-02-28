/**
 * Terminal Store
 *
 * Reactive state for the integrated terminal (xterm.js + PTY).
 */

import { createSignal } from 'solid-js'
import type { PtySession } from '../services/pty-bridge'

export type TerminalStatus = 'idle' | 'connecting' | 'connected' | 'exited'

const [terminalStatus, setTerminalStatus] = createSignal<TerminalStatus>('idle')
const [terminalSession, setTerminalSession] = createSignal<PtySession | null>(null)
const [terminalExitCode, setTerminalExitCode] = createSignal<number | null>(null)

export function useTerminal() {
  return {
    terminalStatus,
    setTerminalStatus,
    terminalSession,
    setTerminalSession,
    terminalExitCode,
    setTerminalExitCode,
  }
}
