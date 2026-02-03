/**
 * Validator Types
 * Core types for the validation pipeline
 *
 * Provides QA verification gate to ensure agent outputs meet quality standards
 */

// ============================================================================
// Validation Result
// ============================================================================

/**
 * Result from running a single validator
 */
export interface ValidationResult {
  /** Validator name that produced this result */
  validator: string
  /** Whether validation passed */
  passed: boolean
  /** Critical errors that should block (syntax errors, type errors, etc.) */
  errors: string[]
  /** Non-critical warnings (style issues, suggestions, etc.) */
  warnings: string[]
  /** Time taken to run this validator in milliseconds */
  durationMs: number
  /** Additional metadata from the validator */
  metadata?: Record<string, unknown>
}

// ============================================================================
// Validator Interface
// ============================================================================

/**
 * Context passed to validators during execution
 */
export interface ValidationContext {
  /** List of files to validate (absolute paths) */
  files: string[]
  /** Working directory */
  cwd: string
  /** AbortSignal for cancellation */
  signal: AbortSignal
  /** Validator configuration */
  config: ValidatorConfig
  /** Optional: previous results from earlier validators */
  previousResults?: ValidationResult[]
}

/**
 * Interface for a validator implementation
 */
export interface Validator {
  /** Unique validator name (e.g., 'syntax', 'typescript', 'lint') */
  name: string
  /** Human-readable description */
  description: string
  /** Whether this validator is critical (failure blocks pipeline) */
  critical: boolean
  /** Run the validator */
  run(ctx: ValidationContext): Promise<ValidationResult>
  /** Optional: Check if validator can run in current environment */
  canRun?(ctx: ValidationContext): Promise<boolean>
}

// ============================================================================
// Validator Configuration
// ============================================================================

/**
 * Names of built-in validators
 */
export type ValidatorName = 'syntax' | 'typescript' | 'lint' | 'test' | 'build' | 'self-review'

/**
 * Configuration for the validation pipeline
 */
export interface ValidatorConfig {
  /** List of validators to run (in order) */
  enabledValidators: ValidatorName[]
  /** Per-validator timeout in milliseconds (default: 30000) */
  timeout: number
  /** Stop on first critical failure (default: true) */
  failFast: boolean
  /** Custom test command (overrides auto-detection) */
  testCommand?: string
  /** Custom build command (overrides auto-detection) */
  buildCommand?: string
  /** Custom lint command (overrides auto-detection) */
  lintCommand?: string
  /** File patterns to include (glob patterns) */
  include?: string[]
  /** File patterns to exclude (glob patterns) */
  exclude?: string[]
}

/**
 * Default validator configuration
 */
export const DEFAULT_VALIDATOR_CONFIG: ValidatorConfig = {
  enabledValidators: ['syntax', 'typescript', 'lint'],
  timeout: 30000,
  failFast: true,
}

// ============================================================================
// Pipeline Result
// ============================================================================

/**
 * Result from running the complete validation pipeline
 */
export interface ValidationPipelineResult {
  /** Overall pass/fail status */
  passed: boolean
  /** Results from all validators that ran */
  results: ValidationResult[]
  /** Total time for all validators in milliseconds */
  totalDurationMs: number
  /** Which validator blocked execution (if any) */
  blockedBy?: string
  /** Whether pipeline was aborted early */
  aborted?: boolean
  /** Summary counts */
  summary: {
    /** Total validators run */
    total: number
    /** Validators that passed */
    passed: number
    /** Validators that failed */
    failed: number
    /** Total errors across all validators */
    totalErrors: number
    /** Total warnings across all validators */
    totalWarnings: number
  }
}

// ============================================================================
// Self-Review Types
// ============================================================================

/**
 * Issue found by self-review validator
 */
export interface SelfReviewIssue {
  /** Severity: critical blocks, minor warns */
  severity: 'critical' | 'minor'
  /** Issue category */
  category: 'bug' | 'security' | 'edge-case' | 'performance' | 'style'
  /** Issue description */
  description: string
  /** File path (if applicable) */
  file?: string
  /** Line number (if applicable) */
  line?: number
  /** Suggested fix (if any) */
  suggestion?: string
}

/**
 * Configuration for self-review validator
 */
export interface SelfReviewConfig {
  /** Model to use for review */
  model?: string
  /** Provider to use */
  provider?: 'anthropic' | 'openai' | 'openrouter'
  /** Maximum tokens for review */
  maxTokens?: number
  /** What to focus on */
  focus?: Array<'bugs' | 'security' | 'edge-cases' | 'performance'>
}

// ============================================================================
// Validator Registry
// ============================================================================

/**
 * Registry of available validators
 */
export interface ValidatorRegistry {
  /** Get a validator by name */
  get(name: ValidatorName): Validator | undefined
  /** Register a custom validator */
  register(validator: Validator): void
  /** Get all registered validators */
  getAll(): Validator[]
  /** Check if a validator exists */
  has(name: ValidatorName): boolean
}
