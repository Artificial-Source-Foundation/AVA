/**
 * Token-efficient tool result transformers.
 *
 * Compresses tool output before sending back to the LLM to reduce
 * token usage without losing essential information.
 */

// ─── ANSI Strip ──────────────────────────────────────────────────────────────

/** Remove ANSI escape codes from a string. */
// biome-ignore lint/suspicious/noControlCharactersInRegex: ANSI escape stripping requires control characters
const ANSI_RE = /\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])/g

export function stripAnsi(s: string): string {
  return s.replace(ANSI_RE, '')
}

// ─── Whitespace Normalization ────────────────────────────────────────────────

/**
 * Collapse runs of spaces/tabs to a single space per line,
 * trim trailing whitespace, and collapse 3+ consecutive blank lines to 2.
 */
export function normalizeWhitespace(s: string): string {
  const lines = s.split('\n')
  const normalized: string[] = []
  let consecutiveBlanks = 0

  for (const line of lines) {
    // Collapse horizontal whitespace runs to single space, then trim
    const trimmed = line.replace(/[ \t]+/g, ' ').trim()
    if (trimmed === '') {
      consecutiveBlanks++
      if (consecutiveBlanks <= 2) {
        normalized.push('')
      }
    } else {
      consecutiveBlanks = 0
      normalized.push(trimmed)
    }
  }

  // Trim leading/trailing blank lines from the result
  while (normalized.length > 0 && normalized[0] === '') normalized.shift()
  while (normalized.length > 0 && normalized[normalized.length - 1] === '') normalized.pop()

  return normalized.join('\n')
}

// ─── Smart Summarize ─────────────────────────────────────────────────────────

const DEFAULT_MAX_BYTES = 5 * 1024 // 5KB threshold

/**
 * For outputs larger than `maxBytes`, return the first 50 lines +
 * last 20 lines with a summary of omitted content in between.
 * Outputs under the threshold are returned unchanged.
 */
export function smartSummarize(output: string, maxBytes: number = DEFAULT_MAX_BYTES): string {
  if (Buffer.byteLength(output, 'utf8') <= maxBytes) {
    return output
  }

  const lines = output.split('\n')
  const totalLines = lines.length

  if (totalLines <= 70) {
    // Not enough lines to meaningfully summarize
    return output
  }

  const headCount = 50
  const tailCount = 20
  const omittedCount = totalLines - headCount - tailCount

  const head = lines.slice(0, headCount).join('\n')
  const tail = lines.slice(-tailCount).join('\n')

  return `${head}\n\n... [${omittedCount} lines omitted out of ${totalLines} total] ...\n\n${tail}`
}

// ─── Grep Result Grouping ────────────────────────────────────────────────────

/**
 * Group grep/ripgrep output by file, showing match counts per file
 * and individual matches underneath. When the grouped output is still
 * large it falls through to smartSummarize.
 */
export function groupGrepResults(output: string): string {
  const lines = output.split('\n').filter((l) => l.length > 0)
  if (lines.length === 0) return output

  // Try to parse "file:line:content" or "file:content" format
  const fileMatches = new Map<string, string[]>()
  let parsedAny = false

  for (const line of lines) {
    // Match patterns like /path/to/file:123:content or /path/to/file:content
    const colonIdx = line.indexOf(':')
    if (colonIdx > 0) {
      const file = line.slice(0, colonIdx)
      // Only treat as a file path if it looks like one (contains / or \)
      if (file.includes('/') || file.includes('\\')) {
        const rest = line.slice(colonIdx + 1)
        let matches = fileMatches.get(file)
        if (!matches) {
          matches = []
          fileMatches.set(file, matches)
        }
        matches.push(rest)
        parsedAny = true
        continue
      }
    }
    // Non-parseable line — put in a catch-all bucket
    let misc = fileMatches.get('__misc__')
    if (!misc) {
      misc = []
      fileMatches.set('__misc__', misc)
    }
    misc.push(line)
  }

  if (!parsedAny) {
    // Could not parse as grep output — return as-is
    return output
  }

  const parts: string[] = []
  let totalMatches = 0

  for (const [file, matches] of fileMatches) {
    if (file === '__misc__') continue
    totalMatches += matches.length
    parts.push(`${file} (${matches.length} match${matches.length === 1 ? '' : 'es'})`)
    // Show up to 5 matches per file to keep it concise
    const shown = matches.slice(0, 5)
    for (const m of shown) {
      parts.push(`  ${m}`)
    }
    if (matches.length > 5) {
      parts.push(`  ... and ${matches.length - 5} more`)
    }
  }

  // Append miscellaneous lines if any
  const misc = fileMatches.get('__misc__')
  if (misc && misc.length > 0) {
    parts.push('')
    for (const m of misc) {
      parts.push(m)
    }
  }

  const header = `${fileMatches.size - (fileMatches.has('__misc__') ? 1 : 0)} files, ${totalMatches} total matches`
  return `${header}\n\n${parts.join('\n')}`
}

// ─── LS Output Summary ──────────────────────────────────────────────────────

/**
 * Summarize directory listing output.
 * Counts files/directories and shows the first 50 entries.
 */
export function summarizeLsOutput(output: string): string {
  const lines = output.split('\n').filter((l) => l.trim().length > 0)
  if (lines.length === 0) return output
  if (lines.length <= 50) return output

  const shown = lines.slice(0, 50).join('\n')
  const omitted = lines.length - 50

  return `${shown}\n\n... and ${omitted} more entries (${lines.length} total)`
}

// ─── Dispatcher ─────────────────────────────────────────────────────────────

/**
 * Apply tool-specific output transformations to reduce token usage.
 * Dispatches to specialized formatters based on tool name.
 */
export function efficientToolResult(toolName: string, output: string): string {
  if (!output || output.length === 0) return output

  switch (toolName) {
    case 'grep':
    case 'ripgrep':
      return smartSummarize(groupGrepResults(output))

    case 'ls':
    case 'glob':
      return summarizeLsOutput(output)

    case 'bash':
      return smartSummarize(normalizeWhitespace(stripAnsi(output)))

    default:
      return smartSummarize(normalizeWhitespace(output))
  }
}
