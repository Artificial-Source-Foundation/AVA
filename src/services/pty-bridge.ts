/**
 * PTY Bridge
 *
 * TypeScript wrappers around Tauri PTY commands.
 * Handles IPC invocations and event subscriptions.
 */

import { invoke } from '@tauri-apps/api/core'
import { listen, type UnlistenFn } from '@tauri-apps/api/event'

export interface PtySpawnOptions {
  id: string
  cols: number
  rows: number
  cwd?: string
}

export interface PtySession {
  id: string
  unlistenOutput: UnlistenFn
  unlistenExit: UnlistenFn
}

/**
 * Spawn a new PTY session. Registers event listeners BEFORE calling spawn
 * to avoid missing initial output.
 */
export async function spawnPty(
  opts: PtySpawnOptions,
  onOutput: (data: string) => void,
  onExit: (code: number) => void
): Promise<PtySession> {
  // Register listeners first
  const unlistenOutput = await listen<string>(`pty-output-${opts.id}`, (event) => {
    onOutput(event.payload)
  })

  const unlistenExit = await listen<number>(`pty-exit-${opts.id}`, (event) => {
    onExit(event.payload)
  })

  // Now spawn the PTY
  try {
    await invoke('pty_spawn', {
      id: opts.id,
      cols: opts.cols,
      rows: opts.rows,
      cwd: opts.cwd,
    })
  } catch (err) {
    // Clean up listeners if spawn fails
    unlistenOutput()
    unlistenExit()
    throw err
  }

  return {
    id: opts.id,
    unlistenOutput,
    unlistenExit,
  }
}

/** Write keystrokes to a PTY session */
export async function writePty(id: string, data: string): Promise<void> {
  await invoke('pty_write', { id, data })
}

/** Resize a PTY session */
export async function resizePty(id: string, cols: number, rows: number): Promise<void> {
  await invoke('pty_resize', { id, cols, rows })
}

/** Kill a PTY session */
export async function killPty(id: string): Promise<void> {
  await invoke('pty_kill', { id })
}

/** Clean up a PTY session (kill + unlisten) */
export async function cleanupPty(session: PtySession): Promise<void> {
  session.unlistenOutput()
  session.unlistenExit()
  try {
    await killPty(session.id)
  } catch {
    /* already dead */
  }
}
