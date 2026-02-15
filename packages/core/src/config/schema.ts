/**
 * Config Schemas
 *
 * Zod validation schemas for settings validation.
 * Ensures runtime type safety and provides helpful error messages.
 */

import { z } from 'zod'

// ============================================================================
// Provider Schema
// ============================================================================

export const LLMProviderSchema = z.enum([
  'anthropic',
  'openai',
  'openrouter',
  'google',
  'copilot',
  'glm',
  'kimi',
])

export const ProviderSettingsSchema = z.object({
  defaultProvider: LLMProviderSchema,
  defaultModel: z.string().min(1),
  weakModel: z.string().min(1).optional(),
  weakModelProvider: LLMProviderSchema.optional(),
  editorModel: z.string().min(1).optional(),
  editorModelProvider: LLMProviderSchema.optional(),
  timeout: z.number().int().min(1000).max(600000),
  openRouterFallback: z.boolean(),
  customEndpoints: z.record(z.string(), z.string().url()).optional(),
})

// ============================================================================
// Agent Schema
// ============================================================================

export const ValidatorTypeSchema = z.enum(['syntax', 'typescript', 'lint', 'test', 'selfReview'])

export const AgentSettingsSchema = z.object({
  maxTurns: z.number().int().min(1).max(1000),
  maxTimeMinutes: z.number().int().min(1).max(480),
  maxRetries: z.number().int().min(0).max(10),
  validatorsEnabled: z.boolean(),
  enabledValidators: z.array(ValidatorTypeSchema),
  parallelWorkers: z.number().int().min(1).max(16),
  gracePeriodMs: z.number().int().min(0).max(300000),
})

// ============================================================================
// Permission Schema
// ============================================================================

export const ConfirmationActionSchema = z.enum(['delete', 'execute', 'write', 'network'])

export const PermissionSettingsSchema = z.object({
  autoApprovePatterns: z.array(z.string()),
  requireConfirmation: z.array(ConfirmationActionSchema),
  deniedPaths: z.array(z.string()),
  maxReadSize: z
    .number()
    .int()
    .min(1024)
    .max(100 * 1024 * 1024),
  allowBashExecution: z.boolean(),
  allowNetworkRequests: z.boolean(),
})

// ============================================================================
// Context Schema
// ============================================================================

export const ContextSettingsSchema = z.object({
  maxTokens: z.number().int().min(1000).max(2000000),
  compactionThreshold: z.number().int().min(50).max(95),
  autoSave: z.boolean(),
  autoSaveInterval: z.number().int().min(0).max(3600000),
  checkpointInterval: z.number().int().min(0).max(100),
  maxSessions: z.number().int().min(1).max(100),
})

// ============================================================================
// UI Schema
// ============================================================================

export const ThemeSchema = z.enum(['light', 'dark', 'system'])

export const UISettingsSchema = z.object({
  theme: ThemeSchema,
  fontSize: z.number().int().min(8).max(32),
  showTokenCounts: z.boolean(),
  streamingSpeed: z.number().int().min(1).max(1000),
  syntaxHighlighting: z.boolean(),
  lineNumbers: z.boolean(),
  compactMode: z.boolean(),
})

// ============================================================================
// Git Schema
// ============================================================================

export const GitConfigSchema = z.object({
  enabled: z.boolean(),
  autoCommit: z.boolean(),
  branchPrefix: z.string().min(1),
  messagePrefix: z.string(),
})

// ============================================================================
// Sandbox Schema
// ============================================================================

export const SandboxSettingsSchema = z.object({
  mode: z.enum(['none', 'docker']).default('none'),
  image: z.string().min(1).default('node:20-slim'),
  timeoutSeconds: z.number().int().min(10).max(600).default(120),
  networkAccess: z.boolean().default(false),
  memoryLimit: z.string().min(1).default('512m'),
  cpuLimit: z.string().min(1).default('1'),
})

// ============================================================================
// Combined Schema
// ============================================================================

export const SettingsSchema = z.object({
  provider: ProviderSettingsSchema,
  agent: AgentSettingsSchema,
  permissions: PermissionSettingsSchema,
  context: ContextSettingsSchema,
  ui: UISettingsSchema,
  git: GitConfigSchema,
  sandbox: SandboxSettingsSchema,
})

// ============================================================================
// Partial Schemas (for updates)
// ============================================================================

export const PartialProviderSettingsSchema = ProviderSettingsSchema.partial()
export const PartialAgentSettingsSchema = AgentSettingsSchema.partial()
export const PartialPermissionSettingsSchema = PermissionSettingsSchema.partial()
export const PartialContextSettingsSchema = ContextSettingsSchema.partial()
export const PartialUISettingsSchema = UISettingsSchema.partial()
export const PartialGitConfigSchema = GitConfigSchema.partial()
export const PartialSandboxSettingsSchema = SandboxSettingsSchema.partial()

// ============================================================================
// Export Schema
// ============================================================================

export const ExportableSettingsSchema = z.object({
  version: z.number().int().min(1),
  exportedAt: z.string().datetime(),
  settings: SettingsSchema,
})

// ============================================================================
// Type Inference
// ============================================================================

export type ValidatedSettings = z.infer<typeof SettingsSchema>
export type ValidatedProviderSettings = z.infer<typeof ProviderSettingsSchema>
export type ValidatedAgentSettings = z.infer<typeof AgentSettingsSchema>
export type ValidatedPermissionSettings = z.infer<typeof PermissionSettingsSchema>
export type ValidatedContextSettings = z.infer<typeof ContextSettingsSchema>
export type ValidatedUISettings = z.infer<typeof UISettingsSchema>
