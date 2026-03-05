import { describe, expect, it } from 'vitest'
import { matchSkills } from './matcher.js'
import type { Skill } from './types.js'

const reactSkill: Skill = {
  name: 'react',
  description: 'React patterns',
  globs: ['**/*.tsx', '**/*.jsx'],
  content: 'Use functional components',
  source: 'built-in',
}

const pythonSkill: Skill = {
  name: 'python',
  description: 'Python patterns',
  globs: ['**/*.py'],
  content: 'Use type hints',
  source: 'built-in',
}

describe('matchSkills', () => {
  it('returns empty array when no skills match', () => {
    const matches = matchSkills([reactSkill], ['src/main.rs'])
    expect(matches).toHaveLength(0)
  })

  it('matches skills by glob', () => {
    const matches = matchSkills([reactSkill], ['src/App.tsx'])
    expect(matches).toHaveLength(1)
    expect(matches[0].skill.name).toBe('react')
    expect(matches[0].matchedFile).toBe('src/App.tsx')
  })

  it('matches multiple skills', () => {
    const matches = matchSkills([reactSkill, pythonSkill], ['src/App.tsx', 'main.py'])
    expect(matches).toHaveLength(2)
  })

  it('respects maxActive config', () => {
    const matches = matchSkills([reactSkill, pythonSkill], ['src/App.tsx', 'main.py'], {
      maxActive: 1,
      maxContentLength: 10_000,
    })
    expect(matches).toHaveLength(1)
  })

  it('returns one match per skill', () => {
    const matches = matchSkills([reactSkill], ['a.tsx', 'b.tsx'])
    expect(matches).toHaveLength(1)
  })

  it('handles empty skills list', () => {
    const matches = matchSkills([], ['src/App.tsx'])
    expect(matches).toEqual([])
  })

  it('handles empty files list', () => {
    const matches = matchSkills([reactSkill], [])
    expect(matches).toEqual([])
  })
})
