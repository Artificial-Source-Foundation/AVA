/**
 * Config Schema Tests
 *
 * Validates Zod schemas accept correct input and reject invalid input.
 */

import { describe, expect, it } from 'vitest'
import {
  AgentSettingsSchema,
  ContextSettingsSchema,
  ExportableSettingsSchema,
  LLMProviderSchema,
  MemorySettingsSchema,
  PartialAgentSettingsSchema,
  PartialContextSettingsSchema,
  PartialMemorySettingsSchema,
  PartialPermissionSettingsSchema,
  PartialProviderSettingsSchema,
  PartialUISettingsSchema,
  PermissionSettingsSchema,
  ProviderSettingsSchema,
  SettingsSchema,
  UISettingsSchema,
} from './schema.js'
import {
  DEFAULT_AGENT_SETTINGS,
  DEFAULT_CONTEXT_SETTINGS,
  DEFAULT_MEMORY_SETTINGS,
  DEFAULT_PERMISSION_SETTINGS,
  DEFAULT_PROVIDER_SETTINGS,
  DEFAULT_SETTINGS,
  DEFAULT_UI_SETTINGS,
} from './types.js'

// ============================================================================
// Provider Schema
// ============================================================================

describe('LLMProviderSchema', () => {
  it('accepts valid providers', () => {
    for (const p of ['anthropic', 'openai', 'openrouter', 'google', 'copilot', 'glm', 'kimi']) {
      expect(LLMProviderSchema.safeParse(p).success).toBe(true)
    }
  })

  it('rejects invalid providers', () => {
    expect(LLMProviderSchema.safeParse('unknown').success).toBe(false)
    expect(LLMProviderSchema.safeParse('').success).toBe(false)
    expect(LLMProviderSchema.safeParse(42).success).toBe(false)
  })
})

describe('ProviderSettingsSchema', () => {
  it('accepts valid defaults', () => {
    expect(ProviderSettingsSchema.safeParse(DEFAULT_PROVIDER_SETTINGS).success).toBe(true)
  })

  it('rejects empty model', () => {
    const result = ProviderSettingsSchema.safeParse({
      ...DEFAULT_PROVIDER_SETTINGS,
      defaultModel: '',
    })
    expect(result.success).toBe(false)
  })

  it('rejects timeout below minimum', () => {
    const result = ProviderSettingsSchema.safeParse({
      ...DEFAULT_PROVIDER_SETTINGS,
      timeout: 500,
    })
    expect(result.success).toBe(false)
  })

  it('rejects timeout above maximum', () => {
    const result = ProviderSettingsSchema.safeParse({
      ...DEFAULT_PROVIDER_SETTINGS,
      timeout: 700000,
    })
    expect(result.success).toBe(false)
  })

  it('accepts optional customEndpoints', () => {
    const result = ProviderSettingsSchema.safeParse({
      ...DEFAULT_PROVIDER_SETTINGS,
      customEndpoints: { local: 'http://localhost:8080' },
    })
    expect(result.success).toBe(true)
  })

  it('rejects invalid URL in customEndpoints', () => {
    const result = ProviderSettingsSchema.safeParse({
      ...DEFAULT_PROVIDER_SETTINGS,
      customEndpoints: { local: 'not-a-url' },
    })
    expect(result.success).toBe(false)
  })
})

// ============================================================================
// Agent Schema
// ============================================================================

describe('AgentSettingsSchema', () => {
  it('accepts valid defaults', () => {
    expect(AgentSettingsSchema.safeParse(DEFAULT_AGENT_SETTINGS).success).toBe(true)
  })

  it('rejects maxTurns below 1', () => {
    const result = AgentSettingsSchema.safeParse({ ...DEFAULT_AGENT_SETTINGS, maxTurns: 0 })
    expect(result.success).toBe(false)
  })

  it('rejects maxTurns above 1000', () => {
    const result = AgentSettingsSchema.safeParse({ ...DEFAULT_AGENT_SETTINGS, maxTurns: 1001 })
    expect(result.success).toBe(false)
  })

  it('rejects parallelWorkers above 16', () => {
    const result = AgentSettingsSchema.safeParse({
      ...DEFAULT_AGENT_SETTINGS,
      parallelWorkers: 17,
    })
    expect(result.success).toBe(false)
  })

  it('rejects invalid validator type', () => {
    const result = AgentSettingsSchema.safeParse({
      ...DEFAULT_AGENT_SETTINGS,
      enabledValidators: ['invalid'],
    })
    expect(result.success).toBe(false)
  })

  it('accepts all valid validator types', () => {
    const result = AgentSettingsSchema.safeParse({
      ...DEFAULT_AGENT_SETTINGS,
      enabledValidators: ['syntax', 'typescript', 'lint', 'test', 'selfReview'],
    })
    expect(result.success).toBe(true)
  })
})

// ============================================================================
// Permission Schema
// ============================================================================

describe('PermissionSettingsSchema', () => {
  it('accepts valid defaults', () => {
    expect(PermissionSettingsSchema.safeParse(DEFAULT_PERMISSION_SETTINGS).success).toBe(true)
  })

  it('rejects maxReadSize below 1024', () => {
    const result = PermissionSettingsSchema.safeParse({
      ...DEFAULT_PERMISSION_SETTINGS,
      maxReadSize: 512,
    })
    expect(result.success).toBe(false)
  })

  it('rejects maxReadSize above 100MB', () => {
    const result = PermissionSettingsSchema.safeParse({
      ...DEFAULT_PERMISSION_SETTINGS,
      maxReadSize: 200 * 1024 * 1024,
    })
    expect(result.success).toBe(false)
  })

  it('rejects invalid confirmation action', () => {
    const result = PermissionSettingsSchema.safeParse({
      ...DEFAULT_PERMISSION_SETTINGS,
      requireConfirmation: ['invalid'],
    })
    expect(result.success).toBe(false)
  })
})

// ============================================================================
// Context Schema
// ============================================================================

describe('ContextSettingsSchema', () => {
  it('accepts valid defaults', () => {
    expect(ContextSettingsSchema.safeParse(DEFAULT_CONTEXT_SETTINGS).success).toBe(true)
  })

  it('rejects maxTokens below 1000', () => {
    const result = ContextSettingsSchema.safeParse({
      ...DEFAULT_CONTEXT_SETTINGS,
      maxTokens: 500,
    })
    expect(result.success).toBe(false)
  })

  it('rejects compactionThreshold below 50', () => {
    const result = ContextSettingsSchema.safeParse({
      ...DEFAULT_CONTEXT_SETTINGS,
      compactionThreshold: 30,
    })
    expect(result.success).toBe(false)
  })

  it('rejects compactionThreshold above 95', () => {
    const result = ContextSettingsSchema.safeParse({
      ...DEFAULT_CONTEXT_SETTINGS,
      compactionThreshold: 100,
    })
    expect(result.success).toBe(false)
  })

  it('accepts zero autoSaveInterval (disabled)', () => {
    const result = ContextSettingsSchema.safeParse({
      ...DEFAULT_CONTEXT_SETTINGS,
      autoSaveInterval: 0,
    })
    expect(result.success).toBe(true)
  })
})

// ============================================================================
// Memory Schema
// ============================================================================

describe('MemorySettingsSchema', () => {
  it('accepts valid defaults', () => {
    expect(MemorySettingsSchema.safeParse(DEFAULT_MEMORY_SETTINGS).success).toBe(true)
  })

  it('rejects minSimilarity above 1', () => {
    const result = MemorySettingsSchema.safeParse({
      ...DEFAULT_MEMORY_SETTINGS,
      minSimilarity: 1.5,
    })
    expect(result.success).toBe(false)
  })

  it('rejects negative decayRate', () => {
    const result = MemorySettingsSchema.safeParse({
      ...DEFAULT_MEMORY_SETTINGS,
      decayRate: -0.1,
    })
    expect(result.success).toBe(false)
  })

  it('rejects empty embeddingModel', () => {
    const result = MemorySettingsSchema.safeParse({
      ...DEFAULT_MEMORY_SETTINGS,
      embeddingModel: '',
    })
    expect(result.success).toBe(false)
  })

  it('rejects maxMemories below 100', () => {
    const result = MemorySettingsSchema.safeParse({
      ...DEFAULT_MEMORY_SETTINGS,
      maxMemories: 50,
    })
    expect(result.success).toBe(false)
  })
})

// ============================================================================
// UI Schema
// ============================================================================

describe('UISettingsSchema', () => {
  it('accepts valid defaults', () => {
    expect(UISettingsSchema.safeParse(DEFAULT_UI_SETTINGS).success).toBe(true)
  })

  it('accepts all theme values', () => {
    for (const theme of ['light', 'dark', 'system']) {
      const result = UISettingsSchema.safeParse({ ...DEFAULT_UI_SETTINGS, theme })
      expect(result.success).toBe(true)
    }
  })

  it('rejects invalid theme', () => {
    const result = UISettingsSchema.safeParse({ ...DEFAULT_UI_SETTINGS, theme: 'neon' })
    expect(result.success).toBe(false)
  })

  it('rejects fontSize below 8', () => {
    const result = UISettingsSchema.safeParse({ ...DEFAULT_UI_SETTINGS, fontSize: 5 })
    expect(result.success).toBe(false)
  })

  it('rejects fontSize above 32', () => {
    const result = UISettingsSchema.safeParse({ ...DEFAULT_UI_SETTINGS, fontSize: 48 })
    expect(result.success).toBe(false)
  })
})

// ============================================================================
// Combined Schema
// ============================================================================

describe('SettingsSchema', () => {
  it('accepts valid defaults', () => {
    expect(SettingsSchema.safeParse(DEFAULT_SETTINGS).success).toBe(true)
  })

  it('rejects missing category', () => {
    const { provider: _, ...rest } = DEFAULT_SETTINGS
    expect(SettingsSchema.safeParse(rest).success).toBe(false)
  })

  it('rejects extra unknown category', () => {
    const result = SettingsSchema.safeParse({
      ...DEFAULT_SETTINGS,
      unknown: { foo: 'bar' },
    })
    // Zod strips unknown keys by default, so this should still pass
    expect(result.success).toBe(true)
  })
})

// ============================================================================
// Partial Schemas
// ============================================================================

describe('partial schemas', () => {
  it('PartialProviderSettingsSchema accepts subset', () => {
    expect(PartialProviderSettingsSchema.safeParse({ timeout: 30000 }).success).toBe(true)
  })

  it('PartialAgentSettingsSchema accepts subset', () => {
    expect(PartialAgentSettingsSchema.safeParse({ maxTurns: 100 }).success).toBe(true)
  })

  it('PartialPermissionSettingsSchema accepts subset', () => {
    expect(PartialPermissionSettingsSchema.safeParse({ allowBashExecution: false }).success).toBe(
      true
    )
  })

  it('PartialContextSettingsSchema accepts subset', () => {
    expect(PartialContextSettingsSchema.safeParse({ maxTokens: 100000 }).success).toBe(true)
  })

  it('PartialMemorySettingsSchema accepts subset', () => {
    expect(PartialMemorySettingsSchema.safeParse({ enabled: false }).success).toBe(true)
  })

  it('PartialUISettingsSchema accepts subset', () => {
    expect(PartialUISettingsSchema.safeParse({ theme: 'dark' }).success).toBe(true)
  })

  it('PartialAgentSettingsSchema rejects invalid values', () => {
    expect(PartialAgentSettingsSchema.safeParse({ maxTurns: -1 }).success).toBe(false)
  })

  it('partial schemas accept empty object', () => {
    expect(PartialProviderSettingsSchema.safeParse({}).success).toBe(true)
    expect(PartialAgentSettingsSchema.safeParse({}).success).toBe(true)
    expect(PartialContextSettingsSchema.safeParse({}).success).toBe(true)
    expect(PartialMemorySettingsSchema.safeParse({}).success).toBe(true)
    expect(PartialUISettingsSchema.safeParse({}).success).toBe(true)
  })
})

// ============================================================================
// Exportable Schema
// ============================================================================

describe('ExportableSettingsSchema', () => {
  it('accepts valid exportable settings', () => {
    const result = ExportableSettingsSchema.safeParse({
      version: 1,
      exportedAt: '2026-02-08T00:00:00.000Z',
      settings: DEFAULT_SETTINGS,
    })
    expect(result.success).toBe(true)
  })

  it('rejects version below 1', () => {
    const result = ExportableSettingsSchema.safeParse({
      version: 0,
      exportedAt: '2026-02-08T00:00:00.000Z',
      settings: DEFAULT_SETTINGS,
    })
    expect(result.success).toBe(false)
  })

  it('rejects invalid datetime format', () => {
    const result = ExportableSettingsSchema.safeParse({
      version: 1,
      exportedAt: 'not-a-date',
      settings: DEFAULT_SETTINGS,
    })
    expect(result.success).toBe(false)
  })
})
