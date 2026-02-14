/**
 * ACP Terminal Bridge
 *
 * Routes shell command execution through the editor's integrated terminal
 * when available, with fallback to local shell execution.
 *
 * The bridge uses ACP JSON-RPC to:
 * 1. Create a terminal in the editor
 * 2. Write command to it
 * 3. Wait for exit code
 * 4. Optionally kill the process
 */

import type { AcpTerminalCapabilities, AcpTerminalResult, AcpTransport } from './types.js'
import { AcpError, AcpErrorCode } from './types.js'

// ============================================================================
// Constants
// ============================================================================

/** Default timeout for terminal commands (5 minutes) */
const DEFAULT_TIMEOUT_MS = 5 * 60 * 1000

/** Terminal name prefix */
const TERMINAL_PREFIX = 'AVA'

// ============================================================================
// ACP Terminal Bridge
// ============================================================================

/**
 * Routes bash tool execution through the editor's terminal.
 *
 * When the editor supports terminal operations, commands run inside
 * the editor's integrated terminal (visible to the user). Otherwise,
 * falls back to local shell execution via the platform layer.
 */
export class AcpTerminalBridge {
  private transport: AcpTransport | null = null
  private capabilities: AcpTerminalCapabilities | null = null
  private activeTerminals = new Map<string, { command: string; startedAt: number }>()
  private terminalCounter = 0

  // ==========================================================================
  // Configuration
  // ==========================================================================

  /**
   * Set the ACP transport for communicating with the editor
   */
  setTransport(transport: AcpTransport): void {
    this.transport = transport
  }

  /**
   * Set terminal capabilities reported by the editor
   */
  setCapabilities(capabilities: AcpTerminalCapabilities): void {
    this.capabilities = capabilities
  }

  /**
   * Check if the editor supports terminal operations
   */
  isAvailable(): boolean {
    return (
      this.transport !== null &&
      this.capabilities !== null &&
      this.capabilities.createTerminal &&
      this.capabilities.writeTerminal
    )
  }

  // ==========================================================================
  // Terminal Operations
  // ==========================================================================

  /**
   * Execute a command through the editor's terminal.
   *
   * @param command - Shell command to execute
   * @param cwd - Working directory
   * @param timeoutMs - Timeout in milliseconds
   * @returns Terminal result with exit code and output
   * @throws AcpError if terminal is unavailable
   */
  async execute(
    command: string,
    cwd: string,
    timeoutMs = DEFAULT_TIMEOUT_MS
  ): Promise<AcpTerminalResult> {
    if (!this.isAvailable()) {
      throw new AcpError(AcpErrorCode.TERMINAL_UNAVAILABLE, 'Editor terminal is not available')
    }

    const transport = this.transport!
    this.terminalCounter++
    const name = `${TERMINAL_PREFIX} #${this.terminalCounter}`

    // Create terminal in editor
    const terminalId = await transport.request<string>('terminal/create', {
      name,
      cwd,
    })

    this.activeTerminals.set(terminalId, {
      command,
      startedAt: Date.now(),
    })

    try {
      // Write command to terminal
      await transport.request('terminal/write', {
        terminalId,
        data: `${command}\n`,
      })

      // Wait for command to complete with timeout
      const result = await this.waitForExit(terminalId, timeoutMs)
      return result
    } catch (error) {
      // Try to kill the terminal on error
      await this.tryKill(terminalId)
      throw error
    } finally {
      this.activeTerminals.delete(terminalId)
    }
  }

  /**
   * Kill an active terminal process
   */
  async kill(terminalId: string): Promise<void> {
    if (!this.transport || !this.capabilities?.killTerminal) return
    await this.tryKill(terminalId)
  }

  /**
   * Kill all active terminals
   */
  async killAll(): Promise<void> {
    const ids = Array.from(this.activeTerminals.keys())
    await Promise.all(ids.map((id) => this.kill(id)))
  }

  /**
   * Get count of active terminals
   */
  getActiveCount(): number {
    return this.activeTerminals.size
  }

  // ==========================================================================
  // Private Helpers
  // ==========================================================================

  /**
   * Wait for a terminal process to exit
   */
  private async waitForExit(terminalId: string, timeoutMs: number): Promise<AcpTerminalResult> {
    const transport = this.transport!

    if (this.capabilities?.waitForExit) {
      // Editor supports waiting for exit
      const result = await Promise.race([
        transport.request<AcpTerminalResult>('terminal/waitForExit', {
          terminalId,
        }),
        this.timeout(timeoutMs, terminalId),
      ])
      return result
    }

    // Fallback: poll for completion (editor doesn't support waitForExit)
    return this.pollForExit(terminalId, timeoutMs)
  }

  /**
   * Poll-based exit detection for editors without waitForExit
   */
  private async pollForExit(terminalId: string, timeoutMs: number): Promise<AcpTerminalResult> {
    const transport = this.transport!
    const startTime = Date.now()
    const pollInterval = 500 // 500ms between polls

    while (Date.now() - startTime < timeoutMs) {
      try {
        const status = await transport.request<{
          running: boolean
          exitCode?: number
          output?: string
        }>('terminal/status', { terminalId })

        if (!status.running) {
          return {
            exitCode: status.exitCode ?? 0,
            output: status.output ?? '',
            killed: false,
          }
        }
      } catch {
        // Terminal may have been destroyed
        return { exitCode: 1, output: '', killed: false }
      }

      await new Promise((r) => setTimeout(r, pollInterval))
    }

    // Timeout reached
    await this.tryKill(terminalId)
    return { exitCode: 1, output: 'Command timed out', killed: true }
  }

  /**
   * Timeout helper that kills the terminal
   */
  private async timeout(ms: number, terminalId: string): Promise<AcpTerminalResult> {
    return new Promise((resolve) => {
      setTimeout(async () => {
        await this.tryKill(terminalId)
        resolve({ exitCode: 1, output: 'Command timed out', killed: true })
      }, ms)
    })
  }

  /**
   * Try to kill a terminal, ignoring errors
   */
  private async tryKill(terminalId: string): Promise<void> {
    try {
      if (this.transport && this.capabilities?.killTerminal) {
        await this.transport.request('terminal/kill', { terminalId })
      }
    } catch {
      // Ignore kill errors (terminal may already be dead)
    }
  }
}

// ============================================================================
// Factory
// ============================================================================

/**
 * Create an ACP terminal bridge
 */
export function createAcpTerminalBridge(): AcpTerminalBridge {
  return new AcpTerminalBridge()
}
