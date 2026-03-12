/**
 * Stub: @tauri-apps/plugin-shell
 *
 * Simulates Tauri shell Command for testing. Handles common shell patterns:
 * echo, pwd, exit codes, stderr redirects, sleep, bash -c, kill.
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

/** Parse a shell command string and determine stdout, stderr, and exit code */
function simulateCommand(
  command: string,
  args?: string[],
  options?: { cwd?: string; env?: Record<string, string> }
): { stdout: string; stderr: string; exitCode: number; delayMs: number } {
  let stdout = ''
  let stderr = ''
  let exitCode = 0
  let delayMs = 10

  // Handle sh -c "..." and sh -l -c "..." (PTY style)
  if (command === 'sh' && args?.includes('-c')) {
    const cIndex = args.indexOf('-c')
    const fullCommand = args[cIndex + 1] || ''

    // Handle stderr redirect: echo error >&2
    if (fullCommand.match(/echo\s+(.+?)\s*>&2/) || fullCommand.match(/echo\s+(.+?)\s+1>&2/)) {
      const match = fullCommand.match(/echo\s+(.+?)\s*(?:1?>)&2/)
      if (match) {
        stderr = match[1].replace(/^['"]|['"]$/g, '')
      }
      return { stdout, stderr, exitCode, delayMs }
    }

    const trimmedCmd = fullCommand.trim()

    // Handle pwd (with possible trailing whitespace from PTY)
    if (trimmedCmd === 'pwd') {
      stdout = options?.cwd || '/'
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle nested bash -c (from PTY: "bash -c exit 42", "bash -c echo $TERM")
    const bashCMatch = trimmedCmd.match(/^bash\s+-c\s+(.+)$/)
    if (bashCMatch) {
      const innerCmd = bashCMatch[1]
      return simulateCommand('sh', ['-c', innerCmd], options)
    }

    // Handle echo (with env var substitution)
    if (fullCommand.startsWith('echo ')) {
      let echoContent = fullCommand.slice(5).replace(/^['"]|['"]$/g, '')
      // Substitute env vars if options.env is provided
      if (options?.env) {
        echoContent = echoContent.replace(/\$(\w+)/g, (_, name) => options.env?.[name] || '')
      }
      stdout = echoContent
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle false
    if (fullCommand === 'false') {
      exitCode = 1
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle exit N
    const exitMatch = fullCommand.match(/^exit\s+(\d+)$/)
    if (exitMatch) {
      exitCode = parseInt(exitMatch[1], 10)
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle sleep N — cap at 500ms for tests (enough for inactivity tests)
    const sleepMatch = fullCommand.match(/^sleep\s+([\d.]+)/)
    if (sleepMatch) {
      delayMs = Math.min(parseFloat(sleepMatch[1]) * 1000, 500)
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle bash -c with $TERM (legacy, for non-env-aware callers)
    if (fullCommand.includes('$TERM') && !options?.env) {
      if (fullCommand.includes('AVA_TERMINAL')) {
        stdout = '1'
      } else {
        stdout = 'xterm-256color'
      }
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle for loops with echo and sleep (output reset test)
    if (fullCommand.includes('for ') && fullCommand.includes('echo')) {
      const nums = fullCommand
        .match(/in\s+([\d\s]+);/)?.[1]
        ?.trim()
        .split(/\s+/) || ['1', '2', '3']
      stdout = nums.join('\n')
      delayMs = 50 // Simulate periodic output
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle cmd /c exit N (Windows compat)
    const cmdExitMatch = fullCommand.match(/cmd\s+\/c\s+exit\s+(\d+)/)
    if (cmdExitMatch) {
      exitCode = parseInt(cmdExitMatch[1], 10)
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle ping (timeout test)
    if (fullCommand.includes('ping')) {
      delayMs = 10000
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle yes | head (buffer test) - produce some output
    if (fullCommand.includes('yes') && fullCommand.includes('head')) {
      stdout = 'y\n'.repeat(100)
      return { stdout, stderr, exitCode, delayMs }
    }

    // Handle cat (interactive, no-op in mock)
    if (fullCommand.trim() === 'cat') {
      return { stdout, stderr, exitCode, delayMs }
    }

    // Unknown command via sh -c — check if it looks like a real command
    const cmdName = fullCommand.trim().split(/\s+/)[0]
    if (
      cmdName &&
      !['echo', 'pwd', 'false', 'exit', 'sleep', 'bash', 'cat', 'yes'].includes(cmdName)
    ) {
      exitCode = 127
      stderr = `sh: ${cmdName}: command not found`
      return { stdout, stderr, exitCode, delayMs }
    }
  }

  // Direct command execution (spawn style)
  if (command === 'echo') {
    stdout = (args || []).join(' ')
    return { stdout, stderr, exitCode, delayMs }
  }

  if (command === 'bash' && args?.[0] === '-c') {
    const subCmd = args[1] || ''

    // echo error >&2
    if (subCmd.match(/echo\s+(.+?)\s*>&2/)) {
      const match = subCmd.match(/echo\s+(.+?)\s*>&2/)
      if (match) stderr = match[1]
      return { stdout, stderr, exitCode, delayMs }
    }

    // exit N
    const exitMatch = subCmd.match(/^exit\s+(\d+)$/)
    if (exitMatch) {
      exitCode = parseInt(exitMatch[1], 10)
      return { stdout, stderr, exitCode, delayMs }
    }

    // for loop with echo
    if (subCmd.includes('for ') && subCmd.includes('echo')) {
      const nums = subCmd
        .match(/in\s+([\d\s]+);/)?.[1]
        ?.trim()
        .split(/\s+/) || ['1', '2', '3']
      stdout = nums.join('\n')
      delayMs = 50
      return { stdout, stderr, exitCode, delayMs }
    }
  }

  if (command === 'pwd') {
    stdout = options?.cwd || '/'
    return { stdout, stderr, exitCode, delayMs }
  }

  if (command === 'sleep') {
    const secs = parseFloat(args?.[0] || '1')
    delayMs = Math.min(secs * 1000, 500)
    return { stdout, stderr, exitCode, delayMs }
  }

  if (command === 'false') {
    exitCode = 1
    return { stdout, stderr, exitCode, delayMs }
  }

  if (command === 'this_command_does_not_exist') {
    exitCode = 127
    return { stdout, stderr, exitCode, delayMs }
  }

  return { stdout, stderr, exitCode, delayMs }
}

export class Command {
  stdout = new MockEmitter()
  stderr = new MockEmitter()
  private closeHandlers: Array<(data: { code: number | null; signal?: number | null }) => void> = []
  private _command = ''
  private _args: string[] = []
  private _options: { cwd?: string; env?: Record<string, string> } = {}
  private _killed = false
  private _timer: ReturnType<typeof setTimeout> | null = null

  static create(
    command: string,
    args?: string[],
    options?: { cwd?: string; env?: Record<string, string> }
  ): Command {
    const cmd = new Command()
    cmd._command = command
    cmd._args = args || []
    cmd._options = options || {}

    const sim = simulateCommand(command, args, options)

    // Simulate async command execution
    // For commands that produce output quickly, use short delay
    // For long-running commands (sleep), use actual delay so inactivity/kill tests work
    const timerDelay = sim.stdout || sim.stderr ? Math.min(sim.delayMs, 30) : sim.delayMs
    cmd._timer = setTimeout(() => {
      if (cmd._killed) return

      // Emit stdout/stderr data
      if (sim.stdout) {
        // For for-loop simulations, emit line by line
        const lines = sim.stdout.split('\n')
        for (const line of lines) {
          if (line) cmd.stdout.emit('data', line)
        }
      }
      if (sim.stderr) {
        cmd.stderr.emit('data', sim.stderr)
      }

      cmd.emitClose(sim.exitCode)
    }, timerDelay)

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
    const sim = simulateCommand(this._command, this._args, this._options)
    // For long-running commands, actually delay so timeout tests work
    if (sim.delayMs > 50) {
      await new Promise((resolve) => setTimeout(resolve, Math.min(sim.delayMs, 500)))
    }
    return { code: sim.exitCode, stdout: sim.stdout, stderr: sim.stderr }
  }

  async spawn(): Promise<{
    pid: number
    write: (data: string) => Promise<void>
    kill: () => Promise<void>
  }> {
    return {
      pid: 12345,
      write: async () => {},
      kill: async () => {
        this._killed = true
        if (this._timer) {
          clearTimeout(this._timer)
          this._timer = null
        }
        // Emit close with non-zero code (killed)
        this.emitClose(137)
      },
    }
  }
}
