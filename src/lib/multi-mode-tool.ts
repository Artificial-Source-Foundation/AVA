/**
 * Delta9 Multi-Mode Tool Dispatcher
 *
 * Utility for creating tools that support multiple modes/operations.
 * Consolidates related tools into a single unified interface.
 *
 * Pattern from: opencode-mem single tool with mode dispatch
 *
 * Example:
 * ```typescript
 * // Instead of: memory_store, memory_search, memory_forget, memory_list
 * // Use: memory tool with mode parameter
 *
 * const memoryTool = createMultiModeTool({
 *   name: 'memory',
 *   description: 'Unified memory operations',
 *   modes: {
 *     store: {
 *       description: 'Store a memory',
 *       params: { content: z.string(), label: z.string() },
 *       handler: async (args) => storeMemory(args),
 *     },
 *     search: {
 *       description: 'Search memories',
 *       params: { query: z.string() },
 *       handler: async (args) => searchMemory(args.query),
 *     },
 *     forget: {
 *       description: 'Delete a memory',
 *       params: { id: z.string() },
 *       handler: async (args) => forgetMemory(args.id),
 *     },
 *     list: {
 *       description: 'List all memories',
 *       params: {},
 *       handler: async () => listMemories(),
 *     },
 *   },
 * })
 * ```
 */

import { getNamedLogger } from './logger.js'

const log = getNamedLogger('multi-mode-tool')

// =============================================================================
// Types
// =============================================================================

/**
 * Mode definition for a multi-mode tool
 */
export interface ModeDefinition<TParams = Record<string, unknown>, TResult = unknown> {
  /** Mode description */
  description: string
  /** Mode-specific parameters */
  params: TParams
  /** Mode handler function */
  handler: (args: TParams, context?: unknown) => Promise<TResult>
  /** Optional validation function */
  validate?: (args: TParams) => string | null
}

/**
 * Multi-mode tool configuration
 */
export interface MultiModeToolConfig<TModes extends Record<string, ModeDefinition>> {
  /** Tool name */
  name: string
  /** Tool description */
  description: string
  /** Available modes */
  modes: TModes
  /** Default mode (if not specified) */
  defaultMode?: keyof TModes
}

/**
 * Mode dispatch result
 */
export interface ModeDispatchResult<T = unknown> {
  /** Whether dispatch succeeded */
  success: boolean
  /** Mode that was dispatched */
  mode: string
  /** Result from handler */
  result?: T
  /** Error message if failed */
  error?: string
}

// =============================================================================
// Multi-Mode Tool Dispatcher
// =============================================================================

/**
 * Multi-mode tool dispatcher
 *
 * Creates a unified interface for related operations.
 */
export class MultiModeDispatcher<TModes extends Record<string, ModeDefinition>> {
  private config: MultiModeToolConfig<TModes>
  private modes: TModes

  constructor(config: MultiModeToolConfig<TModes>) {
    this.config = config
    this.modes = config.modes
  }

  /**
   * Get tool name
   */
  getName(): string {
    return this.config.name
  }

  /**
   * Get tool description
   */
  getDescription(): string {
    return this.config.description
  }

  /**
   * Get available mode names
   */
  getModeNames(): string[] {
    return Object.keys(this.modes)
  }

  /**
   * Get mode definition
   */
  getMode(modeName: string): ModeDefinition | null {
    return this.modes[modeName as keyof TModes] ?? null
  }

  /**
   * Check if mode exists
   */
  hasMode(modeName: string): boolean {
    return modeName in this.modes
  }

  /**
   * Get default mode
   */
  getDefaultMode(): string | null {
    return this.config.defaultMode as string | null ?? null
  }

  /**
   * Dispatch to a specific mode
   */
  async dispatch<TMode extends keyof TModes>(
    mode: TMode,
    args: TModes[TMode]['params'] extends Record<string, unknown>
      ? TModes[TMode]['params']
      : Record<string, unknown>,
    context?: unknown
  ): Promise<ModeDispatchResult> {
    const modeConfig = this.modes[mode]

    if (!modeConfig) {
      return {
        success: false,
        mode: String(mode),
        error: `Unknown mode: ${String(mode)}. Available: ${this.getModeNames().join(', ')}`,
      }
    }

    // Validate if validator exists
    if (modeConfig.validate) {
      const validationError = modeConfig.validate(args as TModes[TMode]['params'])
      if (validationError) {
        return {
          success: false,
          mode: String(mode),
          error: validationError,
        }
      }
    }

    try {
      log.debug(`Dispatching ${this.config.name}.${String(mode)}`, { args })
      const result = await modeConfig.handler(args as TModes[TMode]['params'], context)

      return {
        success: true,
        mode: String(mode),
        result,
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      log.error(`Mode dispatch failed: ${this.config.name}.${String(mode)}`, { error: message })

      return {
        success: false,
        mode: String(mode),
        error: message,
      }
    }
  }

  /**
   * Build combined description for all modes
   */
  buildCombinedDescription(): string {
    const parts: string[] = [this.config.description, '', 'Available modes:']

    for (const [modeName, mode] of Object.entries(this.modes)) {
      parts.push(`- ${modeName}: ${mode.description}`)
    }

    return parts.join('\n')
  }

  /**
   * Build help text for a specific mode
   */
  buildModeHelp(modeName: string): string | null {
    const mode = this.getMode(modeName)
    if (!mode) return null

    return `${this.config.name} ${modeName}: ${mode.description}`
  }
}

// =============================================================================
// Factory Functions
// =============================================================================

/**
 * Create a multi-mode tool dispatcher
 */
export function createMultiModeDispatcher<TModes extends Record<string, ModeDefinition>>(
  config: MultiModeToolConfig<TModes>
): MultiModeDispatcher<TModes> {
  return new MultiModeDispatcher(config)
}

/**
 * Create a simple mode definition
 */
export function defineMode<TParams = Record<string, unknown>, TResult = unknown>(
  description: string,
  handler: (args: TParams, context?: unknown) => Promise<TResult>,
  options?: {
    params?: TParams
    validate?: (args: TParams) => string | null
  }
): ModeDefinition<TParams, TResult> {
  return {
    description,
    params: options?.params ?? ({} as TParams),
    handler,
    validate: options?.validate,
  }
}

// =============================================================================
// Mode Execution Helpers
// =============================================================================

/**
 * Execute a mode with automatic error handling
 */
export async function executeMode<T>(
  dispatcher: MultiModeDispatcher<Record<string, ModeDefinition>>,
  mode: string,
  args: Record<string, unknown>,
  context?: unknown
): Promise<{ success: boolean; data?: T; error?: string }> {
  const result = await dispatcher.dispatch(mode, args, context)

  if (result.success) {
    return {
      success: true,
      data: result.result as T,
    }
  }

  return {
    success: false,
    error: result.error,
  }
}

/**
 * Create a mode router function for tool handlers
 */
export function createModeRouter<TModes extends Record<string, ModeDefinition>>(
  dispatcher: MultiModeDispatcher<TModes>
): (args: { mode: string } & Record<string, unknown>, context?: unknown) => Promise<string> {
  return async (args, context) => {
    const { mode, ...rest } = args
    const result = await dispatcher.dispatch(mode as keyof TModes, rest, context)

    return JSON.stringify({
      success: result.success,
      mode: result.mode,
      ...(result.success ? { result: result.result } : { error: result.error }),
    })
  }
}

// =============================================================================
// Utility
// =============================================================================

/**
 * Validate mode exists
 */
export function validateMode(
  dispatcher: MultiModeDispatcher<Record<string, ModeDefinition>>,
  mode: string
): string | null {
  if (!dispatcher.hasMode(mode)) {
    return `Unknown mode: ${mode}. Available: ${dispatcher.getModeNames().join(', ')}`
  }
  return null
}

/**
 * Get mode enum for schema
 */
export function getModeEnum(
  dispatcher: MultiModeDispatcher<Record<string, ModeDefinition>>
): readonly string[] {
  return Object.freeze(dispatcher.getModeNames())
}
