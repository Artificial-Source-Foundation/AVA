/**
 * Bash Tool
 * Execute shell commands with timeout, inactivity detection, and output truncation.
 * Supports PTY (pseudo-terminal) for interactive commands.
 */

import {
  type CommandValidationResult,
  getCommandValidator,
  quickDangerCheck,
} from '../permissions/command-validator.js'
import { getPlatform } from '../platform.js'
import { ToolError, ToolErrorType } from './errors.js'
import { truncateForMetadata } from './truncation.js'
import type { Tool, ToolContext, ToolResult } from './types.js'
import {
  isBinaryOutput,
  isInteractiveCommand,
  LIMITS,
  resolvePath,
  truncateOutput,
} from './utils.js'

// ============================================================================
// Types
// ============================================================================

interface BashParams {
  command: string
  description: string
  workdir?: string
  timeout?: number
  /** Force interactive mode (uses PTY if available) */
  interactive?: boolean
  /**
   * Self-reported risk assessment from LLM.
   * Set to true for risky operations like package installation,
   * file deletion, system changes, etc.
   * When true, requires explicit user approval even in auto-approve mode.
   */
  requires_approval?: boolean
}

// ============================================================================
// Constants
// ============================================================================

const DEFAULT_TIMEOUT = 2 * 60 * 1000 // 2 minutes
const DEFAULT_INACTIVITY_TIMEOUT = 30 * 1000 // 30 seconds

// ============================================================================
// Implementation
// ============================================================================

export const bashTool: Tool<BashParams> = {
  definition: {
    name: 'bash',
    description: `Execute shell commands. Use the workdir parameter instead of 'cd' commands. Default timeout is 2 minutes. Output is truncated at ${LIMITS.MAX_LINES} lines or ${LIMITS.MAX_BYTES / 1024}KB. Commands are killed if no output for 30 seconds. Interactive commands (vim, ssh, python REPL) automatically use PTY when available.`,
    input_schema: {
      type: 'object',
      properties: {
        command: {
          type: 'string',
          description: 'Shell command to execute',
        },
        description: {
          type: 'string',
          description:
            'Brief description of what this command does (5-10 words). Examples: "List files in directory", "Install dependencies", "Run tests"',
        },
        workdir: {
          type: 'string',
          description:
            'Working directory to run the command in (optional, defaults to project root). Use this instead of cd commands.',
        },
        timeout: {
          type: 'number',
          description: 'Timeout in milliseconds (optional, default 120000 = 2 minutes)',
        },
        interactive: {
          type: 'boolean',
          description:
            'Force interactive mode using PTY (pseudo-terminal). Auto-detected for commands like vim, ssh, python. Set to true for commands that need terminal features.',
        },
        requires_approval: {
          type: 'boolean',
          description:
            'Set to true for risky operations that should require explicit user approval even in auto-approve mode. Examples: package installation (npm install, pip install), file deletion (rm -rf), system modifications, network operations to external hosts. Set to false for safe read-only operations like build, test, lint.',
        },
      },
      required: ['command', 'description'],
    },
  },

  validate(params: unknown): BashParams {
    if (typeof params !== 'object' || params === null) {
      throw new ToolError('Invalid params: expected object', ToolErrorType.INVALID_PARAMS, 'bash')
    }

    const { command, description, workdir, timeout, interactive, requires_approval } =
      params as Record<string, unknown>

    if (typeof command !== 'string' || !command.trim()) {
      throw new ToolError(
        'Invalid command: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'bash'
      )
    }

    if (typeof description !== 'string' || !description.trim()) {
      throw new ToolError(
        'Invalid description: must be non-empty string',
        ToolErrorType.INVALID_PARAMS,
        'bash'
      )
    }

    if (workdir !== undefined && typeof workdir !== 'string') {
      throw new ToolError('Invalid workdir: must be string', ToolErrorType.INVALID_PARAMS, 'bash')
    }

    if (timeout !== undefined) {
      if (typeof timeout !== 'number' || timeout <= 0) {
        throw new ToolError(
          'Invalid timeout: must be positive number',
          ToolErrorType.INVALID_PARAMS,
          'bash'
        )
      }
    }

    if (interactive !== undefined && typeof interactive !== 'boolean') {
      throw new ToolError(
        'Invalid interactive: must be boolean',
        ToolErrorType.INVALID_PARAMS,
        'bash'
      )
    }

    if (requires_approval !== undefined && typeof requires_approval !== 'boolean') {
      throw new ToolError(
        'Invalid requires_approval: must be boolean',
        ToolErrorType.INVALID_PARAMS,
        'bash'
      )
    }

    return {
      command: command.trim(),
      description: description.trim(),
      workdir: workdir?.trim(),
      timeout: timeout as number | undefined,
      interactive: interactive as boolean | undefined,
      requires_approval: requires_approval as boolean | undefined,
    }
  },

  async execute(params: BashParams, ctx: ToolContext): Promise<ToolResult> {
    const platform = getPlatform()
    const cwd = params.workdir
      ? resolvePath(params.workdir, ctx.workingDirectory)
      : ctx.workingDirectory
    const timeout = params.timeout ?? DEFAULT_TIMEOUT

    // Check abort signal before execution
    if (ctx.signal.aborted) {
      return {
        success: false,
        output: 'Command was cancelled before execution',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Security validation: check for dangerous characters and validate against rules
    const validationResult = validateCommandSecurity(params.command)
    if (!validationResult.allowed) {
      return {
        success: false,
        output: formatValidationError(validationResult),
        error: ToolErrorType.PERMISSION_DENIED,
        metadata: {
          command: params.command,
          description: params.description,
          validationReason: validationResult.reason,
          matchedPattern: validationResult.matchedPattern,
          failedSegment: validationResult.failedSegment,
          detectedOperator: validationResult.detectedOperator,
        },
      }
    }

    // Determine if we should use PTY
    const needsPty = params.interactive === true || isInteractiveCommand(params.command)
    const hasPty = platform.pty?.isSupported() ?? false

    // Route to PTY execution for interactive commands
    if (needsPty && hasPty) {
      return executePty(params, cwd, timeout, ctx)
    }

    // Warn if PTY was requested but not available
    if (needsPty && !hasPty) {
      // Fall through to regular shell execution with warning
      const warningPrefix =
        '[Warning: PTY not available for interactive command. Output may be limited.]\n\n'
      const result = await executeShell(params, cwd, timeout, ctx)
      return {
        ...result,
        output: warningPrefix + result.output,
      }
    }

    // Regular shell execution
    return executeShell(params, cwd, timeout, ctx)
  },
}

// ============================================================================
// Shell Execution
// ============================================================================

async function executeShell(
  params: BashParams,
  cwd: string,
  timeout: number,
  ctx: ToolContext
): Promise<ToolResult> {
  const shell = getPlatform().shell

  try {
    // Spawn command using platform shell
    const child = shell.spawn('bash', ['-c', params.command], {
      cwd,
      inactivityTimeout: DEFAULT_INACTIVITY_TIMEOUT,
      killProcessGroup: true,
    })

    // Collect output
    let stdout = ''
    let stderr = ''
    let binaryDetected = false

    // Set up abort handling
    let aborted = false
    const abortHandler = () => {
      aborted = true
      child.kill()
    }
    ctx.signal.addEventListener('abort', abortHandler, { once: true })

    // Set up timeout
    let timedOut = false
    const timeoutId = setTimeout(() => {
      timedOut = true
      child.kill()
    }, timeout)

    // Read stdout stream
    if (child.stdout) {
      const reader = child.stdout.getReader()
      const decoder = new TextDecoder()

      try {
        while (true) {
          const { done, value } = await reader.read()
          if (done) break

          // Check for binary output
          if (isBinaryOutput(value)) {
            binaryDetected = true
            stdout += '\n[Binary output detected. Halting stream...]'
            child.kill()
            break
          }

          stdout += decoder.decode(value, { stream: true })

          // Stream metadata update for live output
          if (ctx.metadata) {
            ctx.metadata({
              title: params.description,
              metadata: { output: truncateForMetadata(stdout), stream: 'stdout' },
            })
          }
        }
      } catch {
        // Stream error - process may have been killed
      } finally {
        reader.releaseLock()
      }
    }

    // Read stderr stream
    if (child.stderr) {
      const reader = child.stderr.getReader()
      const decoder = new TextDecoder()

      try {
        while (true) {
          const { done, value } = await reader.read()
          if (done) break

          stderr += decoder.decode(value, { stream: true })

          // Stream metadata update for live stderr
          if (ctx.metadata) {
            ctx.metadata({
              title: params.description,
              metadata: { output: truncateForMetadata(stderr), stream: 'stderr' },
            })
          }
        }
      } catch {
        // Stream error - process may have been killed
      } finally {
        reader.releaseLock()
      }
    }

    // Wait for process to complete
    const result = await child.wait()

    // Clean up
    clearTimeout(timeoutId)
    ctx.signal.removeEventListener('abort', abortHandler)

    // Handle timeout
    if (timedOut) {
      return {
        success: false,
        output: `Command timed out after ${timeout}ms.\n\nTo increase timeout, use the timeout parameter.`,
        error: ToolErrorType.EXECUTION_TIMEOUT,
        metadata: {
          command: params.command,
          description: params.description,
          cwd,
          timeout,
          timedOut: true,
        },
        locations: [{ path: cwd, type: 'exec' }],
      }
    }

    // Handle abort
    if (aborted) {
      return {
        success: false,
        output: 'Command was cancelled by user',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    // Handle binary output detection
    if (binaryDetected) {
      return {
        success: false,
        output: `Binary output detected in command output.\n\nPartial output before binary:\n${stdout}`,
        error: ToolErrorType.BINARY_OUTPUT,
        metadata: {
          command: params.command,
          description: params.description,
          cwd,
          binaryDetected: true,
        },
        locations: [{ path: cwd, type: 'exec' }],
      }
    }

    const exitCode = result.exitCode

    // Build output
    let output = ''

    if (exitCode === 0) {
      // Success case
      const combined = stdout + (stderr ? `\n${stderr}` : '')
      const truncated = truncateOutput(combined)

      if (truncated.truncated) {
        output = `Exit code: 0\n\n<output>\n${truncated.content}\n\n(Output truncated: ${truncated.removedLines} lines removed. Use read_file with offset to see full output if saved to file.)\n</output>`
      } else {
        output = `Exit code: 0\n\n<output>\n${combined || '(no output)'}\n</output>`
      }
    } else {
      // Error case
      const parts: string[] = [`Exit code: ${exitCode}`]

      if (stderr.trim()) {
        const truncatedErr = truncateOutput(stderr)
        parts.push(`\n<stderr>\n${truncatedErr.content}\n</stderr>`)
      }

      if (stdout.trim()) {
        const truncatedOut = truncateOutput(stdout)
        parts.push(`\n<stdout>\n${truncatedOut.content}\n</stdout>`)
      }

      if (!stderr.trim() && !stdout.trim()) {
        parts.push('\n(no output)')
      }

      output = parts.join('')
    }

    return {
      success: exitCode === 0,
      output,
      metadata: {
        command: params.command,
        description: params.description,
        cwd,
        exitCode,
        stdoutLength: stdout.length,
        stderrLength: stderr.length,
        requiresApproval: params.requires_approval,
      },
      locations: [{ path: cwd, type: 'exec' }],
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return {
      success: false,
      output: `Error executing command: ${message}`,
      error: ToolErrorType.UNKNOWN,
    }
  }
}

// ============================================================================
// PTY Execution
// ============================================================================

/**
 * Execute command using PTY (pseudo-terminal) for interactive commands.
 * PTY provides proper terminal emulation for commands like vim, ssh, python REPL.
 */
async function executePty(
  params: BashParams,
  cwd: string,
  timeout: number,
  ctx: ToolContext
): Promise<ToolResult> {
  const pty = getPlatform().pty

  if (!pty) {
    return {
      success: false,
      output: 'PTY not available on this platform',
      error: ToolErrorType.NOT_SUPPORTED,
    }
  }

  try {
    // Spawn PTY process
    const ptyProcess = pty.spawn('bash', ['-c', params.command], {
      cwd,
      cols: 120,
      rows: 40,
    })

    // Collect output
    let output = ''

    // Set up abort handling
    let aborted = false
    const abortHandler = () => {
      aborted = true
      ptyProcess.kill()
    }
    ctx.signal.addEventListener('abort', abortHandler, { once: true })

    // Set up timeout
    let timedOut = false
    const timeoutId = setTimeout(() => {
      timedOut = true
      ptyProcess.kill()
    }, timeout)

    // Collect PTY output
    ptyProcess.onData((data) => {
      output += data
      // Truncate if getting too large (PTY can produce a lot of output)
      if (output.length > LIMITS.MAX_BYTES * 2) {
        output = output.slice(-LIMITS.MAX_BYTES)
      }

      // Stream metadata update for live PTY output
      if (ctx.metadata) {
        ctx.metadata({
          title: params.description,
          metadata: { output: truncateForMetadata(output), stream: 'pty' },
        })
      }
    })

    // Wait for exit
    const result = await ptyProcess.wait()

    // Clean up
    clearTimeout(timeoutId)
    ctx.signal.removeEventListener('abort', abortHandler)

    // Handle timeout
    if (timedOut) {
      return {
        success: false,
        output: `Command timed out after ${timeout}ms.\n\nPartial output:\n${truncateOutput(output).content}`,
        error: ToolErrorType.EXECUTION_TIMEOUT,
        metadata: {
          command: params.command,
          description: params.description,
          cwd,
          timeout,
          timedOut: true,
          usedPty: true,
        },
        locations: [{ path: cwd, type: 'exec' }],
      }
    }

    // Handle abort
    if (aborted) {
      return {
        success: false,
        output: 'Command was cancelled by user',
        error: ToolErrorType.EXECUTION_ABORTED,
      }
    }

    const exitCode = result.exitCode
    const truncated = truncateOutput(output)

    return {
      success: exitCode === 0,
      output:
        exitCode === 0
          ? `Exit code: 0\n\n<output>\n${truncated.content || '(no output)'}\n</output>`
          : `Exit code: ${exitCode}\n\n<output>\n${truncated.content || '(no output)'}\n</output>`,
      metadata: {
        command: params.command,
        description: params.description,
        cwd,
        exitCode,
        outputLength: output.length,
        usedPty: true,
      },
      locations: [{ path: cwd, type: 'exec' }],
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    return {
      success: false,
      output: `Error executing command with PTY: ${message}`,
      error: ToolErrorType.UNKNOWN,
    }
  }
}

// ============================================================================
// Security Validation
// ============================================================================

/**
 * Validate command for security before execution.
 * Uses the global CommandValidator which can be configured via:
 * - Environment variable: ESTELA_COMMAND_PERMISSIONS
 * - Programmatic API: setCommandPermissions()
 *
 * Security checks:
 * 1. Quick danger check (backticks, newlines, unicode separators)
 * 2. Full validation against allow/deny rules (if configured)
 * 3. Each segment of chained commands validated separately
 */
function validateCommandSecurity(command: string): CommandValidationResult {
  // Quick check for dangerous characters first (fast path)
  const dangerCheck = quickDangerCheck(command)
  if (dangerCheck.found) {
    return {
      allowed: false,
      reason: 'dangerous_char_detected',
      detectedOperator: dangerCheck.character,
      failedSegment: command,
    }
  }

  // Full validation (includes chained command parsing)
  const validator = getCommandValidator()
  return validator.validate(command)
}

/**
 * Format validation error for user display.
 * Provides clear, actionable error messages.
 */
function formatValidationError(result: CommandValidationResult): string {
  const parts: string[] = ['Command blocked for security reasons.']

  switch (result.reason) {
    case 'dangerous_char_detected':
      parts.push(`\nDetected dangerous character: ${result.detectedOperator}`)
      if (result.detectedOperator === '`' || result.detectedOperator === '$(') {
        parts.push('Command substitution is not allowed for security reasons.')
        parts.push('Tip: Use single quotes to pass literal backticks.')
      } else if (
        result.detectedOperator?.includes('\\n') ||
        result.detectedOperator?.includes('\\r')
      ) {
        parts.push('Newlines outside quotes can inject additional commands.')
      } else {
        parts.push('Unicode separators can be used for command injection.')
      }
      break

    case 'denied':
    case 'segment_denied':
      parts.push(`\nCommand matches deny pattern: ${result.matchedPattern}`)
      if (result.failedSegment) {
        parts.push(`Blocked segment: ${result.failedSegment}`)
      }
      break

    case 'segment_no_match':
    case 'no_match_deny_default':
      parts.push('\nCommand does not match any allowed pattern.')
      if (result.failedSegment) {
        parts.push(`Unmatched segment: ${result.failedSegment}`)
      }
      parts.push('Configure ESTELA_COMMAND_PERMISSIONS to allow this command.')
      break

    case 'redirect_detected':
      parts.push(`\nRedirect operator detected: ${result.detectedOperator}`)
      parts.push('Redirects are disabled in current configuration.')
      parts.push('Set allowRedirects: true in config to enable.')
      break

    case 'subshell_denied':
      parts.push(`\nSubshell content denied: ${result.failedSegment}`)
      break

    case 'empty_command':
      parts.push('\nEmpty or whitespace-only command.')
      break

    default:
      parts.push(`\nReason: ${result.reason}`)
  }

  // Show parsed segments for debugging chained commands
  if (result.segments && result.segments.length > 1) {
    parts.push('\n\nParsed command segments:')
    result.segments.forEach((seg, i) => {
      const status = i === result.failedSegmentIndex ? '❌' : '✓'
      parts.push(`  ${status} [${i}] ${seg.command}${seg.separator ? ` ${seg.separator}` : ''}`)
    })
  }

  return parts.join('\n')
}
