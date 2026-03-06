import { describe, expect, it } from 'vitest'
import { getTierPrompt } from './tier-prompts.js'

describe('tier prompts', () => {
  it('director prompt forbids writing code', () => {
    expect(getTierPrompt('director')).toContain('NEVER write code')
  })

  it('engineer prompt requires isolated worktree', () => {
    expect(getTierPrompt('engineer')).toContain('isolated worktree')
  })

  it('reviewer prompt contains lint/typecheck/test commands', () => {
    const prompt = getTierPrompt('reviewer')
    expect(prompt).toContain('npx biome check')
    expect(prompt).toContain('npx tsc --noEmit')
    expect(prompt).toContain('npx vitest')
  })
})
