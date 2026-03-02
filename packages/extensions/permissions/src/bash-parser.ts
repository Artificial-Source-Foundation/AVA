/**
 * Lightweight bash command tokenizer.
 *
 * Splits a command string into structured tokens: command, args, pipes, and redirects.
 * NOT a full parser (no tree-sitter) — just basic tokenization with quote handling.
 */

export interface BashTokens {
  command: string
  args: string[]
  pipes: string[][]
  redirects: string[]
}

/** Token states for the parser. */
type QuoteState = 'none' | 'single' | 'double'

/**
 * Tokenize a raw string into individual tokens, respecting quotes.
 * Handles single quotes, double quotes, and backslash escapes within double quotes.
 */
function tokenize(raw: string): string[] {
  const tokens: string[] = []
  let current = ''
  let state: QuoteState = 'none'
  let i = 0

  while (i < raw.length) {
    const ch = raw[i]!

    if (state === 'single') {
      if (ch === "'") {
        state = 'none'
      } else {
        current += ch
      }
      i++
      continue
    }

    if (state === 'double') {
      if (ch === '\\' && i + 1 < raw.length) {
        const next = raw[i + 1]!
        // In double quotes, backslash escapes only $, `, ", \, and newline
        if (next === '$' || next === '`' || next === '"' || next === '\\' || next === '\n') {
          current += next
          i += 2
          continue
        }
        current += ch
        i++
        continue
      }
      if (ch === '"') {
        state = 'none'
      } else {
        current += ch
      }
      i++
      continue
    }

    // state === 'none'
    if (ch === "'") {
      state = 'single'
      i++
      continue
    }

    if (ch === '"') {
      state = 'double'
      i++
      continue
    }

    if (ch === '\\' && i + 1 < raw.length) {
      current += raw[i + 1]!
      i += 2
      continue
    }

    // Whitespace splits tokens
    if (ch === ' ' || ch === '\t') {
      if (current.length > 0) {
        tokens.push(current)
        current = ''
      }
      i++
      continue
    }

    // Pipe operator
    if (ch === '|') {
      if (current.length > 0) {
        tokens.push(current)
        current = ''
      }
      tokens.push('|')
      i++
      continue
    }

    // Redirect operators: >>, >, 2>&1, 2>, <
    if (ch === '>' || ch === '<') {
      if (current.length > 0) {
        tokens.push(current)
        current = ''
      }
      if (ch === '>' && i + 1 < raw.length && raw[i + 1] === '>') {
        tokens.push('>>')
        i += 2
      } else {
        tokens.push(ch)
        i++
      }
      continue
    }

    // Handle 2>, 2>>, and 2>&1 redirects
    if (ch === '2' && i + 1 < raw.length && raw[i + 1] === '>' && current.length === 0) {
      if (i + 2 < raw.length && raw[i + 2] === '>') {
        tokens.push('2>>')
        i += 3
        continue
      }
      if (i + 2 < raw.length && raw[i + 2] === '&' && i + 3 < raw.length && raw[i + 3] === '1') {
        tokens.push('2>&1')
        i += 4
        continue
      }
      tokens.push('2>')
      i += 2
      continue
    }

    // Semicolons and && are treated as command separators — we only parse the first command
    if (ch === ';') {
      if (current.length > 0) {
        tokens.push(current)
        current = ''
      }
      tokens.push(';')
      i++
      continue
    }

    if (ch === '&' && i + 1 < raw.length && raw[i + 1] === '&') {
      if (current.length > 0) {
        tokens.push(current)
        current = ''
      }
      tokens.push('&&')
      i += 2
      continue
    }

    current += ch
    i++
  }

  if (current.length > 0) {
    tokens.push(current)
  }

  return tokens
}

/** Redirect operators recognized by the parser. */
const REDIRECT_OPS = new Set(['>', '>>', '<', '2>', '2>>', '2>&1'])

/**
 * Parse a bash command string into structured tokens.
 *
 * Splits on pipes, extracts redirects, and separates command from args.
 * Only processes the first command in a chain (stops at ; and &&).
 */
export function parseBashTokens(command: string): BashTokens {
  const raw = command.trim()
  if (raw.length === 0) {
    return { command: '', args: [], pipes: [], redirects: [] }
  }

  const allTokens = tokenize(raw)

  // Split into pipe segments, stopping at ; and &&
  const segments: string[][] = []
  let current: string[] = []
  const redirects: string[] = []

  for (let i = 0; i < allTokens.length; i++) {
    const token = allTokens[i]!

    // Stop at command separators — only parse first command
    if (token === ';' || token === '&&') break

    if (token === '|') {
      if (current.length > 0) {
        segments.push(current)
        current = []
      }
      continue
    }

    // Collect redirects and their targets
    if (REDIRECT_OPS.has(token)) {
      redirects.push(token)
      // Next token is the redirect target (if it exists)
      if (
        i + 1 < allTokens.length &&
        !REDIRECT_OPS.has(allTokens[i + 1]!) &&
        allTokens[i + 1] !== '|'
      ) {
        redirects.push(allTokens[i + 1]!)
        i++
      }
      continue
    }

    current.push(token)
  }

  if (current.length > 0) {
    segments.push(current)
  }

  // First segment is the main command
  const firstSegment = segments[0] ?? []
  const cmd = firstSegment[0] ?? ''
  const args = firstSegment.slice(1)

  // Remaining segments are pipe stages
  const pipes = segments.slice(1)

  return { command: cmd, args, pipes, redirects }
}
