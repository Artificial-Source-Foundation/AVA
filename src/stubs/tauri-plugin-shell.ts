/**
 * Stub: @tauri-apps/plugin-shell
 */

type EventCallback = (data: unknown) => void

class MockEmitter {
  private handlers: Record<string, EventCallback[]> = {}

  on(event: string, cb: EventCallback): void {
    const existing = this.handlers[event]
    if (existing) {
      existing.push(cb)
    } else {
      this.handlers[event] = [cb]
    }
  }

  emit(event: string, data: unknown): void {
    const handlers = this.handlers[event]
    if (handlers) {
      for (const cb of handlers) {
        cb(data)
      }
    }
  }
}

export class Command {
  stdout = new MockEmitter()
  stderr = new MockEmitter()
  private closeHandlers: Array<(data: { code: number | null; signal?: number | null }) => void> = []

  static create(
    command: string,
    args?: string[],
    options?: { cwd?: string; env?: Record<string, string> }
  ): Command {
    const cmd = new Command()

    // Simulate async command execution
    setTimeout(() => {
      // Emit stdout data for common commands
      if (command === 'sh' && args) {
        const fullCommand = args.join(' ')

        if (fullCommand.includes('echo')) {
          const match = fullCommand.match(/echo ['"]?(.+?)['"]?$/)
          if (match) {
            cmd.stdout.emit('data', match[1])
          }
        }

        if (fullCommand.includes('pwd')) {
          cmd.stdout.emit('data', options?.cwd || '/')
        }

        if (fullCommand.includes('bash') && fullCommand.includes('$TERM')) {
          if (fullCommand.includes('AVA_TERMINAL')) {
            cmd.stdout.emit('data', '1')
          } else {
            cmd.stdout.emit('data', 'xterm-256color')
          }
        }
      }

      // Emit close event
      const exitCode = command === 'this_command_does_not_exist' ? 127 : 0
      cmd.emitClose(exitCode)
    }, 10)

    return cmd
  }

  on(_event: 'close', cb: (data: { code: number | null; signal?: number | null }) => void): void {
    this.closeHandlers.push(cb)
  }

  private emitClose(code: number): void {
    for (const cb of this.closeHandlers) {
      cb({ code })
    }
  }

  async execute(): Promise<{ code: number; stdout: string; stderr: string }> {
    return { code: 0, stdout: '', stderr: '' }
  }

  async spawn(): Promise<{
    pid: number
    write: (data: string) => Promise<void>
    kill: () => Promise<void>
  }> {
    return {
      pid: 12345,
      write: async () => {},
      kill: async () => {},
    }
  }
}
