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
  /** Temperature for planning (0-1) */
  temperature: number
  /** Model for dispatch/execution coordination */
  dispatchModel: string
}

// =============================================================================
// Council Configuration
// =============================================================================

export type CouncilMode = 'none' | 'quick' | 'standard' | 'xhigh'

export interface OracleConfig {
  /** Oracle display name (codename for Delta Team: Cipher, Vector, Prism, Apex) */
  name: string
  /** Model to use (user configurable - any model provider) */
  model: string
  /** Whether this oracle is enabled */
  enabled: boolean
  /** Specialty area */
  specialty: 'architecture' | 'logic' | 'ui' | 'performance' | 'general'
  /** Temperature setting (0-1, defines personality decisiveness) */
  temperature?: number
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
// Operator Configuration
// =============================================================================

export interface OperatorConfig {
  /** Default model for operators */
  defaultModel: string
  /** Model for complex tasks */
  complexModel: string
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
  /** Maximum lines patcher can change */
  maxLines: number
}

// =============================================================================
// Support Agent Configuration
// =============================================================================

export interface ScoutConfig {
  model: string
  timeoutSeconds: number
}

export interface IntelConfig {
  model: string
  sources: ('docs' | 'github' | 'web')[]
}

export interface StrategistConfig {
  model: string
  invokeThreshold: 'simple' | 'moderate' | 'complex'
}

export interface UiOpsConfig {
  model: string
  styleSystem: 'tailwind' | 'css' | 'scss' | 'styled-components'
}

export interface ScribeConfig {
  model: string
  format: 'markdown' | 'jsdoc' | 'tsdoc'
}

export interface OpticsConfig {
  model: string
}

export interface QaConfig {
  model: string
  frameworkDetect: boolean
}

export interface SupportConfig {
  scout: ScoutConfig
  intel: IntelConfig
  strategist: StrategistConfig
  uiOps: UiOpsConfig
  scribe: ScribeConfig
  optics: OpticsConfig
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
    model: 'anthropic/claude-sonnet-4',
    temperature: 0.7,
    dispatchModel: 'anthropic/claude-sonnet-4',
  },
  council: {
    enabled: true,
    defaultMode: 'standard',
    autoDetectComplexity: true,
    // The Delta Team - personality-based Oracles with configurable models
    members: [
      {
        name: 'Cipher',
        model: 'anthropic/claude-opus-4-5',
        enabled: true,
        specialty: 'architecture',
        temperature: 0.2, // Decisive, low variance
      },
      {
        name: 'Vector',
        model: 'openai/gpt-4o',
        enabled: true,
        specialty: 'logic',
        temperature: 0.4, // Methodical, balanced
      },
      {
        name: 'Prism',
        model: 'google/gemini-2.0-flash',
        enabled: true,
        specialty: 'ui',
        temperature: 0.6, // Creative, higher variance
      },
      {
        name: 'Apex',
        model: 'deepseek/deepseek-chat',
        enabled: true,
        specialty: 'performance',
        temperature: 0.3, // Precise, analytical
      },
    ],
    parallel: true,
    requireConsensus: false,
    minResponses: 2,
    timeoutSeconds: 120,
  },
  operators: {
    defaultModel: 'anthropic/claude-sonnet-4',
    complexModel: 'anthropic/claude-opus-4-5',
    maxParallel: 3,
    retryLimit: 2,
    canInvokeSupport: true,
  },
  validator: {
    model: 'anthropic/claude-haiku-4',
    strictMode: false,
    runTests: true,
    checkLinting: true,
  },
  patcher: {
    model: 'anthropic/claude-haiku-4',
    maxLines: 50,
  },
  support: {
    scout: {
      model: 'anthropic/claude-haiku-4',
      timeoutSeconds: 30,
    },
    intel: {
      model: 'anthropic/claude-sonnet-4',
      sources: ['docs', 'github', 'web'],
    },
    strategist: {
      model: 'openai/gpt-4o',
      invokeThreshold: 'complex',
    },
    uiOps: {
      model: 'google/gemini-2.0-flash',
      styleSystem: 'tailwind',
    },
    scribe: {
      model: 'google/gemini-2.0-flash',
      format: 'markdown',
    },
    optics: {
      model: 'google/gemini-2.0-flash',
    },
    qa: {
      model: 'anthropic/claude-sonnet-4',
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
