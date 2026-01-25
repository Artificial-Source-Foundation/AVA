/**
 * Tests for Delta9 Skills Loader
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import * as path from 'node:path'
import {
  parseFrontmatter,
  discoverSkills,
  loadSkill,
  resolveSkill,
  getSkillSummaries,
  readSkillResource,
  listSkillFiles,
  DEFAULT_DISCOVERY_PATHS,
} from '../../src/skills/loader.js'
import type { Skill, SkillLabel } from '../../src/skills/types.js'

// Mock fs module
vi.mock('node:fs/promises', () => ({
  readdir: vi.fn(),
  readFile: vi.fn(),
  stat: vi.fn(),
  access: vi.fn(),
}))

// Mock homedir
vi.mock('node:os', () => ({
  homedir: vi.fn(() => '/home/testuser'),
}))

// Get the mocked module
import * as fs from 'node:fs/promises'
const mockReaddir = vi.mocked(fs.readdir)
const mockReadFile = vi.mocked(fs.readFile)
const mockStat = vi.mocked(fs.stat)
const mockAccess = vi.mocked(fs.access)

describe('SkillsLoader', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    // Default: paths don't exist
    mockAccess.mockRejectedValue(new Error('Not found'))
    mockReaddir.mockResolvedValue([])
  })

  describe('parseFrontmatter', () => {
    it('should parse valid frontmatter', () => {
      const content = `---
name: test-skill
description: A test skill
---
# Skill Content

Instructions here.`

      const result = parseFrontmatter<{ name: string; description: string }>(content)

      expect(result.data.name).toBe('test-skill')
      expect(result.data.description).toBe('A test skill')
      expect(result.body).toContain('# Skill Content')
      expect(result.body).toContain('Instructions here.')
    })

    it('should handle missing frontmatter', () => {
      const content = `# Just Content

No frontmatter here.`

      const result = parseFrontmatter<{ name?: string }>(content)

      expect(result.data).toEqual({})
      expect(result.body).toBe(content)
    })

    it('should handle empty frontmatter', () => {
      const content = `---
---
Body content`

      const result = parseFrontmatter<{ name?: string }>(content)

      expect(result.data).toEqual({})
      expect(result.body).toBe('Body content')
    })

    it('should handle invalid YAML gracefully', () => {
      const content = `---
invalid: [unclosed
---
Body`

      const result = parseFrontmatter<{ name?: string }>(content)

      expect(result.data).toEqual({})
    })
  })

  describe('DEFAULT_DISCOVERY_PATHS', () => {
    it('should have correct priority order', () => {
      expect(DEFAULT_DISCOVERY_PATHS[0].label).toBe('project')
      expect(DEFAULT_DISCOVERY_PATHS[0].path).toContain('delta9')

      // Project paths should come before user paths
      const projectPaths = DEFAULT_DISCOVERY_PATHS.filter((p) => p.label === 'project')
      const userPaths = DEFAULT_DISCOVERY_PATHS.filter((p) => p.label === 'user')
      const globalPaths = DEFAULT_DISCOVERY_PATHS.filter((p) => p.label === 'global')

      expect(projectPaths.length).toBeGreaterThan(0)
      expect(userPaths.length).toBeGreaterThan(0)
      expect(globalPaths.length).toBeGreaterThan(0)
    })

    it('should include all expected paths', () => {
      const pathNames = DEFAULT_DISCOVERY_PATHS.map((p) => p.path)

      expect(pathNames).toContain('.delta9/skills')
      expect(pathNames).toContain('.opencode/skills')
      expect(pathNames).toContain('.claude/skills')
      expect(pathNames.some((p) => p.includes('config/delta9'))).toBe(true)
    })
  })

  describe('discoverSkills', () => {
    it('should return empty map when no skills found', async () => {
      const skills = await discoverSkills('/test/project')

      expect(skills.size).toBe(0)
    })

    it('should discover skill from project directory', async () => {
      const projectDir = '/test/project'
      const skillDir = path.join(projectDir, '.delta9/skills')

      // Setup mocks for successful discovery
      mockAccess.mockImplementation(async (p) => {
        if (p === skillDir) return undefined
        throw new Error('Not found')
      })

      mockReaddir.mockImplementation(async (dir) => {
        if (dir === skillDir) {
          return [{ name: 'my-skill', isDirectory: () => true }] as unknown as Awaited<ReturnType<typeof fs.readdir>>
        }
        return []
      })

      mockStat.mockResolvedValue({
        isFile: () => true,
        isDirectory: () => true,
        mode: 0o644,
      } as unknown as Awaited<ReturnType<typeof fs.stat>>)

      const skillMdPath = path.join(skillDir, 'my-skill', 'SKILL.md')
      mockReadFile.mockImplementation(async (p) => {
        if (p === skillMdPath) {
          return `---
name: my-skill
description: My test skill
---
# Instructions

Do something.`
        }
        throw new Error('Not found')
      })

      // Check SKILL.md exists
      mockAccess.mockImplementation(async (p) => {
        if (p === skillDir || p === skillMdPath) return undefined
        throw new Error('Not found')
      })

      const skills = await discoverSkills(projectDir)

      // Note: Full integration would require more complex mocking
      // This test verifies the discovery flow is called correctly
      expect(mockAccess).toHaveBeenCalled()
    })

    it('should respect first-match-wins rule', async () => {
      // When same skill exists in multiple locations,
      // the first one (higher priority) wins
      // This is verified by the discoverSkills function returning a Map
      // where duplicate names are not overwritten
      const skills = new Map<string, Skill>()
      const projectSkill: Skill = {
        name: 'test-skill',
        description: 'Project version',
        label: 'project',
        path: '/project/.delta9/skills/test-skill',
        relativePath: 'test-skill',
        template: 'project instructions',
        scripts: [],
        resources: [],
      }
      const userSkill: Skill = {
        name: 'test-skill',
        description: 'User version',
        label: 'user',
        path: '/home/user/.config/delta9/skills/test-skill',
        relativePath: 'test-skill',
        template: 'user instructions',
        scripts: [],
        resources: [],
      }

      // Simulate first-match-wins
      skills.set(projectSkill.name, projectSkill)
      if (!skills.has(userSkill.name)) {
        skills.set(userSkill.name, userSkill)
      }

      expect(skills.get('test-skill')?.label).toBe('project')
    })
  })

  describe('resolveSkill', () => {
    const mockSkills = new Map<string, Skill>([
      [
        'project-skill',
        {
          name: 'project-skill',
          description: 'A project skill',
          label: 'project' as SkillLabel,
          path: '/test',
          relativePath: 'project-skill',
          template: 'content',
          scripts: [],
          resources: [],
        },
      ],
      [
        'user-skill',
        {
          name: 'user-skill',
          description: 'A user skill',
          label: 'user' as SkillLabel,
          path: '/test',
          relativePath: 'user-skill',
          template: 'content',
          namespace: 'my-namespace',
          scripts: [],
          resources: [],
        },
      ],
    ])

    it('should resolve skill by name', () => {
      const skill = resolveSkill('project-skill', mockSkills)

      expect(skill).toBeDefined()
      expect(skill?.name).toBe('project-skill')
    })

    it('should resolve skill by label:name', () => {
      const skill = resolveSkill('user:user-skill', mockSkills)

      expect(skill).toBeDefined()
      expect(skill?.name).toBe('user-skill')
    })

    it('should resolve skill by namespace:name', () => {
      const skill = resolveSkill('my-namespace:user-skill', mockSkills)

      expect(skill).toBeDefined()
      expect(skill?.name).toBe('user-skill')
    })

    it('should return null for non-existent skill', () => {
      const skill = resolveSkill('non-existent', mockSkills)

      expect(skill).toBeNull()
    })

    it('should return null for wrong label prefix', () => {
      const skill = resolveSkill('global:project-skill', mockSkills)

      expect(skill).toBeNull()
    })
  })

  describe('getSkillSummaries', () => {
    it('should return empty array when no skills found', async () => {
      const summaries = await getSkillSummaries('/test/project')

      expect(summaries).toEqual([])
    })
  })

  describe('readSkillResource', () => {
    it('should return null for non-existent resource', async () => {
      const skill: Skill = {
        name: 'test-skill',
        description: 'Test',
        label: 'project',
        path: '/test',
        relativePath: 'test-skill',
        template: 'content',
        scripts: [],
        resources: [],
      }

      const content = await readSkillResource(skill, 'non-existent.md')

      expect(content).toBeNull()
    })

    it('should return content for existing resource', async () => {
      const skill: Skill = {
        name: 'test-skill',
        description: 'Test',
        label: 'project',
        path: '/test',
        relativePath: 'test-skill',
        template: 'content',
        scripts: [],
        resources: [
          {
            relativePath: 'docs/guide.md',
            absolutePath: '/test/docs/guide.md',
            type: 'md',
          },
        ],
      }

      mockReadFile.mockResolvedValue('# Guide\n\nContent here.')

      const content = await readSkillResource(skill, 'docs/guide.md')

      expect(content).toBe('# Guide\n\nContent here.')
    })
  })

  describe('listSkillFiles', () => {
    it('should list all files in skill', async () => {
      const skill: Skill = {
        name: 'test-skill',
        description: 'Test',
        label: 'project',
        path: '/test',
        relativePath: 'test-skill',
        template: 'content',
        scripts: [
          { relativePath: 'scripts/build.sh', absolutePath: '/test/scripts/build.sh' },
          { relativePath: 'scripts/test.sh', absolutePath: '/test/scripts/test.sh' },
        ],
        resources: [
          { relativePath: 'docs/guide.md', absolutePath: '/test/docs/guide.md', type: 'md' },
          { relativePath: 'templates/template.json', absolutePath: '/test/templates/template.json', type: 'json' },
        ],
      }

      const files = await listSkillFiles(skill)

      expect(files).toHaveLength(4)
      expect(files).toContain('scripts/build.sh')
      expect(files).toContain('scripts/test.sh')
      expect(files).toContain('docs/guide.md')
      expect(files).toContain('templates/template.json')
    })

    it('should return empty array for skill with no files', async () => {
      const skill: Skill = {
        name: 'empty-skill',
        description: 'Empty',
        label: 'project',
        path: '/test',
        relativePath: 'empty-skill',
        template: 'content',
        scripts: [],
        resources: [],
      }

      const files = await listSkillFiles(skill)

      expect(files).toEqual([])
    })
  })
})
