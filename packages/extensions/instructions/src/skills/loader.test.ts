import { describe, expect, it } from 'vitest'
import { parseSkillFile } from './loader.js'

describe('parseSkillFile', () => {
  it('parses valid SKILL.md with frontmatter', () => {
    const raw = [
      '---',
      'name: react-patterns',
      'description: React best practices',
      'globs:',
      '  - "*.tsx"',
      '  - "*.jsx"',
      '---',
      'Use functional components with hooks.',
    ].join('\n')

    const skill = parseSkillFile(raw, '/project/.ava/skills/react/SKILL.md')
    expect(skill).not.toBeNull()
    expect(skill!.name).toBe('react-patterns')
    expect(skill!.description).toBe('React best practices')
    expect(skill!.globs).toEqual(['*.tsx', '*.jsx'])
    expect(skill!.content).toBe('Use functional components with hooks.')
    expect(skill!.source).toBe('/project/.ava/skills/react/SKILL.md')
  })

  it('parses frontmatter with projectTypes', () => {
    const raw = [
      '---',
      'name: node-skill',
      'description: Node patterns',
      'globs:',
      '  - "*.ts"',
      'projectTypes:',
      '  - node',
      '  - express',
      '---',
      'Use async/await.',
    ].join('\n')

    const skill = parseSkillFile(raw, 'test.md')
    expect(skill).not.toBeNull()
    expect(skill!.projectTypes).toEqual(['node', 'express'])
  })

  it('returns null for missing name', () => {
    const raw = [
      '---',
      'description: no name here',
      'globs:',
      '  - "*.ts"',
      '---',
      'Some content.',
    ].join('\n')

    expect(parseSkillFile(raw, 'test.md')).toBeNull()
  })

  it('returns null for missing globs', () => {
    const raw = [
      '---',
      'name: no-globs',
      'description: missing globs',
      '---',
      'Some content.',
    ].join('\n')

    expect(parseSkillFile(raw, 'test.md')).toBeNull()
  })

  it('returns null for empty content', () => {
    const raw = ['---', 'name: empty-content', 'globs:', '  - "*.ts"', '---', ''].join('\n')

    expect(parseSkillFile(raw, 'test.md')).toBeNull()
  })

  it('returns null for whitespace-only content', () => {
    const raw = ['---', 'name: whitespace-only', 'globs:', '  - "*.ts"', '---', '   '].join('\n')

    expect(parseSkillFile(raw, 'test.md')).toBeNull()
  })
})
