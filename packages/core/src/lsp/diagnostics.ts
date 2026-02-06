/**
 * Diagnostics Extraction
 * Get diagnostics from language tools without full LSP
 *
 * Uses common CLI tools:
 * - TypeScript: tsc --noEmit
 * - ESLint: eslint --format json
 * - Python: pyright --outputjson
 */

import { spawn } from 'node:child_process'
import { extname } from 'node:path'
import type { Diagnostic, DiagnosticResult, DiagnosticSummary } from './types.js'

// ============================================================================
// TypeScript Diagnostics
// ============================================================================

/**
 * Parse TypeScript compiler output
 */
function parseTscOutput(output: string): Diagnostic[] {
  const diagnostics: Diagnostic[] = []

  // TSC outputs errors in format: file(line,column): error TS1234: message
  const errorPattern = /^(.+?)\((\d+),(\d+)\):\s+(error|warning)\s+TS(\d+):\s+(.+)$/gm

  const matches = output.matchAll(errorPattern)
  for (const match of matches) {
    const [, , lineStr, colStr, severity, code, message] = match
    const line = parseInt(lineStr, 10) - 1 // Convert to 0-based
    const col = parseInt(colStr, 10) - 1

    diagnostics.push({
      range: {
        start: { line, character: col },
        end: { line, character: col + 1 },
      },
      severity: severity === 'error' ? 1 : 2, // DiagnosticSeverity.Error : Warning
      message,
      code: `TS${code}`,
      source: 'typescript',
    })
  }

  return diagnostics
}

/**
 * Get TypeScript diagnostics for a file
 */
export async function getTypeScriptDiagnostics(
  filePath: string,
  cwd: string
): Promise<DiagnosticResult> {
  const startTime = Date.now()

  return new Promise((resolve) => {
    const proc = spawn('npx', ['tsc', '--noEmit', '--pretty', 'false', filePath], {
      cwd,
      shell: true,
    })

    let stdout = ''
    let stderr = ''

    proc.stdout.on('data', (data) => {
      stdout += data.toString()
    })

    proc.stderr.on('data', (data) => {
      stderr += data.toString()
    })

    proc.on('close', () => {
      const output = stdout + stderr
      const diagnostics = parseTscOutput(output)

      resolve({
        file: filePath,
        diagnostics,
        source: 'typescript',
        durationMs: Date.now() - startTime,
      })
    })

    proc.on('error', () => {
      resolve({
        file: filePath,
        diagnostics: [],
        source: 'typescript',
        durationMs: Date.now() - startTime,
      })
    })
  })
}

// ============================================================================
// ESLint Diagnostics
// ============================================================================

/**
 * ESLint JSON output format
 */
interface EslintResult {
  filePath: string
  messages: Array<{
    ruleId: string | null
    severity: 1 | 2
    message: string
    line: number
    column: number
    endLine?: number
    endColumn?: number
  }>
}

/**
 * Get ESLint diagnostics for a file
 */
export async function getEslintDiagnostics(
  filePath: string,
  cwd: string
): Promise<DiagnosticResult> {
  const startTime = Date.now()

  return new Promise((resolve) => {
    const proc = spawn('npx', ['eslint', '--format', 'json', filePath], {
      cwd,
      shell: true,
    })

    let stdout = ''

    proc.stdout.on('data', (data) => {
      stdout += data.toString()
    })

    proc.on('close', () => {
      try {
        const results: EslintResult[] = JSON.parse(stdout)
        const diagnostics: Diagnostic[] = []

        for (const result of results) {
          for (const msg of result.messages) {
            diagnostics.push({
              range: {
                start: { line: msg.line - 1, character: msg.column - 1 },
                end: {
                  line: (msg.endLine ?? msg.line) - 1,
                  character: (msg.endColumn ?? msg.column) - 1,
                },
              },
              severity: msg.severity,
              message: msg.message,
              code: msg.ruleId ?? undefined,
              source: 'eslint',
            })
          }
        }

        resolve({
          file: filePath,
          diagnostics,
          source: 'eslint',
          durationMs: Date.now() - startTime,
        })
      } catch {
        resolve({
          file: filePath,
          diagnostics: [],
          source: 'eslint',
          durationMs: Date.now() - startTime,
        })
      }
    })

    proc.on('error', () => {
      resolve({
        file: filePath,
        diagnostics: [],
        source: 'eslint',
        durationMs: Date.now() - startTime,
      })
    })
  })
}

// ============================================================================
// Python Diagnostics (pyright)
// ============================================================================

/**
 * Pyright JSON output format
 */
interface PyrightOutput {
  generalDiagnostics: Array<{
    file: string
    severity: 'error' | 'warning' | 'information'
    message: string
    range: {
      start: { line: number; character: number }
      end: { line: number; character: number }
    }
    rule?: string
  }>
}

/**
 * Get Python diagnostics using pyright
 */
export async function getPythonDiagnostics(
  filePath: string,
  cwd: string
): Promise<DiagnosticResult> {
  const startTime = Date.now()

  return new Promise((resolve) => {
    const proc = spawn('pyright', ['--outputjson', filePath], {
      cwd,
      shell: true,
    })

    let stdout = ''

    proc.stdout.on('data', (data) => {
      stdout += data.toString()
    })

    proc.on('close', () => {
      try {
        const output: PyrightOutput = JSON.parse(stdout)
        const diagnostics: Diagnostic[] = []

        for (const diag of output.generalDiagnostics || []) {
          // Map severity
          let severity: number
          switch (diag.severity) {
            case 'error':
              severity = 1
              break
            case 'warning':
              severity = 2
              break
            default:
              severity = 3
          }

          diagnostics.push({
            range: {
              start: { line: diag.range.start.line, character: diag.range.start.character },
              end: { line: diag.range.end.line, character: diag.range.end.character },
            },
            severity,
            message: diag.message,
            code: diag.rule,
            source: 'pyright',
          })
        }

        resolve({
          file: filePath,
          diagnostics,
          source: 'pyright',
          durationMs: Date.now() - startTime,
        })
      } catch {
        resolve({
          file: filePath,
          diagnostics: [],
          source: 'pyright',
          durationMs: Date.now() - startTime,
        })
      }
    })

    proc.on('error', () => {
      resolve({
        file: filePath,
        diagnostics: [],
        source: 'pyright',
        durationMs: Date.now() - startTime,
      })
    })
  })
}

// ============================================================================
// Go Diagnostics (gopls)
// ============================================================================

/**
 * Parse go vet / go build output
 */
function parseGoOutput(output: string, source: string): Diagnostic[] {
  const diagnostics: Diagnostic[] = []

  // Go outputs errors in format: file:line:column: message
  const errorPattern = /^(.+?):(\d+):(\d+):\s+(.+)$/gm

  const matches = output.matchAll(errorPattern)
  for (const match of matches) {
    const [, , lineStr, colStr, message] = match
    const line = parseInt(lineStr, 10) - 1
    const col = parseInt(colStr, 10) - 1

    // Determine severity from message content
    const isWarning = message.includes('warning') || message.includes('unused')

    diagnostics.push({
      range: {
        start: { line, character: col },
        end: { line, character: col + 1 },
      },
      severity: isWarning ? 2 : 1,
      message,
      source,
    })
  }

  return diagnostics
}

/**
 * Get Go diagnostics using go vet and go build
 */
export async function getGoDiagnostics(filePath: string, cwd: string): Promise<DiagnosticResult> {
  const startTime = Date.now()

  return new Promise((resolve) => {
    // Run go vet first, then go build for type errors
    const proc = spawn('go', ['vet', filePath], {
      cwd,
      shell: true,
    })

    let stdout = ''
    let stderr = ''

    proc.stdout.on('data', (data) => {
      stdout += data.toString()
    })

    proc.stderr.on('data', (data) => {
      stderr += data.toString()
    })

    proc.on('close', () => {
      const output = stdout + stderr
      const diagnostics = parseGoOutput(output, 'go-vet')

      resolve({
        file: filePath,
        diagnostics,
        source: 'go',
        durationMs: Date.now() - startTime,
      })
    })

    proc.on('error', () => {
      resolve({
        file: filePath,
        diagnostics: [],
        source: 'go',
        durationMs: Date.now() - startTime,
      })
    })
  })
}

// ============================================================================
// Universal Diagnostic Getter
// ============================================================================

/**
 * Get diagnostics for a file based on its extension
 */
export async function getDiagnostics(filePath: string, cwd: string): Promise<DiagnosticResult[]> {
  const ext = extname(filePath).toLowerCase()
  const results: DiagnosticResult[] = []

  // TypeScript/JavaScript files
  if (['.ts', '.tsx', '.js', '.jsx'].includes(ext)) {
    // Run both TypeScript and ESLint in parallel
    const [tscResult, eslintResult] = await Promise.all([
      getTypeScriptDiagnostics(filePath, cwd),
      getEslintDiagnostics(filePath, cwd),
    ])

    if (tscResult.diagnostics.length > 0) {
      results.push(tscResult)
    }
    if (eslintResult.diagnostics.length > 0) {
      results.push(eslintResult)
    }
  }

  // Python files
  if (ext === '.py' || ext === '.pyi') {
    const pyrightResult = await getPythonDiagnostics(filePath, cwd)
    if (pyrightResult.diagnostics.length > 0) {
      results.push(pyrightResult)
    }
  }

  // Go files
  if (ext === '.go') {
    const goResult = await getGoDiagnostics(filePath, cwd)
    if (goResult.diagnostics.length > 0) {
      results.push(goResult)
    }
  }

  return results
}

/**
 * Get diagnostics summary from multiple results
 */
export function summarizeDiagnostics(results: DiagnosticResult[]): DiagnosticSummary {
  let errors = 0
  let warnings = 0
  let hints = 0
  const filesWithErrors = new Set<string>()

  for (const result of results) {
    for (const diag of result.diagnostics) {
      if (diag.severity === 1) {
        errors++
        filesWithErrors.add(result.file)
      } else if (diag.severity === 2) {
        warnings++
      } else {
        hints++
      }
    }
  }

  return {
    errors,
    warnings,
    hints,
    filesWithErrors: Array.from(filesWithErrors),
  }
}

/**
 * Format diagnostics for display
 */
export function formatDiagnostics(results: DiagnosticResult[]): string {
  if (results.length === 0) {
    return ''
  }

  const lines: string[] = []

  for (const result of results) {
    if (result.diagnostics.length === 0) continue

    for (const diag of result.diagnostics) {
      const severityStr = diag.severity === 1 ? 'error' : diag.severity === 2 ? 'warning' : 'info'
      const pos = `${diag.range.start.line + 1}:${diag.range.start.character + 1}`
      const code = diag.code ? ` (${diag.code})` : ''

      lines.push(`${result.file}:${pos} - ${severityStr}${code}: ${diag.message}`)
    }
  }

  return lines.join('\n')
}

/**
 * Check if a file has any errors (not warnings)
 */
export async function hasErrors(filePath: string, cwd: string): Promise<boolean> {
  const results = await getDiagnostics(filePath, cwd)
  return results.some((r) => r.diagnostics.some((d) => d.severity === 1))
}
