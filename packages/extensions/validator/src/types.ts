/**
 * Validator types — QA pipeline contracts.
 */

import type { LLMProvider } from '@ava/core-v2/llm'

export type ValidatorName = 'syntax' | 'typescript' | 'lint' | 'test' | 'build' | 'self-review'

export interface ValidationResult {
  validator: string
  passed: boolean
  errors: string[]
  warnings: string[]
  durationMs: number
  metadata?: Record<string, unknown>
}

export interface ValidationContext {
  files: string[]
  cwd: string
  signal: AbortSignal
  config: ValidatorConfig
  previousResults?: ValidationResult[]
}

export interface Validator {
  name: ValidatorName
  description: string
  critical: boolean
  run(ctx: ValidationContext): Promise<ValidationResult>
  canRun?(ctx: ValidationContext): Promise<boolean>
}

export interface ValidatorConfig {
  enabledValidators: ValidatorName[]
  timeout: number
  failFast: boolean
  reviewEnabled?: boolean
  reviewProvider?: LLMProvider
  reviewModel?: string
  reviewMaxRetries?: number
  testCommand?: string
  buildCommand?: string
  lintCommand?: string
  include?: string[]
  exclude?: string[]
}

export const DEFAULT_VALIDATOR_CONFIG: ValidatorConfig = {
  enabledValidators: ['syntax', 'typescript', 'lint'],
  timeout: 30_000,
  failFast: true,
  reviewEnabled: false,
  reviewMaxRetries: 1,
}

export interface ValidationPipelineResult {
  passed: boolean
  results: ValidationResult[]
  totalDurationMs: number
  blockedBy?: string
  aborted?: boolean
  summary: {
    total: number
    passed: number
    failed: number
    totalErrors: number
    totalWarnings: number
  }
}
