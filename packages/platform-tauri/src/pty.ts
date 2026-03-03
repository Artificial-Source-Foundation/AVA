/**
 * Tauri PTY (Pseudo-Terminal) Implementation
 *
 * Uses Tauri's shell Command API to spawn interactive commands.
 * Dynamically imports @tauri-apps/plugin-shell so this module
 * can be imported in environments where Tauri APIs are unavailable.
 */

import type { IPTY, PTYOptions, PTYProcess } from '@ava/core-v2'

/** Default terminal dimensions */
const DEFAULT_COLS = 80
const DEFAULT_ROWS = 24

/** Grace period before force-kill escalation (ms) */
const KILL_GRACE_MS = 3000

/**
 * Wrapper around a Tauri Command child process implementing PTYProcess.
 *
 * Tauri does not expose a true PTY, so this emulates the interface
 * using `@tauri-apps/plugin-shell` Command.create with sidecar: false.
 */
class TauriPTYProcess implements PTYProcess {
  private child: { write: (data: string) => Promise<void>; kill: () => Promise<void> } | null = null
  private dataCallbacks: Array<(data: string) => void> = []
  private exitCallbacks: Array<(code: number, signal?: number) => void> = []
  private resolvedPid = 0
  private running = false
  private exitPromise: Promise<{ exitCode: number; signal?: number }>
  private resolveExit!: (value: { exitCode: number; signal?: number }) => void
  private buffer = ''
  private readonly bufferLimit = 1024 * 1024 * 2

  constructor() {
    this.exitPromise = new Promise((resolve) => {
      this.resolveExit = resolve
    })
  }

  get pid(): number {
    return this.resolvedPid
  }

  /** Bootstrap the process. Called by TauriPTY.spawn(). */
  async start(command: string, args: string[], options?: PTYOptions): Promise<void> {
    const { Command } = await import('@tauri-apps/plugin-shell')

    // Use login shell for better compatibility with interactive commands
    const shellArgs = ['-l', '-c', `${command} ${args.join(' ')}`]

    // Set up environment with terminal markers
    const env = {
      ...options?.env,
      TERM: 'xterm-256color',
      AVA_TERMINAL: '1',
      COLUMNS: String(options?.cols ?? DEFAULT_COLS),
      LINES: String(options?.rows ?? DEFAULT_ROWS),
    }

    const cmd = Command.create('sh', shellArgs, {
      cwd: options?.cwd,
      env,
    })

    // Wire stdout/stderr into dataCallbacks (Tauri emits line events)
    cmd.stdout.on('data', (line: string) => this.handleData(`${line}\n`))
    cmd.stderr.on('data', (line: string) => this.handleData(`${line}\n`))

    cmd.on('close', (data: { code: number | null; signal?: number | null }) => {
      this.running = false
      const code = data.code ?? 1
      const signal = data.signal ?? undefined
      for (const cb of this.exitCallbacks) cb(code, signal)
      this.resolveExit({ exitCode: code, signal })
    })

    const child = await cmd.spawn()
    this.child = child as unknown as {
      write: (data: string) => Promise<void>
      kill: () => Promise<void>
    }
    this.resolvedPid = (child as unknown as { pid?: number }).pid ?? 0
    this.running = true
  }

  onData(callback: (data: string) => void): void {
    if (this.buffer.length > 0) {
      callback(this.buffer)
      this.buffer = ''
    }
    this.dataCallbacks.push(callback)
  }

  onExit(callback: (code: number, signal?: number) => void): void {
    this.exitCallbacks.push(callback)
  }

  write(data: string): void {
    if (this.running && this.child) {
      void this.child.write(data)
    }
  }

  resize(_cols: number, _rows: number): void {
    // Tauri's shell API does not support terminal resize.
    // This is a no-op — the terminal runs at the default size.
  }

  kill(_signal?: string): void {
    if (!this.running || !this.child) return

    void this.child.kill()

    // Escalate after grace period if still running
    setTimeout(() => {
      if (this.running && this.child) {
        void this.child.kill()
      }
    }, KILL_GRACE_MS)
  }

  async wait(): Promise<{ exitCode: number; signal?: number }> {
    return this.exitPromise
  }

  private handleData(data: string): void {
    if (this.dataCallbacks.length > 0) {
      for (const cb of this.dataCallbacks) cb(data)
    } else {
      this.buffer += data
      if (this.buffer.length > this.bufferLimit) {
        this.buffer = this.buffer.slice(-this.bufferLimit)
      }
    }
  }
}

/**
 * Tauri PTY implementation.
 *
 * Wraps Tauri shell commands to provide a PTYProcess-compatible interface.
 * Note: Tauri does not expose a true PTY (no resize, no raw terminal mode).
 * Interactive commands that require a real terminal may not work correctly.
 */
export class TauriPTY implements IPTY {
  private supported: boolean | null = null

  isSupported(): boolean {
    if (this.supported !== null) return this.supported

    // Probe availability by checking if we are in a Tauri context
    try {
      // window.__TAURI__ is set by the Tauri runtime
      this.supported = typeof window !== 'undefined' && '__TAURI__' in window
    } catch {
      this.supported = false
    }
    return this.supported
  }

  spawn(command: string, args: string[], options?: PTYOptions): PTYProcess {
    const proc = new TauriPTYProcess()

    // Start is async but PTYProcess.spawn is sync — fire and forget.
    // Callers interact via onData/onExit/write which buffer until ready.
    void proc.start(command, args, {
      cols: options?.cols ?? DEFAULT_COLS,
      rows: options?.rows ?? DEFAULT_ROWS,
      cwd: options?.cwd,
      env: options?.env,
    })

    return proc
  }
}
