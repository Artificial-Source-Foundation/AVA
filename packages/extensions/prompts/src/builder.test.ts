import { afterEach, describe, expect, it } from 'vitest'
import {
  addPromptSection,
  buildSystemPrompt,
  getPromptSections,
  resetPromptSections,
} from './builder.js'

describe('Prompt Builder', () => {
  afterEach(() => {
    resetPromptSections()
  })

  it('builds prompt from default sections', () => {
    // Default sections are added on module load
    const prompt = buildSystemPrompt()
    expect(prompt).toContain('AVA')
    expect(prompt).toContain('coding')
  })

  it('adds custom sections', () => {
    resetPromptSections()
    addPromptSection({ name: 'custom', priority: 0, content: 'Custom section content' })
    const prompt = buildSystemPrompt()
    expect(prompt).toContain('Custom section content')
  })

  it('sorts sections by priority', () => {
    resetPromptSections()
    addPromptSection({ name: 'second', priority: 10, content: 'SECOND' })
    addPromptSection({ name: 'first', priority: 0, content: 'FIRST' })
    const prompt = buildSystemPrompt()
    expect(prompt.indexOf('FIRST')).toBeLessThan(prompt.indexOf('SECOND'))
  })

  it('returns disposable to remove section', () => {
    resetPromptSections()
    const remove = addPromptSection({ name: 'temp', priority: 0, content: 'temporary' })
    expect(buildSystemPrompt()).toContain('temporary')
    remove()
    expect(buildSystemPrompt()).not.toContain('temporary')
  })

  it('appends model-family section for claude', () => {
    resetPromptSections()
    addPromptSection({ name: 'base', priority: 0, content: 'Base prompt' })
    const prompt = buildSystemPrompt('claude-3-opus')
    expect(prompt).toContain('XML tags')
    expect(prompt).toContain('thinking blocks')
  })

  it('appends model-family section for gpt', () => {
    resetPromptSections()
    addPromptSection({ name: 'base', priority: 0, content: 'Base prompt' })
    const prompt = buildSystemPrompt('gpt-4o')
    expect(prompt).toContain('markdown')
    expect(prompt).toContain('function calling')
  })

  it('does not append family section for unknown models', () => {
    resetPromptSections()
    addPromptSection({ name: 'base', priority: 0, content: 'Base prompt' })
    const prompt = buildSystemPrompt('qwen-72b')
    expect(prompt).toBe('Base prompt')
  })

  it('does not append family section when no model is provided', () => {
    resetPromptSections()
    addPromptSection({ name: 'base', priority: 0, content: 'Base prompt' })
    const prompt = buildSystemPrompt()
    expect(prompt).toBe('Base prompt')
  })

  it('lists sections', () => {
    resetPromptSections()
    addPromptSection({ name: 'a', priority: 5, content: 'A' })
    addPromptSection({ name: 'b', priority: 1, content: 'B' })
    const sections = getPromptSections()
    expect(sections).toHaveLength(2)
    expect(sections[0]!.name).toBe('b') // Lower priority first
  })
})
