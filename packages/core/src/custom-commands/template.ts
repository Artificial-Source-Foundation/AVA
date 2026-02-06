/**
 * Template Engine for Custom Commands
 *
 * Handles three placeholder types in prompt templates:
 *   @{path}     - File content injection (processed first)
 *   !{command}  - Shell command execution (processed second)
 *   {{args}}    - Argument substitution (processed last)
 *
 * Processing order matters: file contents and shell outputs may themselves
 * contain {{args}} which should be substituted.
 */

import type { Placeholder, ShellExecution, TemplateResult } from './types.js'

// ============================================================================
// Placeholder Patterns
// ============================================================================

/**
 * Regex patterns for each placeholder type.
 * Patterns use non-greedy matching within balanced delimiters.
 */
const PATTERNS = {
  /** @{file/path.md} - File content injection */
  file: /@\{([^}]+)\}/g,
  /** !{shell command} - Shell command execution (handles {{args}} inside) */
  shell: /!\{((?:[^{}]|\{\{[^}]*\}\})*)\}/g,
  /** {{args}} - Argument substitution */
  args: /\{\{args\}\}/gi,
} as const

// ============================================================================
// Template Resolution
// ============================================================================

/**
 * Resolve a command template by processing all placeholders.
 *
 * @param template - Prompt template with placeholders
 * @param args - User-provided arguments
 * @param options - Resolution options (file reader, shell executor)
 * @returns Resolved template result
 */
export async function resolveTemplate(
  template: string,
  args: string,
  options: TemplateResolveOptions
): Promise<TemplateResult> {
  const shellCommands: ShellExecution[] = []
  const injectedFiles: string[] = []
  let hasErrors = false
  let result = template

  // Phase 1: File injection (@{...})
  if (options.readFile) {
    result = await resolveFileInjections(result, options, injectedFiles)
  }

  // Phase 2: Shell command injection (!{...})
  if (options.executeShell) {
    const shellResult = await resolveShellInjections(result, args, options)
    result = shellResult.resolved
    shellCommands.push(...shellResult.executions)
    if (shellResult.hasErrors) hasErrors = true
  }

  // Phase 3: Argument substitution ({{args}})
  result = resolveArgs(result, args)

  return {
    prompt: result,
    shellCommands,
    injectedFiles,
    hasErrors,
  }
}

/**
 * Options for template resolution
 */
export interface TemplateResolveOptions {
  /** Read a file and return its contents */
  readFile?: (path: string) => Promise<string>
  /** Execute a shell command and return result */
  executeShell?: (command: string) => Promise<ShellExecution>
  /** Working directory for file and shell operations */
  workingDirectory?: string
}

// ============================================================================
// Phase 1: File Injection
// ============================================================================

async function resolveFileInjections(
  template: string,
  options: TemplateResolveOptions,
  injectedFiles: string[]
): Promise<string> {
  const matches = findAllMatches(template, PATTERNS.file)
  if (matches.length === 0) return template

  let result = template
  // Process in reverse order to preserve indices
  for (let i = matches.length - 1; i >= 0; i--) {
    const match = matches[i]!
    const filePath = match.content.trim()
    injectedFiles.push(filePath)

    try {
      const content = await options.readFile!(filePath)
      result = result.slice(0, match.start) + content + result.slice(match.end)
    } catch (err) {
      const errorMsg = `[Error reading file: ${filePath} - ${err instanceof Error ? err.message : 'Unknown error'}]`
      result = result.slice(0, match.start) + errorMsg + result.slice(match.end)
    }
  }

  return result
}

// ============================================================================
// Phase 2: Shell Injection
// ============================================================================

async function resolveShellInjections(
  template: string,
  args: string,
  options: TemplateResolveOptions
): Promise<{ resolved: string; executions: ShellExecution[]; hasErrors: boolean }> {
  const matches = findAllMatches(template, PATTERNS.shell)
  if (matches.length === 0) {
    return { resolved: template, executions: [], hasErrors: false }
  }

  const executions: ShellExecution[] = []
  let hasErrors = false
  let result = template

  // Process in reverse order to preserve indices
  for (let i = matches.length - 1; i >= 0; i--) {
    const match = matches[i]!

    // Shell-escape args within shell commands
    let command = match.content
    command = command.replace(PATTERNS.args, () => shellEscape(args))

    const execution = await options.executeShell!(command)
    executions.push(execution)

    if (!execution.success) {
      hasErrors = true
      const errorOutput = execution.stderr
        ? `[Command failed (exit ${execution.exitCode}): ${execution.stderr}]`
        : `[Command failed with exit code ${execution.exitCode}]`
      result = result.slice(0, match.start) + errorOutput + result.slice(match.end)
    } else {
      result = result.slice(0, match.start) + execution.output + result.slice(match.end)
    }
  }

  return { resolved: result, executions, hasErrors }
}

// ============================================================================
// Phase 3: Argument Substitution
// ============================================================================

/**
 * Replace all {{args}} placeholders with raw argument string.
 * If template has no {{args}} placeholders, append args after a newline separator.
 */
function resolveArgs(template: string, args: string): string {
  if (!args) return template

  if (PATTERNS.args.test(template)) {
    // Reset lastIndex after test
    PATTERNS.args.lastIndex = 0
    return template.replace(PATTERNS.args, args)
  }

  // No {{args}} placeholder: append args to end
  return `${template}\n\n${args}`
}

// ============================================================================
// Placeholder Extraction
// ============================================================================

/**
 * Find all placeholder matches in a template
 */
function findAllMatches(template: string, pattern: RegExp): Placeholder[] {
  const matches: Placeholder[] = []
  const regex = new RegExp(pattern.source, pattern.flags)
  let match: RegExpExecArray | null = regex.exec(template)

  while (match !== null) {
    const type: Placeholder['type'] =
      pattern === PATTERNS.file ? 'file' : pattern === PATTERNS.shell ? 'shell' : 'args'

    matches.push({
      raw: match[0],
      type,
      content: match[1]!,
      start: match.index,
      end: match.index + match[0].length,
    })

    match = regex.exec(template)
  }

  return matches
}

/**
 * Extract all placeholders from a template (for analysis/validation)
 */
export function extractPlaceholders(template: string): Placeholder[] {
  return [
    ...findAllMatches(template, PATTERNS.file),
    ...findAllMatches(template, PATTERNS.shell),
    ...findAllMatches(template, PATTERNS.args),
  ].sort((a, b) => a.start - b.start)
}

// ============================================================================
// Shell Escaping
// ============================================================================

/**
 * Escape a string for safe use in shell commands.
 * Wraps in single quotes and escapes existing single quotes.
 */
function shellEscape(s: string): string {
  if (!s) return "''"
  // Replace single quotes with escaped version
  return `'${s.replace(/'/g, "'\\''")}'`
}
