/**
 * Quote-Aware Shell Parser
 * State machine for tracking quote context in shell commands
 *
 * Security Features:
 * - Detects dangerous characters outside quotes
 * - Tracks single vs double quote semantics
 * - Handles escape sequences properly
 * - Detects Unicode command separators
 */

// ============================================================================
// Types
// ============================================================================

/**
 * Quote context state
 */
export interface QuoteState {
  /** Currently inside single quotes */
  inSingleQuote: boolean
  /** Currently inside double quotes */
  inDoubleQuote: boolean
  /** Next character is escaped */
  isEscaped: boolean
  /** Current position in string */
  position: number
}

/**
 * Dangerous character detection result
 */
export interface DangerousCharResult {
  /** Whether dangerous character was found */
  found: boolean
  /** Type of dangerous character */
  type?: 'backtick' | 'newline' | 'unicode_separator' | 'null_byte'
  /** Position where found */
  position?: number
  /** The actual character found */
  character?: string
  /** Human-readable description */
  description?: string
}

/**
 * Command segment from parsing
 */
export interface CommandSegment {
  /** The command text */
  command: string
  /** Separator that follows this segment (|, &&, ||, ;) */
  separator?: string
  /** Start position in original string */
  startPos: number
  /** End position in original string */
  endPos: number
}

// ============================================================================
// Constants
// ============================================================================

/**
 * Unicode characters that can act as line/command separators
 */
export const UNICODE_SEPARATORS: Record<string, string> = {
  '\u2028': 'LINE SEPARATOR (U+2028)',
  '\u2029': 'PARAGRAPH SEPARATOR (U+2029)',
  '\u0085': 'NEXT LINE (U+0085)',
  '\r': 'CARRIAGE RETURN',
  '\n': 'NEWLINE',
}

/**
 * Shell operators that separate commands
 */
export const COMMAND_SEPARATORS = ['&&', '||', '|', ';'] as const

/**
 * Redirect operators
 */
export const REDIRECT_OPERATORS = [
  '>>',
  '>',
  '<<',
  '<',
  '>&',
  '<&',
  '|&',
  '<(',
  '>(',
  '2>',
  '2>>',
  '&>',
  '&>>',
] as const

// ============================================================================
// Quote State Machine
// ============================================================================

/**
 * Create initial quote state
 */
export function createQuoteState(): QuoteState {
  return {
    inSingleQuote: false,
    inDoubleQuote: false,
    isEscaped: false,
    position: 0,
  }
}

/**
 * Process a single character and update state
 */
export function processChar(char: string, state: QuoteState): QuoteState {
  const newState = { ...state, position: state.position + 1 }

  // Handle escape sequences (only in double quotes or unquoted)
  if (state.isEscaped) {
    newState.isEscaped = false
    return newState
  }

  // Backslash escapes next char (but not inside single quotes)
  if (char === '\\' && !state.inSingleQuote) {
    newState.isEscaped = true
    return newState
  }

  // Single quote handling
  if (char === "'" && !state.inDoubleQuote) {
    newState.inSingleQuote = !state.inSingleQuote
    return newState
  }

  // Double quote handling
  if (char === '"' && !state.inSingleQuote) {
    newState.inDoubleQuote = !state.inDoubleQuote
    return newState
  }

  return newState
}

/**
 * Check if currently inside any quotes
 */
export function isInsideQuotes(state: QuoteState): boolean {
  return state.inSingleQuote || state.inDoubleQuote
}

/**
 * Check if currently in a "safe" context (single quotes where everything is literal)
 */
export function isInSafeContext(state: QuoteState): boolean {
  return state.inSingleQuote
}

// ============================================================================
// Dangerous Character Detection
// ============================================================================

/**
 * Scan a command string for dangerous characters outside quotes
 *
 * Detects:
 * - Backticks outside single quotes (command substitution)
 * - Newlines outside quotes (command separation)
 * - Unicode separators (potential injection)
 * - Null bytes (string termination attacks)
 */
export function detectDangerousCharacters(command: string): DangerousCharResult {
  let state = createQuoteState()

  for (let i = 0; i < command.length; i++) {
    const char = command[i]
    const prevState = state
    state = processChar(char, state)

    // Skip if escaped
    if (prevState.isEscaped) {
      continue
    }

    // Check for backticks
    // DANGEROUS: Outside quotes OR inside double quotes (bash executes them!)
    // SAFE: Only inside single quotes (literal)
    if (char === '`') {
      if (!prevState.inSingleQuote) {
        return {
          found: true,
          type: 'backtick',
          position: i,
          character: '`',
          description: prevState.inDoubleQuote
            ? 'Backtick in double quotes executes command substitution'
            : 'Backtick outside quotes executes command substitution',
        }
      }
    }

    // Check for $() command substitution (only outside single quotes)
    if (char === '$' && command[i + 1] === '(' && !prevState.inSingleQuote) {
      return {
        found: true,
        type: 'backtick',
        position: i,
        character: '$(',
        description: 'Command substitution $() detected',
      }
    }

    // Check for null bytes
    if (char === '\0') {
      return {
        found: true,
        type: 'null_byte',
        position: i,
        character: '\\0',
        description: 'Null byte can terminate strings unexpectedly',
      }
    }

    // Check for newlines and Unicode separators outside quotes
    if (!isInsideQuotes(prevState)) {
      // Regular newline
      if (char === '\n') {
        return {
          found: true,
          type: 'newline',
          position: i,
          character: '\\n',
          description: 'Newline outside quotes acts as command separator',
        }
      }

      // Carriage return (could be part of \r\n or standalone)
      if (char === '\r') {
        return {
          found: true,
          type: 'newline',
          position: i,
          character: '\\r',
          description: 'Carriage return outside quotes could separate commands',
        }
      }

      // Unicode separators
      const unicodeName = UNICODE_SEPARATORS[char]
      if (unicodeName && char !== '\n' && char !== '\r') {
        return {
          found: true,
          type: 'unicode_separator',
          position: i,
          character: char,
          description: `${unicodeName} can act as command separator`,
        }
      }
    }
  }

  return { found: false }
}

// ============================================================================
// Command Segmentation
// ============================================================================

/**
 * Parse a command string into segments split by shell operators
 * Respects quote context (doesn't split inside quotes)
 */
export function parseCommandSegments(command: string): CommandSegment[] {
  const segments: CommandSegment[] = []
  let state = createQuoteState()
  let currentStart = 0
  let i = 0

  while (i < command.length) {
    const char = command[i]
    state = processChar(char, state)

    // Only look for separators outside quotes
    if (!isInsideQuotes(state) && !state.isEscaped) {
      // Check for multi-char operators first (&&, ||, |&)
      const twoChar = command.slice(i, i + 2)
      if (twoChar === '&&' || twoChar === '||') {
        const segment = command.slice(currentStart, i).trim()
        if (segment) {
          segments.push({
            command: segment,
            separator: twoChar,
            startPos: currentStart,
            endPos: i,
          })
        }
        i += 2
        currentStart = i
        // Reset state after separator
        state = createQuoteState()
        state.position = i
        continue
      }

      // Single char operators (|, ;)
      if (char === '|' || char === ';') {
        const segment = command.slice(currentStart, i).trim()
        if (segment) {
          segments.push({
            command: segment,
            separator: char,
            startPos: currentStart,
            endPos: i,
          })
        }
        i += 1
        currentStart = i
        // Reset state after separator
        state = createQuoteState()
        state.position = i
        continue
      }
    }

    i++
  }

  // Add final segment
  const finalSegment = command.slice(currentStart).trim()
  if (finalSegment) {
    segments.push({
      command: finalSegment,
      startPos: currentStart,
      endPos: command.length,
    })
  }

  return segments
}

/**
 * Detect redirect operators in a command
 */
export function detectRedirects(command: string): string[] {
  const found: string[] = []
  let state = createQuoteState()

  for (let i = 0; i < command.length; i++) {
    const char = command[i]
    state = processChar(char, state)

    // Only detect outside quotes
    if (!isInsideQuotes(state) && !state.isEscaped) {
      for (const op of REDIRECT_OPERATORS) {
        if (command.slice(i, i + op.length) === op) {
          found.push(op)
          break
        }
      }
    }
  }

  return found
}

/**
 * Extract subshell contents: $(...) and (...)
 */
export function extractSubshells(command: string): string[] {
  const subshells: string[] = []
  let state = createQuoteState()
  let i = 0

  while (i < command.length) {
    const char = command[i]
    state = processChar(char, state)

    // Only extract outside single quotes
    if (!state.inSingleQuote && !state.isEscaped) {
      // $(...) subshell
      if (char === '$' && command[i + 1] === '(') {
        const content = extractParenContent(command, i + 1)
        if (content) {
          subshells.push(content)
          i += content.length + 3 // Skip $( + content + )
          continue
        }
      }

      // (...) subshell (only at start of segment or after operator)
      if (char === '(' && (i === 0 || /[\s;&|]/.test(command[i - 1] ?? ''))) {
        const content = extractParenContent(command, i)
        if (content) {
          subshells.push(content)
          i += content.length + 2 // Skip ( + content + )
          continue
        }
      }
    }

    i++
  }

  return subshells
}

/**
 * Helper to extract content within balanced parentheses
 */
function extractParenContent(command: string, startIndex: number): string | null {
  if (command[startIndex] !== '(') return null

  let depth = 0
  let i = startIndex

  while (i < command.length) {
    if (command[i] === '(') depth++
    if (command[i] === ')') depth--

    if (depth === 0) {
      return command.slice(startIndex + 1, i)
    }
    i++
  }

  return null // Unbalanced
}
