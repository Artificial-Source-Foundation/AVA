/**
 * Syntax Validator
 * Checks that files parse without syntax errors
 *
 * Uses esbuild for fast parsing of TypeScript/JavaScript files.
 * Falls back to simple regex-based checks for other file types.
 */

import { getPlatform } from '../platform.js'
import { createFailedResult, createPassedResult } from './pipeline.js'
import type { ValidationContext, ValidationResult, Validator } from './types.js'

// ============================================================================
// Syntax Validator
// ============================================================================

/**
 * Syntax Validator
 *
 * Critical validator that checks files can be parsed without errors.
 * Uses esbuild transform (fast, no bundling) for TypeScript/JavaScript.
 */
export const syntaxValidator: Validator = {
  name: 'syntax',
  description: 'Check that files parse without syntax errors',
  critical: true,

  async run(ctx: ValidationContext): Promise<ValidationResult> {
    const startTime = Date.now()
    const errors: string[] = []
    const warnings: string[] = []

    // Filter to supported file types
    const supportedExtensions = ['.ts', '.tsx', '.js', '.jsx', '.mjs', '.cjs']
    const filesToCheck = ctx.files.filter((f) => supportedExtensions.some((ext) => f.endsWith(ext)))

    if (filesToCheck.length === 0) {
      return createPassedResult('syntax', Date.now() - startTime, [
        'No TypeScript/JavaScript files to check',
      ])
    }

    // Check each file using esbuild transform
    for (const file of filesToCheck) {
      if (ctx.signal.aborted) break

      try {
        const result = await checkFileSyntax(file, ctx)
        if (result.error) {
          errors.push(`${file}: ${result.error}`)
        }
        if (result.warnings) {
          warnings.push(...result.warnings.map((w) => `${file}: ${w}`))
        }
      } catch (error) {
        errors.push(`${file}: ${error}`)
      }
    }

    const durationMs = Date.now() - startTime

    if (errors.length > 0) {
      return createFailedResult('syntax', durationMs, errors, warnings)
    }

    return createPassedResult('syntax', durationMs, warnings)
  },
}

// ============================================================================
// Syntax Checking Implementation
// ============================================================================

interface SyntaxCheckResult {
  error?: string
  warnings?: string[]
}

/**
 * Check syntax of a single file using esbuild
 *
 * We use `esbuild --bundle=false` to parse without bundling.
 * This is very fast and catches syntax errors.
 */
async function checkFileSyntax(file: string, ctx: ValidationContext): Promise<SyntaxCheckResult> {
  const shell = getPlatform().shell
  const fs = getPlatform().fs

  // Determine loader based on extension
  const ext = file.split('.').pop() || 'ts'
  const loader = getEsbuildLoader(ext)

  // Try using esbuild directly if available
  try {
    const result = await shell.exec(
      `npx esbuild "${file}" --bundle=false --loader=${loader} --format=esm --outfile=/dev/null 2>&1`,
      { cwd: ctx.cwd, timeout: 10000 }
    )

    if (result.exitCode !== 0) {
      // Parse esbuild error output
      const errorMatch = result.stderr.match(/error: (.+)/)
      const error = errorMatch ? errorMatch[1] : result.stderr.trim() || result.stdout.trim()
      return { error: error || 'Syntax error' }
    }

    return {}
  } catch {
    // Fallback: try to read and do basic validation
    try {
      const content = await fs.readFile(file)
      const basicCheck = basicSyntaxCheck(content, file)
      return basicCheck
    } catch (readError) {
      return { error: `Cannot read file: ${readError}` }
    }
  }
}

/**
 * Get esbuild loader for file extension
 */
function getEsbuildLoader(ext: string): string {
  switch (ext) {
    case 'ts':
      return 'ts'
    case 'tsx':
      return 'tsx'
    case 'js':
    case 'mjs':
    case 'cjs':
      return 'js'
    case 'jsx':
      return 'jsx'
    default:
      return 'ts'
  }
}

/**
 * Basic syntax check using regex patterns
 * Fallback when esbuild is not available
 */
function basicSyntaxCheck(content: string, _file: string): SyntaxCheckResult {
  const warnings: string[] = []

  // Check for unclosed brackets/braces/parens
  const brackets = countBrackets(content)
  if (brackets.parens !== 0) {
    return { error: `Unclosed parentheses (${brackets.parens > 0 ? 'missing )' : 'extra )'})` }
  }
  if (brackets.braces !== 0) {
    return { error: `Unclosed braces (${brackets.braces > 0 ? 'missing }' : 'extra }'})` }
  }
  if (brackets.brackets !== 0) {
    return { error: `Unclosed brackets (${brackets.brackets > 0 ? 'missing ]' : 'extra ]'})` }
  }

  // Check for unterminated strings (basic check)
  const lines = content.split('\n')
  for (let i = 0; i < lines.length; i++) {
    const line = lines[i]
    // Skip comment lines
    if (line.trim().startsWith('//') || line.trim().startsWith('*')) continue

    // Check for obvious string issues (very basic)
    const singleQuotes = (line.match(/'/g) || []).length
    const doubleQuotes = (line.match(/"/g) || []).length
    // Note: backticks are not checked as template literals can span multiple lines
    if (singleQuotes % 2 !== 0 && !line.includes("\\'")) {
      warnings.push(`Line ${i + 1}: Possible unterminated single-quoted string`)
    }
    if (doubleQuotes % 2 !== 0 && !line.includes('\\"')) {
      warnings.push(`Line ${i + 1}: Possible unterminated double-quoted string`)
    }
  }

  return { warnings: warnings.length > 0 ? warnings : undefined }
}

/**
 * Count unclosed brackets in code
 */
function countBrackets(content: string): {
  parens: number
  braces: number
  brackets: number
} {
  let parens = 0
  let braces = 0
  let brackets = 0

  // Remove strings and comments to avoid counting brackets inside them
  const cleaned = content
    // Remove template literals
    .replace(/`[^`]*`/g, '')
    // Remove double-quoted strings
    .replace(/"[^"\\]*(?:\\.[^"\\]*)*"/g, '')
    // Remove single-quoted strings
    .replace(/'[^'\\]*(?:\\.[^'\\]*)*'/g, '')
    // Remove multi-line comments
    .replace(/\/\*[\s\S]*?\*\//g, '')
    // Remove single-line comments
    .replace(/\/\/.*/g, '')

  for (const char of cleaned) {
    switch (char) {
      case '(':
        parens++
        break
      case ')':
        parens--
        break
      case '{':
        braces++
        break
      case '}':
        braces--
        break
      case '[':
        brackets++
        break
      case ']':
        brackets--
        break
    }
  }

  return { parens, braces, brackets }
}
