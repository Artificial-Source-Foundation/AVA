/**
 * Node.js PTY (Pseudo-Terminal) Implementation
 * Uses node-pty for interactive command support
 *
 * Based on patterns from:
 * - Gemini CLI: Tries @lydell/node-pty first, falls back to node-pty
 * - OpenCode: Lazy loading, buffer management, session lifecycle
 */

import type { IPTY, PTYOptions, PTYProcess } from '@estela/core'

/** PTY implementation info */
export interface PTYImplementation {
  module: typeof import('node-pty')
  name: 'lydell-node-pty' | 'node-pty'
}

// Dynamic import for node-pty to handle cases where it's not installed
let ptyImpl: PTYImplementation | null = null
let ptyLoadError: Error | null = null

/**
 * Try to load PTY module - attempts @lydell/node-pty first (better maintained fork),
 * then falls back to node-pty
 */
async function loadPty(): Promise<PTYImplementation | null> {
  if (ptyImpl !== null) return ptyImpl
  if (ptyLoadError !== null) return null

  // Try @lydell/node-pty first (better maintained fork)
  try {
    const lydell = '@lydell/node-pty'
    const module = await import(lydell)
    ptyImpl = { module, name: 'lydell-node-pty' }
    return ptyImpl
  } catch {
    // Fall back to node-pty
    try {
      const nodePty = 'node-pty'
      const module = await import(nodePty)
      ptyImpl = { module, name: 'node-pty' }
      return ptyImpl
    } catch (error) {
      ptyLoadError = error as Error
      return null
    }
  }
}

// Synchronous check - only returns true if already loaded
function isPtyLoaded(): boolean {
  return ptyImpl !== null
}

/** Default terminal dimensions */
const DEFAULT_COLS = 80
const DEFAULT_ROWS = 24

/** Grace period before SIGKILL escalation (ms) */
const SIGKILL_GRACE_MS = 3000

/** Buffer limit for storing output when no subscribers (2MB like OpenCode) */
const BUFFER_LIMIT = 1024 * 1024 * 2

/**
 * Wrapper around node-pty process to implement PTYProcess interface
 * Based on OpenCode's session management pattern
 */
class NodePTYProcess implements PTYProcess {
  private ptyProcess: import('node-pty').IPty
  private dataCallbacks: Array<(data: string) => void> = []
  private exitCallbacks: Array<(code: number, signal?: number) => void> = []
  private exitPromise: Promise<{ exitCode: number; signal?: number }>
  private isRunning = true
  /** Output buffer for replaying to new subscribers */
  private buffer = ''

  constructor(ptyProcess: import('node-pty').IPty) {
    this.ptyProcess = ptyProcess

    // Set up data forwarding with buffering
    this.ptyProcess.onData((data) => {
      // If we have callbacks, send directly
      if (this.dataCallbacks.length > 0) {
        for (const cb of this.dataCallbacks) {
          cb(data)
        }
      } else {
        // Buffer output for later replay
        this.buffer += data
        if (this.buffer.length > BUFFER_LIMIT) {
          this.buffer = this.buffer.slice(-BUFFER_LIMIT)
        }
      }
    })

    // Set up exit handling
    this.exitPromise = new Promise((resolve) => {
      this.ptyProcess.onExit(({ exitCode, signal }) => {
        this.isRunning = false
        const signalNum = signal ?? undefined
        for (const cb of this.exitCallbacks) {
          cb(exitCode, signalNum)
        }
        resolve({ exitCode, signal: signalNum })
      })
    })
  }

  get pid(): number {
    return this.ptyProcess.pid
  }

  onData(callback: (data: string) => void): void {
    // Replay buffered content to new subscriber
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
    if (this.isRunning) {
      this.ptyProcess.write(data)
    }
  }

  resize(cols: number, rows: number): void {
    if (this.isRunning) {
      this.ptyProcess.resize(cols, rows)
    }
  }

  kill(signal?: string): void {
    if (!this.isRunning) return

    // node-pty doesn't support signals directly, use process.kill
    const sig = signal as NodeJS.Signals | undefined
    try {
      process.kill(this.ptyProcess.pid, sig ?? 'SIGTERM')
    } catch {
      // Process might already be dead
    }

    // Schedule SIGKILL escalation if needed
    setTimeout(() => {
      if (this.isRunning) {
        try {
          process.kill(this.ptyProcess.pid, 'SIGKILL')
        } catch {
          // Process might already be dead
        }
      }
    }, SIGKILL_GRACE_MS)
  }

  async wait(): Promise<{ exitCode: number; signal?: number }> {
    return this.exitPromise
  }
}

/**
 * Node.js PTY implementation using node-pty
 * Tries @lydell/node-pty first, falls back to node-pty
 */
export class NodePTY implements IPTY {
  private initialized = false
  private supported = false
  private implName: string | null = null

  /**
   * Initialize PTY support (attempts to load node-pty)
   */
  async initialize(): Promise<boolean> {
    if (this.initialized) return this.supported

    const impl = await loadPty()
    this.supported = impl !== null
    this.implName = impl?.name ?? null
    this.initialized = true
    return this.supported
  }

  isSupported(): boolean {
    // If not initialized, do a synchronous check
    if (!this.initialized) {
      return isPtyLoaded()
    }
    return this.supported
  }

  /** Get the name of the PTY implementation being used */
  getImplementationName(): string | null {
    return this.implName
  }

  spawn(command: string, args: string[], options?: PTYOptions): PTYProcess {
    if (!ptyImpl) {
      throw new Error('PTY not available. Call initialize() first or ensure node-pty is installed.')
    }

    const shell = process.platform === 'win32' ? 'cmd.exe' : '/bin/bash'
    const shellArgs =
      process.platform === 'win32'
        ? ['/c', command, ...args]
        : ['-c', `${command} ${args.join(' ')}`]

    // Add login shell flag for Unix shells (like OpenCode does)
    if (shell.endsWith('sh') && !shellArgs.includes('-l')) {
      shellArgs.unshift('-l')
    }

    const ptyProcess = ptyImpl.module.spawn(shell, shellArgs, {
      name: 'xterm-256color',
      cols: options?.cols ?? DEFAULT_COLS,
      rows: options?.rows ?? DEFAULT_ROWS,
      cwd: options?.cwd ?? process.cwd(),
      env: {
        ...(process.env as Record<string, string>),
        ...options?.env,
        TERM: 'xterm-256color',
        ESTELA_TERMINAL: '1', // Mark as Estela terminal (like OpenCode's OPENCODE_TERMINAL)
      },
    })

    return new NodePTYProcess(ptyProcess)
  }
}

/**
 * Create and initialize a NodePTY instance
 * Returns null if PTY is not supported
 */
export async function createNodePTY(): Promise<NodePTY | null> {
  const pty = new NodePTY()
  const supported = await pty.initialize()
  return supported ? pty : null
}

/** Get which PTY implementation was loaded (for debugging) */
export function getPTYImplementationName(): string | null {
  return ptyImpl?.name ?? null
}
