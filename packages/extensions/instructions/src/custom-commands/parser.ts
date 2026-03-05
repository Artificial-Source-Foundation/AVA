/**
 * TOML-like command file parser.
 *
 * Parses simple TOML-like files without requiring a TOML library.
 * Supports: name, description, prompt, allowed_tools, mode.
 */

import type { CustomCommand } from './types.js'

/**
 * Parse a TOML-like command file into a CustomCommand.
 * Returns null if the file is invalid or missing required fields.
 */
export function parseCommandFile(content: string, source: string): CustomCommand | null {
  const fields: Record<string, string> = {}
  let inMultiline = false
  let multilineKey = ''
  let multilineValue = ''

  for (const line of content.split('\n')) {
    const trimmed = line.trim()

    // Handle multiline strings (triple-quoted)
    if (inMultiline) {
      if (trimmed === '"""' || trimmed === "'''") {
        fields[multilineKey] = multilineValue.trim()
        inMultiline = false
        continue
      }
      multilineValue += `${line}\n`
      continue
    }

    // Skip comments and empty lines
    if (trimmed.startsWith('#') || trimmed === '') continue

    // Parse key = value
    const eqIdx = trimmed.indexOf('=')
    if (eqIdx === -1) continue

    const key = trimmed.slice(0, eqIdx).trim()
    let value = trimmed.slice(eqIdx + 1).trim()

    // Check for multiline string start
    if (value === '"""' || value === "'''") {
      inMultiline = true
      multilineKey = key
      multilineValue = ''
      continue
    }

    // Remove surrounding quotes
    if (
      (value.startsWith('"') && value.endsWith('"')) ||
      (value.startsWith("'") && value.endsWith("'"))
    ) {
      value = value.slice(1, -1)
    }

    fields[key] = value
  }

  // Validate required fields
  const name = fields.name
  const description = fields.description ?? ''
  const prompt = fields.prompt

  if (!name || !prompt) return null

  // Parse allowed_tools (comma-separated or array-like)
  let allowedTools: string[] | undefined
  if (fields.allowed_tools) {
    allowedTools = fields.allowed_tools
      .replace(/^\[|\]$/g, '')
      .split(',')
      .map((t) => t.trim().replace(/^["']|["']$/g, ''))
      .filter(Boolean)
  }

  return {
    name,
    description,
    prompt,
    allowedTools,
    mode: fields.mode,
    source,
  }
}
