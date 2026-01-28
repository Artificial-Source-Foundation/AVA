/**
 * Delta9 Configuration Types
 *
 * Configuration is loaded from:
 * 1. Defaults (hardcoded)
 * 2. Global: ~/.config/opencode/delta9.json
 * 3. Project: .delta9/config.json (overrides)
 */

// =============================================================================
// Commander Configuration
// =============================================================================

export interface CommanderConfig {
  /** Model for planning phase */
  model: string
  /** Fallback models if primary fails */
  fallbacks: string[]
  /** Temperature for planning (0-1) */
  temperature: number
  /** Model for dispatch/execution coordination */
  dispatchModel: string
}

// =============================================================================
// Thinking/Reasoning Configuration (ARCH-5)
// =============================================================================

export interface ThinkingConfig {
  /** OpenAI reasoning mode: 'standard' | 'high' | 'xhigh' */
  reasoningMode?: 'standard' | 'high' | 'xhigh'

  /** Claude extended thinking budget (tokens) */
  thinkingBudget?: number

  /** Gemini deep think mode */
  deepThink?: boolean

  /** DeepSeek R1: trigger thinking with <think> prefix */
  triggerThinking?: boolean
}

// =============================================================================
// Council Configuration
// =============================================================================

export type CouncilMode = 'none' | 'quick' | 'standard' | 'xhigh'

export interface OracleConfig {
  /** Strategic Advisor display name (Cipher, Vector, Apex, Aegis, Razor, Oracle) */
  name: string
  /** Model to use (user configurable - any model provider) */
  model: string
  /** Fallback models if primary fails */
  fallbacks: string[]
  /** Whether this advisor is enabled */
  enabled: boolean
  /** Specialty area */
  specialty:
    | 'architecture'
    | 'logic'
    | 'ui'
    | 'performance'
    | 'security'
    | 'simplification'
    | 'innovation'
    | 'general'
  /** Temperature setting (0-1, defines personality decisiveness) */
  temperature?: number
  /** Thinking/reasoning configuration (provider-specific) */
  thinking?: ThinkingConfig
}

export interface CouncilConfig {
  /** Whether council is enabled */
  enabled: boolean
  /** Default council mode when auto-detecting */
  defaultMode: CouncilMode
  /** Whether to auto-detect complexity */
  autoDetectComplexity: boolean
  /** Council members */
  members: OracleConfig[]
  /** Run oracles in parallel */
  parallel: boolean
  /** Require consensus from oracles */
  requireConsensus: boolean
  /** Minimum oracle responses required */
  minResponses: number
  /** Timeout for oracle responses in seconds */
  timeoutSeconds: number
}

// =============================================================================
// Operator Configuration (3-Tier Marine System)
// =============================================================================

export interface OperatorConfig {
  /** Tier 1: Marine Private - simple tasks (Sonnet) */
  tier1Model: string
  /** Fallback models for tier 1 */
  tier1Fallbacks: string[]
  /** Tier 1 thinking config (usually none - fast execution) */
  tier1Thinking?: ThinkingConfig

  /** Tier 2: Marine Sergeant - moderate tasks (GPT-5.2) */
  tier2Model: string
  /** Fallback models for tier 2 */
  tier2Fallbacks: string[]
  /** Tier 2 thinking config (high reasoning) */
  tier2Thinking?: ThinkingConfig

  /** Tier 3: Delta Force - critical tasks (Opus) */
  tier3Model: string
  /** Fallback models for tier 3 */
  tier3Fallbacks: string[]
  /** Tier 3 thinking config (extended thinking) */
  tier3Thinking?: ThinkingConfig

  /** Maximum parallel operators */
  maxParallel: number
  /** Maximum retry attempts */
  retryLimit: number
  /** Whether operators can invoke support agents */
  canInvokeSupport: boolean
}

// =============================================================================
// Validator Configuration
// =============================================================================

export interface ValidatorConfig {
  /** Model for validation */
  model: string
  /** Fallback models if primary fails */
  fallbacks: string[]
  /** Strict mode - more thorough checking */
  strictMode: boolean
  /** Run tests as part of validation */
  runTests: boolean
  /** Check linting as part of validation */
  checkLinting: boolean
}

// =============================================================================
// Patcher Configuration
// =============================================================================

export interface PatcherConfig {
  /** Model for quick patches */
  model: string
  /** Fallback models if primary fails */
  fallbacks: string[]
  /** Maximum lines patcher can change */
  maxLines: number
}

// =============================================================================
// Support Agent Configuration
// =============================================================================

export interface ScoutConfig {
  model: string
  fallbacks: string[]
  timeoutSeconds: number
}

export interface IntelConfig {
  model: string
  fallbacks: string[]
  sources: ('docs' | 'github' | 'web')[]
}

export interface StrategistConfig {
  model: string
  fallbacks: string[]
  invokeThreshold: 'simple' | 'moderate' | 'complex'
}

export interface UiOpsConfig {
  model: string
  fallbacks: string[]
  styleSystem: 'tailwind' | 'css' | 'scss' | 'styled-components'
}

export interface ScribeConfig {
  model: string
  fallbacks: string[]
  format: 'markdown' | 'jsdoc' | 'tsdoc'
}

export interface QaConfig {
  model: string
  fallbacks: string[]
  frameworkDetect: boolean
}

/**
 * Support Agent Configuration
 *
 * Delta Team Support Agents (7 agents):
 * - RECON (scout): Fast codebase reconnaissance
 * - SIGINT (intel): Intelligence research & documentation
 * - TACCOM (strategist): Tactical command advisor
 * - SURGEON (patcher): Quick surgical fixes
 * - SENTINEL (qa): Quality assurance guardian
 * - SCRIBE (scribe): Documentation writer
 * - FACADE (uiOps): Frontend operations specialist
 *
 * Note: SPECTRE (optics) removed - redundant with FACADE
 */
export interface SupportConfig {
  scout: ScoutConfig
  intel: IntelConfig
  strategist: StrategistConfig
  uiOps: UiOpsConfig
  scribe: ScribeConfig
  qa: QaConfig
}

// =============================================================================
// Mission Configuration
// =============================================================================

export interface MissionSettings {
  /** Auto-create checkpoints */
  autoCheckpoint: boolean
  /** When to create checkpoints */
  checkpointOn: 'objective_complete' | 'task_complete' | 'never'
  /** State directory name */
  stateDir: string
  /** Enable history logging */
  historyEnabled: boolean
}

// =============================================================================
// Memory Configuration
// =============================================================================

export interface MemoryConfig {
  /** Enable cross-session memory */
  enabled: boolean
  /** Learn from failures */
  learnFromFailures: boolean
  /** Learn from successes */
  learnFromSuccesses: boolean
  /** Maximum memory entries */
  maxEntries: number
}

// =============================================================================
// Budget Configuration
// =============================================================================

export interface BudgetConfig {
  /** Enable budget tracking */
  enabled: boolean
  /** Default budget limit in dollars */
  defaultLimit: number
  /** Warn at this percentage (0-1) */
  warnAt: number
  /** Pause at this percentage (0-1) */
  pauseAt: number
  /** Hard limit at this percentage (0-1) - triggers abort */
  hardLimitAt: number
  /** Track costs by agent type */
  trackByAgent: boolean
}

// =============================================================================
// Notification Configuration
// =============================================================================

export interface NotificationConfig {
  /** Enable notifications */
  enabled: boolean
  /** Discord webhook URL */
  discordWebhook: string | null
  /** Slack webhook URL */
  slackWebhook: string | null
  /** Events to notify on */
  onEvents: ('mission_complete' | 'validation_failed' | 'budget_warning' | 'needs_input')[]
}

// =============================================================================
// UI Configuration
// =============================================================================

export interface UiConfig {
  /** Show progress indicators */
  showProgress: boolean
  /** Show cost tracking */
  showCost: boolean
  /** Verbose logging */
  verboseLogs: boolean
}

// =============================================================================
// Seamless Integration Configuration
// =============================================================================

export interface SeamlessConfig {
  /** Replace default build agent */
  replaceBuild: boolean
  /** Replace default plan agent */
  replacePlan: boolean
  /** Enable keyword detection */
  keywordDetection: boolean
  /** Keywords that trigger specific modes */
  keywords: {
    councilXhigh: string[]
    councilNone: string[]
    forcePlan: string[]
  }
}

// =============================================================================
// Full Configuration
// =============================================================================

export interface Delta9Config {
  commander: CommanderConfig
  council: CouncilConfig
  operators: OperatorConfig
  validator: ValidatorConfig
  patcher: PatcherConfig
  support: SupportConfig
  mission: MissionSettings
  memory: MemoryConfig
  budget: BudgetConfig
  notifications: NotificationConfig
  ui: UiConfig
  seamless: SeamlessConfig
}

// =============================================================================
// Default Configuration
// =============================================================================

export const DEFAULT_CONFIG: Delta9Config = {
  commander: {
    model: 'anthropic/claude-opus-4-5',
    fallbacks: ['openai/gpt-5.2-codex', 'google/gemini-3-pro-preview'],
    temperature: 0.7,
    dispatchModel: 'anthropic/claude-sonnet-4-5',
  },
  council: {
    enabled: true,
    defaultMode: 'standard',
    autoDetectComplexity: true,
    // Strategic Advisors (6 members) - diverse providers and specialties
    members: [
      {
        name: 'Cipher',
        model: 'openai/gpt-5.2-codex', // Changed from Opus - architecture needs code understanding
        fallbacks: ['anthropic/claude-opus-4-5', 'google/gemini-3-pro-preview'],
        enabled: true,
        specialty: 'architecture',
        temperature: 0.2, // Low for precision
        thinking: {
          reasoningMode: 'xhigh', // Architecture needs deep thinking
        },
      },
      {
        name: 'Vector',
        model: 'openrouter/deepseek/deepseek-r1', // DeepSeek R1 for logic
        fallbacks: ['anthropic/claude-opus-4-5', 'openai/gpt-5.2-codex'],
        enabled: true,
        specialty: 'logic',
        temperature: 0.6, // R1 requires 0.6
        thinking: {
          triggerThinking: true, // R1 thinking mode
        },
      },
      {
        name: 'Apex',
        model: 'anthropic/claude-opus-4-5', // Keep Opus for performance analysis
        fallbacks: ['openai/gpt-5.2-codex', 'google/gemini-3-pro-preview'],
        enabled: true,
        specialty: 'performance',
        temperature: 0.3,
        thinking: {
          thinkingBudget: 16000, // Extended thinking for performance analysis
        },
      },
      {
        name: 'Aegis', // NEW - Security & Risk Advisor
        model: 'anthropic/claude-opus-4-5',
        fallbacks: ['openai/gpt-5.2-codex', 'google/gemini-3-pro-preview'],
        enabled: true,
        specialty: 'security',
        temperature: 0.3,
        thinking: {
          thinkingBudget: 32000, // Max thinking for security (critical)
        },
      },
      {
        name: 'Razor', // NEW - Simplification Advisor
        model: 'google/gemini-3-pro-preview',
        fallbacks: ['anthropic/claude-sonnet-4-5', 'openai/gpt-5.2-codex'],
        enabled: true,
        specialty: 'simplification',
        temperature: 0.4,
        thinking: {
          deepThink: false, // KISS advisor shouldn't overthink
        },
      },
      {
        name: 'Oracle', // NEW - Innovation Advisor (replaces Prism)
        model: 'moonshot/kimi-k2.5', // Kimi for creative/innovative thinking
        fallbacks: ['anthropic/claude-opus-4-5', 'openai/gpt-5.2-codex'],
        enabled: true,
        specialty: 'innovation',
        temperature: 0.7, // Creative, higher variance
        // Kimi's Agent Swarm handles its own reasoning
      },
    ],
    parallel: true,
    requireConsensus: false,
    minResponses: 2,
    timeoutSeconds: 120,
  },
  operators: {
    // Tier 1: Marine Private (simple tasks)
    tier1Model: 'anthropic/claude-sonnet-4-5',
    tier1Fallbacks: ['openai/gpt-5.2-codex', 'google/gemini-3-flash-preview'],
    // No thinking for fast execution

    // Tier 2: Marine Sergeant (moderate tasks)
    tier2Model: 'openai/gpt-5.2-codex',
    tier2Fallbacks: ['anthropic/claude-sonnet-4-5', 'google/gemini-3-pro-preview'],
    tier2Thinking: {
      reasoningMode: 'high', // GPT-5.2 high mode
    },

    // Tier 3: Delta Force (critical tasks)
    tier3Model: 'anthropic/claude-opus-4-5',
    tier3Fallbacks: ['openai/gpt-5.2-codex', 'google/gemini-3-pro-preview'],
    tier3Thinking: {
      thinkingBudget: 32000, // Claude Opus extended thinking
    },

    maxParallel: 3,
    retryLimit: 2,
    canInvokeSupport: true,
  },
  validator: {
    model: 'anthropic/claude-haiku-4-5',
    fallbacks: ['openai/gpt-4o-mini', 'google/gemini-3-flash-preview'],
    strictMode: false,
    runTests: true,
    checkLinting: true,
  },
  patcher: {
    model: 'anthropic/claude-haiku-4-5',
    fallbacks: ['openai/gpt-4o-mini', 'google/gemini-3-flash-preview'],
    maxLines: 50,
  },
  support: {
    scout: {
      model: 'openrouter/z-ai/glm-4.7', // User's ZAI Max - fast reconnaissance
      fallbacks: ['anthropic/claude-haiku-4-5', 'google/gemini-3-flash-preview'],
      timeoutSeconds: 30,
    },
    intel: {
      model: 'google/gemini-3-pro-preview', // #1 Search Arena
      fallbacks: ['anthropic/claude-sonnet-4-5', 'openai/gpt-5.2-codex'],
      sources: ['docs', 'github', 'web'],
    },
    strategist: {
      model: 'openai/gpt-5.2-codex', // Strong reasoning
      fallbacks: ['anthropic/claude-opus-4-5', 'google/gemini-3-pro-preview'],
      invokeThreshold: 'complex',
    },
    uiOps: {
      model: 'google/gemini-3-pro-preview', // Changed from Flash - better for UI/UX
      fallbacks: ['anthropic/claude-sonnet-4-5', 'openai/gpt-5.2-codex'],
      styleSystem: 'tailwind',
    },
    scribe: {
      model: 'openrouter/z-ai/glm-4.7', // Changed - user's ZAI Max for docs
      fallbacks: ['anthropic/claude-haiku-4-5', 'google/gemini-3-flash-preview'],
      format: 'markdown',
    },
    // Note: SPECTRE (optics) removed - redundant with FACADE
    qa: {
      model: 'anthropic/claude-sonnet-4-5', // Keep - good at finding issues
      fallbacks: ['openai/gpt-5.2-codex', 'google/gemini-3-flash-preview'],
      frameworkDetect: true,
    },
  },
  mission: {
    autoCheckpoint: true,
    checkpointOn: 'objective_complete',
    stateDir: '.delta9',
    historyEnabled: true,
  },
  memory: {
    enabled: true,
    learnFromFailures: true,
    learnFromSuccesses: true,
    maxEntries: 1000,
  },
  budget: {
    enabled: true,
    defaultLimit: 10.0,
    warnAt: 0.7,
    pauseAt: 0.9,
    hardLimitAt: 1.0,
    trackByAgent: true,
  },
  notifications: {
    enabled: false,
    discordWebhook: null,
    slackWebhook: null,
    onEvents: ['mission_complete', 'validation_failed', 'budget_warning', 'needs_input'],
  },
  ui: {
    showProgress: true,
    showCost: true,
    verboseLogs: false,
  },
  seamless: {
    replaceBuild: true,
    replacePlan: true,
    keywordDetection: true,
    keywords: {
      councilXhigh: ['thorough', 'careful', 'critical', 'important'],
      councilNone: ['quick', 'just', 'simple', 'fast'],
      forcePlan: ['plan', 'design', 'architect', 'strategy'],
    },
  },
}
