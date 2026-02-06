/**
 * TOML Command Parser
 * Lightweight parser for custom command TOML files.
 *
 * Only supports the subset of TOML used by command files:
 * - Simple string values: key = "value"
 * - Multi-line strings: key = \"""...\"""
 * - Comments: # comment
 *
 * No support for tables, arrays, dates, etc. (not needed for commands).
 */

import type { CustomCommandDef } from './types.js'

// ============================================================================
// Parser
// ============================================================================

/**
 * Parse a TOML command file content into a CustomCommandDef.
 *
 * @param content - Raw TOML file content
 * @param name - Command name (from filename)
 * @param sourcePath - Absolute path to source file
 * @param isProjectLevel - Whether this is a project-level command
 * @returns Parsed command definition
 * @throws Error if prompt field is missing
 */
export function parseCommandToml(
  content: string,
  name: string,
  sourcePath: string,
  isProjectLevel: boolean
): CustomCommandDef {
  const fields = parseTomlFields(content)

  const prompt = fields.prompt
  if (!prompt) {
    throw new Error(`Custom command "${name}" is missing required "prompt" field (${sourcePath})`)
  }

  return {
    name,
    description: fields.description,
    prompt,
    sourcePath,
    isProjectLevel,
  }
}

// ============================================================================
// TOML Field Parser
// ============================================================================

/**
 * Parse key-value fields from TOML content.
 * Handles simple strings and multi-line strings.
 */
function parseTomlFields(content: string): Record<string, string> {
  const fields: Record<string, string> = {}
  let i = 0
  const lines = content.split('\n')

  while (i < lines.length) {
    const line = lines[i]!.trim()

    // Skip empty lines and comments
    if (!line || line.startsWith('#')) {
      i++
      continue
    }

    // Parse key = value
    const eqIndex = line.indexOf('=')
    if (eqIndex === -1) {
      i++
      continue
    }

    const key = line.slice(0, eqIndex).trim()
    const valueRaw = line.slice(eqIndex + 1).trim()

    // Multi-line string (triple quotes)
    if (valueRaw.startsWith('"""')) {
      const multiLineResult = parseMultiLineString(lines, i, eqIndex)
      fields[key] = multiLineResult.value
      i = multiLineResult.endLine + 1
      continue
    }

    // Single-line string (double or single quotes)
    if (
      (valueRaw.startsWith('"') && valueRaw.endsWith('"')) ||
      (valueRaw.startsWith("'") && valueRaw.endsWith("'"))
    ) {
      fields[key] = unescapeString(valueRaw.slice(1, -1))
    } else {
      // Bare value (boolean, number, etc.)
      fields[key] = valueRaw
    }

    i++
  }

  return fields
}

/**
 * Parse a multi-line string starting with \""" on a given line.
 */
function parseMultiLineString(
  lines: string[],
  startLine: number,
  eqIndex: number
): { value: string; endLine: number } {
  const firstLine = lines[startLine]!
  const afterEq = firstLine.slice(eqIndex + 1).trim()

  // Content after opening """
  const content = afterEq.slice(3) // Remove opening """

  // Check if closing """ is on the same line
  const closingIndex = content.indexOf('"""')
  if (closingIndex !== -1) {
    return {
      value: unescapeString(content.slice(0, closingIndex)),
      endLine: startLine,
    }
  }

  // Multi-line: collect until closing """
  const parts: string[] = []

  // Skip leading newline after opening """
  if (content) {
    parts.push(content)
  }

  let i = startLine + 1
  while (i < lines.length) {
    const line = lines[i]!
    const tripleIndex = line.indexOf('"""')

    if (tripleIndex !== -1) {
      // Found closing """
      const beforeClose = line.slice(0, tripleIndex)
      if (beforeClose) {
        parts.push(beforeClose)
      }
      return {
        value: unescapeString(parts.join('\n')),
        endLine: i,
      }
    }

    parts.push(line)
    i++
  }

  // Unterminated multi-line string - return what we have
  return {
    value: unescapeString(parts.join('\n')),
    endLine: lines.length - 1,
  }
}

/**
 * Unescape common string escape sequences
 */
function unescapeString(s: string): string {
  return s
    .replace(/\\n/g, '\n')
    .replace(/\\t/g, '\t')
    .replace(/\\r/g, '\r')
    .replace(/\\\\/g, '\\')
    .replace(/\\"/g, '"')
}
