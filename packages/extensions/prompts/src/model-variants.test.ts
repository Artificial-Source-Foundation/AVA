import { afterEach, describe, expect, it } from 'vitest'
import { addPromptSection, buildSystemPrompt, resetPromptSections } from './builder.js'
import {
  detectModelFamily,
  getModelVariantPrompt,
  getModelVariantPromptSection,
} from './model-variants.js'

describe('detectModelFamily', () => {
  it('detects supported model families', () => {
    expect(detectModelFamily('claude-3-opus')).toBe('claude')
    expect(detectModelFamily('gpt-4o')).toBe('gpt')
    expect(detectModelFamily('gemini-2.0-flash')).toBe('gemini')
    expect(detectModelFamily('meta-llama/llama-3.1')).toBe('llama')
    expect(detectModelFamily('mistral-large-latest')).toBe('mistral')
  })

  it('returns other for unknown models', () => {
    expect(detectModelFamily('qwen-72b')).toBe('other')
  })
})

describe('getModelVariantPrompt', () => {
  it('returns family-specific guidance', () => {
    const claude = getModelVariantPrompt('claude-sonnet-4')
    expect(claude.family).toBe('claude')
    expect(claude.thinkingMode).toContain('extended thinking')

    const gpt = getModelVariantPrompt('openai/gpt-5.3-codex')
    expect(gpt.family).toBe('gpt')
    expect(gpt.structuredOutput).toContain('valid JSON')
  })

  it('creates a prompt section string', () => {
    const section = getModelVariantPromptSection('gemini-1.5-pro')
    expect(section).toContain('Model Variant Guidance')
    expect(section).toContain('Family: gemini')
  })
})

describe('prompt builder integration', () => {
  afterEach(() => {
    resetPromptSections()
  })

  it('appends model-variant section when model is provided', () => {
    resetPromptSections()
    addPromptSection({ name: 'base', priority: 0, content: 'Base prompt' })

    const prompt = buildSystemPrompt('gpt-4o')
    expect(prompt).toContain('Base prompt')
    expect(prompt).toContain('Model Variant Guidance')
    expect(prompt).toContain('Family: gpt')
  })
})
