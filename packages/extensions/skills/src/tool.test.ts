/**
 * Tests for the load_skill tool.
 */

import { describe, expect, it } from 'vitest'
import { createLoadSkillTool } from './tool.js'
import type { Skill } from './types.js'

function makeSkill(overrides: Partial<Skill> = {}): Skill {
  return {
    name: 'react-patterns',
    description: 'React best practices',
    globs: ['**/*.tsx'],
    content: 'Use functional components with hooks.',
    source: '.ava/skills/react/SKILL.md',
    ...overrides,
  }
}

describe('load_skill tool', () => {
  it('has the correct definition', () => {
    const tool = createLoadSkillTool([])
    expect(tool.definition.name).toBe('load_skill')
    expect(tool.definition.description).toContain('Load a skill')
  })

  it('loads a manual skill by exact name', async () => {
    const skill = makeSkill({
      name: 'react-patterns',
      activation: 'manual',
      content: 'Use hooks and functional components.',
    })
    const tool = createLoadSkillTool([skill])

    const result = await tool.execute({ name: 'react-patterns' })
    expect(result.success).toBe(true)
    expect(result.output).toBe('Use hooks and functional components.')
  })

  it('loads an agent skill by name', async () => {
    const skill = makeSkill({
      name: 'testing',
      activation: 'agent',
      content: 'Testing best practices.',
    })
    const tool = createLoadSkillTool([skill])

    const result = await tool.execute({ name: 'testing' })
    expect(result.success).toBe(true)
    expect(result.output).toBe('Testing best practices.')
  })

  it('returns already-active message for auto skills', async () => {
    const skill = makeSkill({ name: 'react', activation: 'auto', content: 'React content' })
    const tool = createLoadSkillTool([skill])

    const result = await tool.execute({ name: 'react' })
    expect(result.success).toBe(true)
    expect(result.output).toContain('already active')
    expect(result.output).toContain('React content')
  })

  it('returns already-active message for always skills', async () => {
    const skill = makeSkill({ name: 'global', activation: 'always', content: 'Global rules' })
    const tool = createLoadSkillTool([skill])

    const result = await tool.execute({ name: 'global' })
    expect(result.success).toBe(true)
    expect(result.output).toContain('already active')
  })

  it('loads a skill by case-insensitive name', async () => {
    const skill = makeSkill({
      name: 'React-Patterns',
      activation: 'agent',
      content: 'Hooks are great.',
    })
    const tool = createLoadSkillTool([skill])

    const result = await tool.execute({ name: 'react-patterns' })
    expect(result.success).toBe(true)
    expect(result.output).toBe('Hooks are great.')
  })

  it('returns error when skill not found', async () => {
    const skill = makeSkill({ name: 'react-patterns' })
    const tool = createLoadSkillTool([skill])

    const result = await tool.execute({ name: 'vue-patterns' })
    expect(result.success).toBe(false)
    expect(result.error).toContain('vue-patterns')
    expect(result.error).toContain('not found')
    expect(result.error).toContain('react-patterns')
  })

  it('lists available skills when not found', async () => {
    const skills = [
      makeSkill({ name: 'react' }),
      makeSkill({ name: 'typescript' }),
      makeSkill({ name: 'testing' }),
    ]
    const tool = createLoadSkillTool(skills)

    const result = await tool.execute({ name: 'python' })
    expect(result.success).toBe(false)
    expect(result.error).toContain('react')
    expect(result.error).toContain('typescript')
    expect(result.error).toContain('testing')
  })

  it('returns "none" when no skills are available', async () => {
    const tool = createLoadSkillTool([])

    const result = await tool.execute({ name: 'anything' })
    expect(result.success).toBe(false)
    expect(result.error).toContain('none')
  })

  it('reflects skills added after tool creation (shared array)', async () => {
    const skills: Skill[] = []
    const tool = createLoadSkillTool(skills)

    // Initially empty
    const result1 = await tool.execute({ name: 'new-skill' })
    expect(result1.success).toBe(false)

    // Add a manual skill after creation
    skills.push(
      makeSkill({ name: 'new-skill', activation: 'manual', content: 'dynamically added' })
    )

    const result2 = await tool.execute({ name: 'new-skill' })
    expect(result2.success).toBe(true)
    expect(result2.output).toBe('dynamically added')
  })
})
