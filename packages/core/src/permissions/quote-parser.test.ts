/**
 * Quote-Aware Shell Parser Tests
 */

import { describe, expect, it } from 'vitest'
import {
  createQuoteState,
  detectDangerousCharacters,
  detectRedirects,
  extractSubshells,
  isInSafeContext,
  isInsideQuotes,
  parseCommandSegments,
  processChar,
} from './quote-parser.js'

// ============================================================================
// Quote State Machine
// ============================================================================

describe('createQuoteState', () => {
  it('initializes with all flags false', () => {
    const state = createQuoteState()
    expect(state.inSingleQuote).toBe(false)
    expect(state.inDoubleQuote).toBe(false)
    expect(state.isEscaped).toBe(false)
    expect(state.position).toBe(0)
  })
})

describe('processChar', () => {
  it('toggles single quote state', () => {
    let state = createQuoteState()
    state = processChar("'", state)
    expect(state.inSingleQuote).toBe(true)
    state = processChar("'", state)
    expect(state.inSingleQuote).toBe(false)
  })

  it('toggles double quote state', () => {
    let state = createQuoteState()
    state = processChar('"', state)
    expect(state.inDoubleQuote).toBe(true)
    state = processChar('"', state)
    expect(state.inDoubleQuote).toBe(false)
  })

  it('sets escape flag on backslash', () => {
    let state = createQuoteState()
    state = processChar('\\', state)
    expect(state.isEscaped).toBe(true)
  })

  it('clears escape flag on next char', () => {
    let state = createQuoteState()
    state = processChar('\\', state)
    state = processChar('n', state)
    expect(state.isEscaped).toBe(false)
  })

  it('backslash does not escape inside single quotes', () => {
    let state = createQuoteState()
    state = processChar("'", state) // enter single quotes
    state = processChar('\\', state)
    expect(state.isEscaped).toBe(false)
  })

  it('double quotes inside single quotes are literal', () => {
    let state = createQuoteState()
    state = processChar("'", state) // enter single quotes
    state = processChar('"', state) // literal
    expect(state.inDoubleQuote).toBe(false)
    expect(state.inSingleQuote).toBe(true)
  })

  it('single quotes inside double quotes are literal', () => {
    let state = createQuoteState()
    state = processChar('"', state) // enter double quotes
    state = processChar("'", state) // literal
    expect(state.inSingleQuote).toBe(false)
    expect(state.inDoubleQuote).toBe(true)
  })

  it('increments position', () => {
    let state = createQuoteState()
    state = processChar('a', state)
    expect(state.position).toBe(1)
    state = processChar('b', state)
    expect(state.position).toBe(2)
  })
})

describe('isInsideQuotes', () => {
  it('returns false when not in quotes', () => {
    expect(isInsideQuotes(createQuoteState())).toBe(false)
  })

  it('returns true in single quotes', () => {
    let state = createQuoteState()
    state = processChar("'", state)
    expect(isInsideQuotes(state)).toBe(true)
  })

  it('returns true in double quotes', () => {
    let state = createQuoteState()
    state = processChar('"', state)
    expect(isInsideQuotes(state)).toBe(true)
  })
})

describe('isInSafeContext', () => {
  it('returns false when not in quotes', () => {
    expect(isInSafeContext(createQuoteState())).toBe(false)
  })

  it('returns true only in single quotes', () => {
    let state = createQuoteState()
    state = processChar("'", state)
    expect(isInSafeContext(state)).toBe(true)
  })

  it('returns false in double quotes', () => {
    let state = createQuoteState()
    state = processChar('"', state)
    expect(isInSafeContext(state)).toBe(false)
  })
})

// ============================================================================
// Dangerous Character Detection
// ============================================================================

describe('detectDangerousCharacters', () => {
  it('returns not found for safe commands', () => {
    expect(detectDangerousCharacters('ls -la').found).toBe(false)
    expect(detectDangerousCharacters('echo hello').found).toBe(false)
  })

  it('detects backticks outside quotes', () => {
    const result = detectDangerousCharacters('echo `whoami`')
    expect(result.found).toBe(true)
    expect(result.type).toBe('backtick')
  })

  it('detects backticks inside double quotes', () => {
    const result = detectDangerousCharacters('echo "hello `whoami`"')
    expect(result.found).toBe(true)
    expect(result.type).toBe('backtick')
  })

  it('ignores backticks inside single quotes', () => {
    const result = detectDangerousCharacters("echo '`safe`'")
    expect(result.found).toBe(false)
  })

  it('detects $() command substitution', () => {
    const result = detectDangerousCharacters('echo $(whoami)')
    expect(result.found).toBe(true)
    expect(result.type).toBe('backtick')
  })

  it('ignores $() inside single quotes', () => {
    const result = detectDangerousCharacters("echo '$(safe)'")
    expect(result.found).toBe(false)
  })

  it('detects null bytes', () => {
    const result = detectDangerousCharacters('hello\0world')
    expect(result.found).toBe(true)
    expect(result.type).toBe('null_byte')
  })

  it('detects newlines outside quotes', () => {
    const result = detectDangerousCharacters('echo hello\nrm -rf /')
    expect(result.found).toBe(true)
    expect(result.type).toBe('newline')
  })

  it('ignores newlines inside quotes', () => {
    expect(detectDangerousCharacters('"hello\nworld"').found).toBe(false)
  })

  it('detects carriage returns outside quotes', () => {
    const result = detectDangerousCharacters('echo hello\rrm -rf /')
    expect(result.found).toBe(true)
    expect(result.type).toBe('newline')
  })

  it('detects Unicode line separators', () => {
    const result = detectDangerousCharacters('echo hello\u2028rm')
    expect(result.found).toBe(true)
    expect(result.type).toBe('unicode_separator')
  })

  it('detects Unicode paragraph separators', () => {
    const result = detectDangerousCharacters('echo hello\u2029rm')
    expect(result.found).toBe(true)
    expect(result.type).toBe('unicode_separator')
  })

  it('returns position of dangerous character', () => {
    const result = detectDangerousCharacters('abc`def')
    expect(result.position).toBe(3)
  })

  it('ignores escaped backticks', () => {
    const result = detectDangerousCharacters('echo \\`safe\\`')
    expect(result.found).toBe(false)
  })
})

// ============================================================================
// Command Segmentation
// ============================================================================

describe('parseCommandSegments', () => {
  it('parses single command', () => {
    const segments = parseCommandSegments('ls -la')
    expect(segments).toHaveLength(1)
    expect(segments[0].command).toBe('ls -la')
  })

  it('splits on &&', () => {
    const segments = parseCommandSegments('cd dir && ls')
    expect(segments).toHaveLength(2)
    expect(segments[0].command).toBe('cd dir')
    expect(segments[0].separator).toBe('&&')
    expect(segments[1].command).toBe('ls')
  })

  it('splits on ||', () => {
    const segments = parseCommandSegments('cmd1 || cmd2')
    expect(segments).toHaveLength(2)
    expect(segments[0].separator).toBe('||')
  })

  it('splits on pipe', () => {
    const segments = parseCommandSegments('cat file | grep pattern')
    expect(segments).toHaveLength(2)
    expect(segments[0].separator).toBe('|')
  })

  it('splits on semicolon', () => {
    const segments = parseCommandSegments('cmd1; cmd2')
    expect(segments).toHaveLength(2)
    expect(segments[0].separator).toBe(';')
  })

  it('does not split inside quotes', () => {
    const segments = parseCommandSegments('echo "hello && world"')
    expect(segments).toHaveLength(1)
    expect(segments[0].command).toBe('echo "hello && world"')
  })

  it('does not split inside single quotes', () => {
    const segments = parseCommandSegments("echo 'a | b'")
    expect(segments).toHaveLength(1)
  })

  it('handles multiple separators', () => {
    const segments = parseCommandSegments('a && b || c; d | e')
    expect(segments).toHaveLength(5)
  })

  it('tracks positions', () => {
    const segments = parseCommandSegments('cmd1 && cmd2')
    expect(segments[0].startPos).toBe(0)
    expect(segments[0].endPos).toBe(5)
  })
})

// ============================================================================
// Redirect Detection
// ============================================================================

describe('detectRedirects', () => {
  it('detects output redirect', () => {
    expect(detectRedirects('echo hello > file.txt')).toContain('>')
  })

  it('detects append redirect', () => {
    expect(detectRedirects('echo hello >> file.txt')).toContain('>>')
  })

  it('detects input redirect', () => {
    expect(detectRedirects('cmd < input.txt')).toContain('<')
  })

  it('detects stderr redirect', () => {
    expect(detectRedirects('cmd 2> error.log')).toContain('2>')
  })

  it('returns empty for no redirects', () => {
    expect(detectRedirects('ls -la')).toHaveLength(0)
  })

  it('ignores redirects inside quotes', () => {
    expect(detectRedirects('echo ">"')).toHaveLength(0)
  })
})

// ============================================================================
// Subshell Extraction
// ============================================================================

describe('extractSubshells', () => {
  it('extracts $() subshells', () => {
    const result = extractSubshells('echo $(whoami)')
    expect(result).toContain('whoami')
  })

  it('extracts (...) subshells at start', () => {
    const result = extractSubshells('(cd dir && make)')
    expect(result).toContain('cd dir && make')
  })

  it('returns empty when no subshells', () => {
    expect(extractSubshells('ls -la')).toHaveLength(0)
  })

  it('ignores subshells in single quotes', () => {
    expect(extractSubshells("echo '$(safe)'")).toHaveLength(0)
  })
})
