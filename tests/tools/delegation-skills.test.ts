/**
 * Delegation Skills Loading Tests
 */

import { describe, it, expect, vi, beforeEach } from 'vitest'
import { loadSkillsForAgent } from '../../src/tools/skills.js'

// Mock the skills module
vi.mock('../../src/skills/index.js', () => ({
  discoverSkills: vi.fn(),
  resolveSkill: vi.fn(),
  injectSkill: vi.fn(),
}))

import { discoverSkills, resolveSkill, injectSkill } from '../../src/skills/index.js'

const mockDiscoverSkills = vi.mocked(discoverSkills)
const mockResolveSkill = vi.mocked(resolveSkill)
const mockInjectSkill = vi.mocked(injectSkill)

describe('loadSkillsForAgent', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('should return empty array for empty skill names', async () => {
    const result = await loadSkillsForAgent('/cwd', [])
    expect(result).toEqual([])
    expect(mockDiscoverSkills).not.toHaveBeenCalled()
  })

  it('should return empty array for undefined skill names', async () => {
    const result = await loadSkillsForAgent('/cwd', undefined as unknown as string[])
    expect(result).toEqual([])
  })

  it('should load single skill successfully', async () => {
    const mockSkillsMap = new Map([
      [
        'typescript-patterns',
        {
          name: 'typescript-patterns',
          description: 'TypeScript patterns',
          path: '/skills/typescript-patterns',
          label: 'project',
        },
      ],
    ])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill.mockReturnValue(mockSkillsMap.get('typescript-patterns') as never)
    mockInjectSkill.mockReturnValue({
      success: true,
      content: '# TypeScript Patterns\n\nUse strict typing...',
    } as never)

    const result = await loadSkillsForAgent('/cwd', ['typescript-patterns'])

    expect(result).toHaveLength(1)
    expect(result[0]).toContain('TypeScript Patterns')
    expect(mockDiscoverSkills).toHaveBeenCalledWith('/cwd')
    expect(mockResolveSkill).toHaveBeenCalledWith('typescript-patterns', mockSkillsMap)
    expect(mockInjectSkill).toHaveBeenCalled()
  })

  it('should load multiple skills successfully', async () => {
    const mockSkillsMap = new Map([
      ['skill-a', { name: 'skill-a', path: '/skills/a' }],
      ['skill-b', { name: 'skill-b', path: '/skills/b' }],
    ])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill
      .mockReturnValueOnce(mockSkillsMap.get('skill-a') as never)
      .mockReturnValueOnce(mockSkillsMap.get('skill-b') as never)
    mockInjectSkill
      .mockReturnValueOnce({ success: true, content: 'Content A' } as never)
      .mockReturnValueOnce({ success: true, content: 'Content B' } as never)

    const result = await loadSkillsForAgent('/cwd', ['skill-a', 'skill-b'])

    expect(result).toHaveLength(2)
    expect(result[0]).toBe('Content A')
    expect(result[1]).toBe('Content B')
  })

  it('should skip skills that are not found', async () => {
    const mockSkillsMap = new Map([['existing', { name: 'existing', path: '/skills/existing' }]])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill
      .mockReturnValueOnce(mockSkillsMap.get('existing') as never)
      .mockReturnValueOnce(undefined as never) // not-found
    mockInjectSkill.mockReturnValue({ success: true, content: 'Content' } as never)

    const result = await loadSkillsForAgent('/cwd', ['existing', 'not-found'])

    expect(result).toHaveLength(1)
    expect(result[0]).toBe('Content')
  })

  it('should skip skills that fail to inject', async () => {
    const mockSkillsMap = new Map([
      ['good-skill', { name: 'good-skill', path: '/skills/good' }],
      ['bad-skill', { name: 'bad-skill', path: '/skills/bad' }],
    ])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill
      .mockReturnValueOnce(mockSkillsMap.get('good-skill') as never)
      .mockReturnValueOnce(mockSkillsMap.get('bad-skill') as never)
    mockInjectSkill
      .mockReturnValueOnce({ success: true, content: 'Good content' } as never)
      .mockReturnValueOnce({ success: false, error: 'Failed to load' } as never)

    const result = await loadSkillsForAgent('/cwd', ['good-skill', 'bad-skill'])

    expect(result).toHaveLength(1)
    expect(result[0]).toBe('Good content')
  })

  it('should pass provider and model IDs to injectSkill', async () => {
    const mockSkillsMap = new Map([['skill', { name: 'skill', path: '/skills/skill' }]])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill.mockReturnValue(mockSkillsMap.get('skill') as never)
    mockInjectSkill.mockReturnValue({ success: true, content: 'Content' } as never)

    await loadSkillsForAgent('/cwd', ['skill'], 'anthropic', 'claude-3-opus')

    expect(mockInjectSkill).toHaveBeenCalledWith(
      expect.anything(),
      'anthropic',
      'claude-3-opus'
    )
  })

  it('should handle empty content from injection', async () => {
    const mockSkillsMap = new Map([['skill', { name: 'skill', path: '/skills/skill' }]])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill.mockReturnValue(mockSkillsMap.get('skill') as never)
    mockInjectSkill.mockReturnValue({ success: true, content: '' } as never)

    const result = await loadSkillsForAgent('/cwd', ['skill'])

    // Empty content should not be included
    expect(result).toHaveLength(0)
  })
})

describe('delegation skill integration', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  it('should format skills with XML tags', async () => {
    const mockSkillsMap = new Map([
      ['typescript', { name: 'typescript', path: '/skills/ts' }],
    ])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill.mockReturnValue(mockSkillsMap.get('typescript') as never)
    mockInjectSkill.mockReturnValue({
      success: true,
      content: '## TypeScript Best Practices\n\n- Use strict mode',
    } as never)

    const result = await loadSkillsForAgent('/cwd', ['typescript'])

    expect(result[0]).toContain('TypeScript Best Practices')
  })

  it('should preserve order of skills', async () => {
    const mockSkillsMap = new Map([
      ['first', { name: 'first', path: '/skills/first' }],
      ['second', { name: 'second', path: '/skills/second' }],
      ['third', { name: 'third', path: '/skills/third' }],
    ])

    mockDiscoverSkills.mockResolvedValue(mockSkillsMap as never)
    mockResolveSkill
      .mockReturnValueOnce(mockSkillsMap.get('first') as never)
      .mockReturnValueOnce(mockSkillsMap.get('second') as never)
      .mockReturnValueOnce(mockSkillsMap.get('third') as never)
    mockInjectSkill
      .mockReturnValueOnce({ success: true, content: 'First' } as never)
      .mockReturnValueOnce({ success: true, content: 'Second' } as never)
      .mockReturnValueOnce({ success: true, content: 'Third' } as never)

    const result = await loadSkillsForAgent('/cwd', ['first', 'second', 'third'])

    expect(result).toEqual(['First', 'Second', 'Third'])
  })
})
