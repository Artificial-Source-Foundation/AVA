/**
 * Delta9 Output Guardrails
 *
 * Prevents large tool outputs from consuming entire context.
 * Inspired by SWARM's output-guardrails.ts pattern.
 *
 * Philosophy: "Protect context budget. Truncate smartly."
 */

// =============================================================================
// Configuration
// =============================================================================

/** Default character limit for most tools */
const DEFAULT_LIMIT = 32000

/** Higher limit for code/document tools */
const CODE_TOOL_LIMIT = 64000

/** Tools that should skip truncation entirely */
const SKIP_TOOLS = [
  // Delta9 coordination tools (need full context)
  'mission_status',
  'mission_create',
  'council_convene',
  'consult_council',
  'delegate_task',
  'dispatch_task',
  'validation_result',
  // Background task management
  'background_output',
  'background_list',
]

/** Tools that get higher limits (code-focused) */
const CODE_TOOLS = [
  'Read',
  'read',
  'Grep',
  'grep',
  'Glob',
  'glob',
  'file_read',
  'search',
]

// =============================================================================
// Types
// =============================================================================

export interface GuardrailResult {
  /** Original output length */
  originalLength: number
  /** Truncated output length */
  truncatedLength: number
  /** Whether output was truncated */
  wasTruncated: boolean
  /** The guardrailed output */
  output: string
}

// =============================================================================
// Core Functions
// =============================================================================

/**
 * Apply guardrails to tool output.
 *
 * @param tool - Tool name
 * @param output - Raw tool output
 * @returns Guardrailed result
 */
export function guardrailOutput(tool: string, output: string): GuardrailResult {
  const originalLength = output.length

  // Skip guardrails for certain tools
  if (shouldSkipGuardrails(tool)) {
    return {
      originalLength,
      truncatedLength: originalLength,
      wasTruncated: false,
      output,
    }
  }

  // Determine limit based on tool type
  const limit = getToolLimit(tool)

  // Check if truncation needed
  if (output.length <= limit) {
    return {
      originalLength,
      truncatedLength: originalLength,
      wasTruncated: false,
      output,
    }
  }

  // Apply smart truncation
  const truncated = truncateWithBoundaries(output, limit)

  return {
    originalLength,
    truncatedLength: truncated.length,
    wasTruncated: true,
    output: truncated,
  }
}

/**
 * Check if tool should skip guardrails.
 */
export function shouldSkipGuardrails(tool: string): boolean {
  return SKIP_TOOLS.some((t) => t.toLowerCase() === tool.toLowerCase())
}

/**
 * Get character limit for a tool.
 */
export function getToolLimit(tool: string): number {
  if (CODE_TOOLS.some((t) => t.toLowerCase() === tool.toLowerCase())) {
    return CODE_TOOL_LIMIT
  }
  return DEFAULT_LIMIT
}

// =============================================================================
// Smart Truncation
// =============================================================================

/**
 * Truncate text while preserving structure.
 *
 * Preserves:
 * - Complete JSON objects/arrays
 * - Complete code blocks (```)
 * - Complete markdown sections
 * - Word boundaries
 */
export function truncateWithBoundaries(text: string, maxChars: number): string {
  if (text.length <= maxChars) {
    return text
  }

  let cutPoint = maxChars

  // Strategy 1: Try to end at a code block boundary
  const codeBlockEnd = findSafeCodeBlockCut(text, maxChars)
  if (codeBlockEnd > maxChars * 0.7) {
    cutPoint = codeBlockEnd
  }

  // Strategy 2: Try to end at a JSON boundary
  const jsonEnd = findSafeJsonCut(text, cutPoint)
  if (jsonEnd > cutPoint * 0.8) {
    cutPoint = jsonEnd
  }

  // Strategy 3: Try to end at a paragraph boundary
  const paragraphEnd = findParagraphBoundary(text, cutPoint)
  if (paragraphEnd > cutPoint * 0.8) {
    cutPoint = paragraphEnd
  }

  // Strategy 4: At least end at a line boundary
  const lineEnd = text.lastIndexOf('\n', cutPoint)
  if (lineEnd > cutPoint * 0.9) {
    cutPoint = lineEnd
  }

  // Strategy 5: At minimum, end at a word boundary
  const spaceEnd = text.lastIndexOf(' ', cutPoint)
  if (spaceEnd > cutPoint * 0.95) {
    cutPoint = spaceEnd
  }

  const truncated = text.slice(0, cutPoint)
  const omittedChars = text.length - cutPoint

  return `${truncated}\n\n[OUTPUT TRUNCATED: ${omittedChars.toLocaleString()} characters omitted. Use more specific queries to see full content.]`
}

// =============================================================================
// Boundary Finders
// =============================================================================

/**
 * Find a safe cut point that doesn't break code blocks.
 */
function findSafeCodeBlockCut(text: string, maxChars: number): number {
  const searchArea = text.slice(0, maxChars)

  // Find the last complete code block
  let lastBlockEnd = 0
  let pos = 0

  while (pos < searchArea.length) {
    const blockStart = searchArea.indexOf('```', pos)
    if (blockStart === -1) break

    const blockEnd = searchArea.indexOf('```', blockStart + 3)
    if (blockEnd === -1) break

    lastBlockEnd = blockEnd + 3
    pos = blockEnd + 3
  }

  return lastBlockEnd
}

/**
 * Find a safe cut point that doesn't break JSON.
 */
function findSafeJsonCut(text: string, maxChars: number): number {
  const searchArea = text.slice(0, maxChars)

  // Simple heuristic: find last matching brace/bracket
  let braceDepth = 0
  let bracketDepth = 0
  let lastSafePoint = 0

  for (let i = 0; i < searchArea.length; i++) {
    const char = searchArea[i]

    if (char === '{') braceDepth++
    else if (char === '}') {
      braceDepth--
      if (braceDepth === 0 && bracketDepth === 0) {
        lastSafePoint = i + 1
      }
    } else if (char === '[') bracketDepth++
    else if (char === ']') {
      bracketDepth--
      if (braceDepth === 0 && bracketDepth === 0) {
        lastSafePoint = i + 1
      }
    }
  }

  return lastSafePoint
}

/**
 * Find a paragraph boundary (double newline).
 */
function findParagraphBoundary(text: string, maxChars: number): number {
  const searchArea = text.slice(0, maxChars)
  return searchArea.lastIndexOf('\n\n') + 2
}
