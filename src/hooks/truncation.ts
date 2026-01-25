/**
 * Delta9 Output Truncation Hooks
 *
 * Smart truncation for tool outputs to manage context window:
 * - Per-tool truncation limits
 * - Intelligent truncation (preserve structure)
 * - Head/tail preservation
 * - Truncation warnings
 */

import type { MissionState } from '../mission/state.js'

// =============================================================================
// Types
// =============================================================================

export interface TruncationHooksInput {
  /** Mission state instance */
  state: MissionState
  /** Logger function */
  log: (level: string, message: string, data?: Record<string, unknown>) => void
  /** Global truncation config */
  config?: TruncationConfig
}

export interface TruncationConfig {
  /** Default max output length (characters) */
  defaultLimit: number
  /** Per-tool limits (override default) */
  toolLimits: Record<string, number>
  /** Preserve head/tail balance (0-1, 0.5 = equal) */
  headTailBalance: number
  /** Enable smart truncation (try to preserve structure) */
  smartTruncation: boolean
  /** Truncation warning threshold (chars) */
  warningThreshold: number
}

export interface TruncationInput {
  /** Tool name */
  tool: string
  /** Original output */
  output: string
  /** Output metadata */
  metadata?: Record<string, unknown>
}

export interface TruncationOutput {
  /** Truncated output */
  output: string
  /** Whether output was truncated */
  wasTruncated: boolean
  /** Original length */
  originalLength: number
  /** Truncated length */
  truncatedLength: number
}

// =============================================================================
// Default Configuration
// =============================================================================

const DEFAULT_CONFIG: TruncationConfig = {
  defaultLimit: 32000, // 32K default
  toolLimits: {
    // Read tools - larger limit for context
    Read: 50000,
    Grep: 20000,
    Glob: 10000,
    // Search tools - smaller limit
    search_files: 15000,
    search_code: 15000,
    // Test output - moderate limit
    run_tests: 25000,
    check_lint: 15000,
    check_types: 15000,
    // Bash - moderate limit
    Bash: 20000,
    bash: 20000,
    // File tools - small limit
    Write: 5000,
    Edit: 5000,
    // Debug tools - larger for debugging
    delta9_diagnostics: 40000,
    // Delegation - smaller (status only)
    delegate_task: 10000,
    background_output: 20000,
    // Council - moderate
    consult_council: 25000,
    council_status: 15000,
  },
  headTailBalance: 0.3, // Preserve 30% from start, 70% from end
  smartTruncation: true,
  warningThreshold: 20000,
}

// =============================================================================
// Truncation Stats
// =============================================================================

interface TruncationStats {
  totalTruncations: number
  totalCharsSaved: number
  byTool: Map<string, { count: number; charsSaved: number }>
}

const stats: TruncationStats = {
  totalTruncations: 0,
  totalCharsSaved: 0,
  byTool: new Map(),
}

/**
 * Get truncation statistics
 */
export function getTruncationStats(): {
  totalTruncations: number
  totalCharsSaved: number
  byTool: Record<string, { count: number; charsSaved: number }>
} {
  return {
    totalTruncations: stats.totalTruncations,
    totalCharsSaved: stats.totalCharsSaved,
    byTool: Object.fromEntries(stats.byTool),
  }
}

/**
 * Clear truncation stats (for testing)
 */
export function clearTruncationStats(): void {
  stats.totalTruncations = 0
  stats.totalCharsSaved = 0
  stats.byTool.clear()
}

// =============================================================================
// Truncation Functions
// =============================================================================

/**
 * Truncate output based on tool-specific limits
 */
export function truncateOutput(
  input: TruncationInput,
  config: TruncationConfig = DEFAULT_CONFIG
): TruncationOutput {
  const { tool, output } = input
  const originalLength = output.length

  // Get limit for this tool
  const limit = config.toolLimits[tool] ?? config.defaultLimit

  // No truncation needed
  if (originalLength <= limit) {
    return {
      output,
      wasTruncated: false,
      originalLength,
      truncatedLength: originalLength,
    }
  }

  // Perform truncation
  let truncated: string

  if (config.smartTruncation) {
    truncated = smartTruncate(output, limit, config.headTailBalance)
  } else {
    truncated = simpleTruncate(output, limit, config.headTailBalance)
  }

  // Update stats
  const charsSaved = originalLength - truncated.length
  stats.totalTruncations++
  stats.totalCharsSaved += charsSaved

  const toolStats = stats.byTool.get(tool) ?? { count: 0, charsSaved: 0 }
  toolStats.count++
  toolStats.charsSaved += charsSaved
  stats.byTool.set(tool, toolStats)

  return {
    output: truncated,
    wasTruncated: true,
    originalLength,
    truncatedLength: truncated.length,
  }
}

/**
 * Simple truncation with head/tail preservation
 */
function simpleTruncate(output: string, limit: number, headTailBalance: number): string {
  const headSize = Math.floor(limit * headTailBalance)
  const tailSize = limit - headSize - 100 // Reserve 100 chars for indicator

  const head = output.substring(0, headSize)
  const tail = output.substring(output.length - tailSize)

  const truncationMarker = `\n\n... [${output.length - headSize - tailSize} characters truncated] ...\n\n`

  return head + truncationMarker + tail
}

/**
 * Smart truncation that tries to preserve structure
 */
function smartTruncate(output: string, limit: number, headTailBalance: number): string {
  // Try to detect structure
  const isJSON = output.trim().startsWith('{') || output.trim().startsWith('[')
  const isCode = output.includes('function ') || output.includes('const ') || output.includes('class ')
  const hasLineStructure = output.includes('\n')

  if (isJSON) {
    return truncateJSON(output, limit)
  }

  if (hasLineStructure) {
    return truncateByLines(output, limit, headTailBalance)
  }

  if (isCode) {
    return truncateCode(output, limit, headTailBalance)
  }

  // Fall back to simple truncation
  return simpleTruncate(output, limit, headTailBalance)
}

/**
 * Truncate JSON while preserving structure hints
 */
function truncateJSON(output: string, limit: number): string {
  try {
    const parsed = JSON.parse(output)

    // If it's an array, truncate array elements
    if (Array.isArray(parsed)) {
      const headerSize = 200
      const footerSize = 100

      if (output.length <= limit) return output

      // Estimate how many elements we can keep
      const avgElementSize = output.length / parsed.length
      const targetElements = Math.floor((limit - headerSize - footerSize) / avgElementSize)

      if (targetElements < parsed.length) {
        const truncated = parsed.slice(0, targetElements)
        const result = JSON.stringify(truncated, null, 2)
        return result + `\n\n// ... ${parsed.length - targetElements} more items truncated`
      }
    }

    // For objects, just truncate the stringified version
    const stringified = JSON.stringify(parsed, null, 2)
    if (stringified.length <= limit) return stringified

    return simpleTruncate(stringified, limit, 0.3)
  } catch {
    // Not valid JSON, fall back to simple
    return simpleTruncate(output, limit, 0.3)
  }
}

/**
 * Truncate by lines, preserving line boundaries
 */
function truncateByLines(output: string, limit: number, headTailBalance: number): string {
  const lines = output.split('\n')

  if (output.length <= limit) return output

  // Calculate approximate lines to keep
  const avgLineLength = output.length / lines.length
  const targetLines = Math.floor(limit / avgLineLength)

  const headLines = Math.floor(targetLines * headTailBalance)
  const tailLines = targetLines - headLines - 1 // -1 for truncation indicator

  if (headLines + tailLines >= lines.length) {
    return output
  }

  const head = lines.slice(0, headLines).join('\n')
  const tail = lines.slice(-tailLines).join('\n')

  const truncatedLines = lines.length - headLines - tailLines
  const truncationMarker = `\n\n... [${truncatedLines} lines truncated] ...\n\n`

  return head + truncationMarker + tail
}

/**
 * Truncate code while preserving function boundaries
 */
function truncateCode(output: string, limit: number, headTailBalance: number): string {
  // For now, use line-based truncation
  // Could be enhanced to preserve function/class boundaries
  return truncateByLines(output, limit, headTailBalance)
}

// =============================================================================
// Hook Factory
// =============================================================================

/**
 * Create truncation hooks
 */
export function createTruncationHooks(input: TruncationHooksInput): {
  'tool.execute.after': (
    toolInput: { tool: string; sessionID: string; callID: string },
    output: { output: string; metadata: Record<string, unknown>; title: string; error?: Error }
  ) => Promise<void>
} {
  const { log, config } = input
  const effectiveConfig = config ?? DEFAULT_CONFIG

  return {
    'tool.execute.after': async (toolInput, output) => {
      const result = truncateOutput(
        {
          tool: toolInput.tool,
          output: output.output,
          metadata: output.metadata,
        },
        effectiveConfig
      )

      if (result.wasTruncated) {
        // Replace output with truncated version
        output.output = result.output

        // Add truncation notice to metadata
        output.metadata = {
          ...output.metadata,
          truncation: {
            wasTruncated: true,
            originalLength: result.originalLength,
            truncatedLength: result.truncatedLength,
            charsSaved: result.originalLength - result.truncatedLength,
          },
        }

        // Log if above warning threshold
        if (result.originalLength > effectiveConfig.warningThreshold) {
          log('warn', `Output truncated for ${toolInput.tool}`, {
            originalLength: result.originalLength,
            truncatedLength: result.truncatedLength,
            charsSaved: result.originalLength - result.truncatedLength,
          })
        } else {
          log('debug', `Output truncated for ${toolInput.tool}`, {
            originalLength: result.originalLength,
            truncatedLength: result.truncatedLength,
          })
        }
      }
    },
  }
}
