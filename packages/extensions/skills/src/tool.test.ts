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

  it('loads a skill by exact name', async () => {
    const skill = makeSkill({
      name: 'react-patterns',
      content: 'Use hooks and functional components.',
    })
    const tool = createLoadSkillTool([skill])

    const result = await tool.execute({ name: 'react-patterns' })
    expect(result.success).toBe(true)
    expect(result.output).toBe('Use hooks and functional components.')
  })

  it('loads a skill by case-insensitive name', async () => {
    const skill = makeSkill({ name: 'React-Patterns', content: 'Hooks are great.' })
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

  it('finds first match (exact or case-insensitive)', async () => {
    const skills = [
      makeSkill({ name: 'React', content: 'uppercase React' }),
      makeSkill({ name: 'react', content: 'lowercase react' }),
    ]
    const tool = createLoadSkillTool(skills)

    // 'react' matches 'React' via case-insensitive check first (array order)
    const result = await tool.execute({ name: 'react' })
    expect(result.success).toBe(true)
    expect(result.output).toBe('uppercase React')

    // Exact case match
    const result2 = await tool.execute({ name: 'React' })
    expect(result2.success).toBe(true)
    expect(result2.output).toBe('uppercase React')
  })

  it('reflects skills added after tool creation (shared array)', async () => {
    const skills: Skill[] = []
    const tool = createLoadSkillTool(skills)

    // Initially empty
    const result1 = await tool.execute({ name: 'new-skill' })
    expect(result1.success).toBe(false)

    // Add a skill after creation
    skills.push(makeSkill({ name: 'new-skill', content: 'dynamically added' }))

    const result2 = await tool.execute({ name: 'new-skill' })
    expect(result2.success).toBe(true)
    expect(result2.output).toBe('dynamically added')
  })
})
