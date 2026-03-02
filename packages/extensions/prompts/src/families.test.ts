import { describe, expect, it } from 'vitest'
import {
  detectModelFamily,
  FAMILY_PROMPT_SECTIONS,
  getModelFamilyPromptSection,
} from './families.js'

describe('detectModelFamily', () => {
  it('detects claude models', () => {
    expect(detectModelFamily('claude-3-opus')).toBe('claude')
    expect(detectModelFamily('claude-3-sonnet-20240229')).toBe('claude')
    expect(detectModelFamily('claude-3-haiku')).toBe('claude')
    expect(detectModelFamily('anthropic/claude-sonnet-4')).toBe('claude')
  })

  it('detects sonnet/haiku/opus without claude prefix', () => {
    expect(detectModelFamily('sonnet-4')).toBe('claude')
    expect(detectModelFamily('haiku-3.5')).toBe('claude')
    expect(detectModelFamily('opus-next')).toBe('claude')
  })

  it('detects gpt models', () => {
    expect(detectModelFamily('gpt-4o')).toBe('gpt')
    expect(detectModelFamily('gpt-4o-mini')).toBe('gpt')
    expect(detectModelFamily('gpt-3.5-turbo')).toBe('gpt')
    expect(detectModelFamily('openai/gpt-5.3-codex')).toBe('gpt')
  })

  it('detects o-series models as gpt', () => {
    expect(detectModelFamily('o1-preview')).toBe('gpt')
    expect(detectModelFamily('o3-mini')).toBe('gpt')
    expect(detectModelFamily('o4-mini')).toBe('gpt')
  })

  it('detects chatgpt as gpt', () => {
    expect(detectModelFamily('chatgpt-4o-latest')).toBe('gpt')
  })

  it('detects gemini models', () => {
    expect(detectModelFamily('gemini-1.5-pro')).toBe('gemini')
    expect(detectModelFamily('gemini-2.0-flash')).toBe('gemini')
    expect(detectModelFamily('google/gemma-7b')).toBe('gemini')
  })

  it('detects llama models', () => {
    expect(detectModelFamily('meta-llama/llama-3.1-70b')).toBe('llama')
    expect(detectModelFamily('codellama-34b')).toBe('llama')
    expect(detectModelFamily('llama-3.2-90b-vision')).toBe('llama')
  })

  it('detects deepseek models', () => {
    expect(detectModelFamily('deepseek-coder-v2')).toBe('deepseek')
    expect(detectModelFamily('deepseek-chat')).toBe('deepseek')
    expect(detectModelFamily('deepseek/deepseek-r1')).toBe('deepseek')
  })

  it('detects mistral models', () => {
    expect(detectModelFamily('mistral-large-latest')).toBe('mistral')
    expect(detectModelFamily('mixtral-8x7b')).toBe('mistral')
    expect(detectModelFamily('codestral-latest')).toBe('mistral')
    expect(detectModelFamily('mistralai/mistral-nemo')).toBe('mistral')
  })

  it('returns unknown for unrecognized models', () => {
    expect(detectModelFamily('some-custom-model')).toBe('unknown')
    expect(detectModelFamily('local-finetune-v1')).toBe('unknown')
    expect(detectModelFamily('qwen-72b')).toBe('unknown')
  })

  it('is case-insensitive', () => {
    expect(detectModelFamily('Claude-3-Opus')).toBe('claude')
    expect(detectModelFamily('GPT-4O')).toBe('gpt')
    expect(detectModelFamily('GEMINI-PRO')).toBe('gemini')
    expect(detectModelFamily('DeepSeek-V2')).toBe('deepseek')
  })
})

describe('FAMILY_PROMPT_SECTIONS', () => {
  it('has entries for all families', () => {
    expect(FAMILY_PROMPT_SECTIONS.claude).toContain('XML tags')
    expect(FAMILY_PROMPT_SECTIONS.gpt).toContain('markdown')
    expect(FAMILY_PROMPT_SECTIONS.gemini).toContain('context windows')
    expect(FAMILY_PROMPT_SECTIONS.llama).toContain('concise')
    expect(FAMILY_PROMPT_SECTIONS.deepseek).toContain('fill-in-the-middle')
    expect(FAMILY_PROMPT_SECTIONS.mistral).toContain('function calling')
    expect(FAMILY_PROMPT_SECTIONS.unknown).toBe('')
  })
})

describe('getModelFamilyPromptSection', () => {
  it('returns claude section for claude models', () => {
    const section = getModelFamilyPromptSection('claude-3-opus')
    expect(section).toContain('XML tags')
    expect(section).toContain('thinking blocks')
  })

  it('returns gpt section for gpt models', () => {
    const section = getModelFamilyPromptSection('gpt-4o')
    expect(section).toContain('markdown')
    expect(section).toContain('function calling')
  })

  it('returns gemini section for gemini models', () => {
    const section = getModelFamilyPromptSection('gemini-1.5-pro')
    expect(section).toContain('context windows')
  })

  it('returns llama section for llama models', () => {
    const section = getModelFamilyPromptSection('llama-3.1-70b')
    expect(section).toContain('concise')
  })

  it('returns deepseek section for deepseek models', () => {
    const section = getModelFamilyPromptSection('deepseek-coder')
    expect(section).toContain('fill-in-the-middle')
  })

  it('returns mistral section for mistral models', () => {
    const section = getModelFamilyPromptSection('mistral-large')
    expect(section).toContain('function calling')
  })

  it('returns empty string for unknown models', () => {
    expect(getModelFamilyPromptSection('some-random-model')).toBe('')
    expect(getModelFamilyPromptSection('qwen-72b')).toBe('')
  })
})
