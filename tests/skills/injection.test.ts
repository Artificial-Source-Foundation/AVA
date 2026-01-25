/**
 * Tests for Delta9 Skills Injection
 */

import { describe, it, expect, beforeEach } from 'vitest'
import {
  getFormatForModel,
  renderSkill,
  renderSkillsList,
  activateSkillInSession,
  deactivateSkillInSession,
  getActiveSkills,
  isSkillActive,
  clearSessionSkills,
  clearAllSessionSkills,
  injectSkill,
} from '../../src/skills/injection.js'
import type { Skill, SkillSummary } from '../../src/skills/types.js'

describe('SkillsInjection', () => {
  beforeEach(() => {
    clearAllSessionSkills()
  })

  describe('getFormatForModel', () => {
    it('should return xml for Claude models', () => {
      expect(getFormatForModel('anthropic', 'claude-3-opus')).toBe('xml')
      expect(getFormatForModel('anthropic', 'claude-sonnet-4')).toBe('xml')
      expect(getFormatForModel('anthropic', 'claude-haiku-3.5')).toBe('xml')
    })

    it('should return json for OpenAI models', () => {
      expect(getFormatForModel('openai', 'gpt-4o')).toBe('json')
      expect(getFormatForModel('openai', 'gpt-4-turbo')).toBe('json')
      expect(getFormatForModel('openai', 'o1-preview')).toBe('json')
    })

    it('should return md for Google models', () => {
      expect(getFormatForModel('google', 'gemini-pro')).toBe('md')
    })

    it('should return json for DeepSeek models', () => {
      expect(getFormatForModel('deepseek', 'deepseek-v3')).toBe('json')
    })

    it('should default to xml when provider unknown', () => {
      expect(getFormatForModel(undefined, undefined)).toBe('xml')
      expect(getFormatForModel('unknown-provider', 'unknown-model')).toBe('xml')
    })

    it('should prioritize model ID over provider', () => {
      // Even if provider is google, claude model ID should use xml
      expect(getFormatForModel('google', 'claude-3-opus')).toBe('xml')
    })
  })

  describe('renderSkill', () => {
    const testSkill: Skill = {
      name: 'test-skill',
      description: 'A test skill for testing',
      label: 'project',
      path: '/test/skills/test-skill',
      relativePath: 'test-skill',
      template: '# Instructions\n\nDo the thing.',
      useWhen: 'When testing',
      allowedTools: ['Read', 'Write'],
      scripts: [{ relativePath: 'scripts/build.sh', absolutePath: '/test/scripts/build.sh' }],
      resources: [{ relativePath: 'docs/guide.md', absolutePath: '/test/docs/guide.md', type: 'md' }],
    }

    it('should render in XML format', () => {
      const result = renderSkill(testSkill, 'xml')

      expect(result).toContain('<skill name="test-skill">')
      expect(result).toContain('<description>A test skill for testing</description>')
      expect(result).toContain('<source>project</source>')
      expect(result).toContain('<use-when>When testing</use-when>')
      expect(result).toContain('<allowed-tools>Read, Write</allowed-tools>')
      expect(result).toContain('<scripts>')
      expect(result).toContain('<script path="scripts/build.sh" />')
      expect(result).toContain('<resources>')
      expect(result).toContain('<resource path="docs/guide.md" type="md" />')
      expect(result).toContain('<content>')
      expect(result).toContain('# Instructions')
      expect(result).toContain('</skill>')
    })

    it('should render in JSON format', () => {
      const result = renderSkill(testSkill, 'json')
      const parsed = JSON.parse(result)

      expect(parsed.skill.name).toBe('test-skill')
      expect(parsed.skill.description).toBe('A test skill for testing')
      expect(parsed.skill.source).toBe('project')
      expect(parsed.skill.useWhen).toBe('When testing')
      expect(parsed.skill.allowedTools).toEqual(['Read', 'Write'])
      expect(parsed.skill.scripts).toContain('scripts/build.sh')
      expect(parsed.skill.content).toContain('# Instructions')
    })

    it('should render in Markdown format', () => {
      const result = renderSkill(testSkill, 'md')

      expect(result).toContain('## Skill: test-skill')
      expect(result).toContain('**Description:** A test skill for testing')
      expect(result).toContain('**Source:** project')
      expect(result).toContain('**Use when:** When testing')
      expect(result).toContain('**Allowed tools:** Read, Write')
      expect(result).toContain('### Scripts')
      expect(result).toContain('`scripts/build.sh`')
      expect(result).toContain('### Resources')
      expect(result).toContain('`docs/guide.md`')
      expect(result).toContain('### Instructions')
      expect(result).toContain('# Instructions')
    })

    it('should respect includeScripts option', () => {
      const result = renderSkill(testSkill, 'xml', { includeScripts: false })

      expect(result).not.toContain('<scripts>')
    })

    it('should respect includeResources option', () => {
      const result = renderSkill(testSkill, 'xml', { includeResources: false })

      expect(result).not.toContain('<resources>')
    })

    it('should escape XML special characters', () => {
      const skillWithSpecialChars: Skill = {
        ...testSkill,
        name: 'skill-name',
        description: 'Test <with> special & "chars"',
      }

      const result = renderSkill(skillWithSpecialChars, 'xml')

      expect(result).toContain('&lt;with&gt;')
      expect(result).toContain('&amp;')
      expect(result).toContain('&quot;chars&quot;')
    })
  })

  describe('renderSkillsList', () => {
    const testSkills: SkillSummary[] = [
      { name: 'skill-a', description: 'Skill A description', label: 'project', useWhen: 'When doing A' },
      { name: 'skill-b', description: 'Skill B description', label: 'user' },
    ]

    it('should render skills list in XML', () => {
      const result = renderSkillsList(testSkills, 'xml')

      expect(result).toContain('<available-skills>')
      expect(result).toContain('<instructions>')
      expect(result).toContain('use_skill')
      expect(result).toContain('<skills>')
      expect(result).toContain('<skill name="skill-a">')
      expect(result).toContain('<description>Skill A description</description>')
      expect(result).toContain('<source>project</source>')
      expect(result).toContain('<use-when>When doing A</use-when>')
      expect(result).toContain('<skill name="skill-b">')
      expect(result).toContain('</available-skills>')
    })

    it('should render skills list in JSON', () => {
      const result = renderSkillsList(testSkills, 'json')
      const parsed = JSON.parse(result)

      expect(parsed.availableSkills.instructions).toContain('use_skill')
      expect(parsed.availableSkills.skills).toHaveLength(2)
      expect(parsed.availableSkills.skills[0].name).toBe('skill-a')
      expect(parsed.availableSkills.skills[1].name).toBe('skill-b')
    })

    it('should render skills list in Markdown', () => {
      const result = renderSkillsList(testSkills, 'md')

      expect(result).toContain('## Available Skills')
      expect(result).toContain('use_skill')
      expect(result).toContain('**skill-a** (project): Skill A description')
      expect(result).toContain('Use when: When doing A')
      expect(result).toContain('**skill-b** (user): Skill B description')
    })
  })

  describe('Session Tracking', () => {
    describe('activateSkillInSession', () => {
      it('should activate a skill in a session', () => {
        activateSkillInSession('session-1', 'skill-a')

        expect(isSkillActive('session-1', 'skill-a')).toBe(true)
      })

      it('should handle multiple skills in same session', () => {
        activateSkillInSession('session-1', 'skill-a')
        activateSkillInSession('session-1', 'skill-b')

        expect(isSkillActive('session-1', 'skill-a')).toBe(true)
        expect(isSkillActive('session-1', 'skill-b')).toBe(true)
      })

      it('should handle same skill in different sessions', () => {
        activateSkillInSession('session-1', 'skill-a')
        activateSkillInSession('session-2', 'skill-a')

        expect(isSkillActive('session-1', 'skill-a')).toBe(true)
        expect(isSkillActive('session-2', 'skill-a')).toBe(true)
      })
    })

    describe('deactivateSkillInSession', () => {
      it('should deactivate a skill', () => {
        activateSkillInSession('session-1', 'skill-a')
        deactivateSkillInSession('session-1', 'skill-a')

        expect(isSkillActive('session-1', 'skill-a')).toBe(false)
      })

      it('should not affect other skills', () => {
        activateSkillInSession('session-1', 'skill-a')
        activateSkillInSession('session-1', 'skill-b')
        deactivateSkillInSession('session-1', 'skill-a')

        expect(isSkillActive('session-1', 'skill-a')).toBe(false)
        expect(isSkillActive('session-1', 'skill-b')).toBe(true)
      })
    })

    describe('getActiveSkills', () => {
      it('should return empty array for unknown session', () => {
        expect(getActiveSkills('unknown')).toEqual([])
      })

      it('should return all active skills', () => {
        activateSkillInSession('session-1', 'skill-a')
        activateSkillInSession('session-1', 'skill-b')

        const active = getActiveSkills('session-1')

        expect(active).toHaveLength(2)
        expect(active).toContain('skill-a')
        expect(active).toContain('skill-b')
      })
    })

    describe('isSkillActive', () => {
      it('should return false for unknown session', () => {
        expect(isSkillActive('unknown', 'skill-a')).toBe(false)
      })

      it('should return false for inactive skill', () => {
        activateSkillInSession('session-1', 'skill-a')

        expect(isSkillActive('session-1', 'skill-b')).toBe(false)
      })
    })

    describe('clearSessionSkills', () => {
      it('should clear all skills for a session', () => {
        activateSkillInSession('session-1', 'skill-a')
        activateSkillInSession('session-1', 'skill-b')
        clearSessionSkills('session-1')

        expect(getActiveSkills('session-1')).toEqual([])
      })

      it('should not affect other sessions', () => {
        activateSkillInSession('session-1', 'skill-a')
        activateSkillInSession('session-2', 'skill-b')
        clearSessionSkills('session-1')

        expect(getActiveSkills('session-2')).toEqual(['skill-b'])
      })
    })

    describe('clearAllSessionSkills', () => {
      it('should clear all sessions', () => {
        activateSkillInSession('session-1', 'skill-a')
        activateSkillInSession('session-2', 'skill-b')
        clearAllSessionSkills()

        expect(getActiveSkills('session-1')).toEqual([])
        expect(getActiveSkills('session-2')).toEqual([])
      })
    })
  })

  describe('injectSkill', () => {
    const testSkill: Skill = {
      name: 'test-skill',
      description: 'A test skill',
      label: 'project',
      path: '/test',
      relativePath: 'test-skill',
      template: 'Instructions here.',
      scripts: [],
      resources: [],
    }

    it('should return success with content', () => {
      const result = injectSkill(testSkill, 'anthropic', 'claude-3-opus')

      expect(result.success).toBe(true)
      expect(result.name).toBe('test-skill')
      expect(result.content).toContain('<skill')
      expect(result.error).toBeUndefined()
    })

    it('should use XML format for Claude', () => {
      const result = injectSkill(testSkill, 'anthropic', 'claude-3-opus')

      expect(result.content).toContain('<skill name="test-skill">')
    })

    it('should use JSON format for GPT', () => {
      const result = injectSkill(testSkill, 'openai', 'gpt-4o')

      expect(result.content).toContain('"skill"')
      expect(result.content).toContain('"name": "test-skill"')
    })

    it('should use Markdown format for Gemini', () => {
      const result = injectSkill(testSkill, 'google', 'gemini-pro')

      expect(result.content).toContain('## Skill: test-skill')
    })

    it('should respect format option override', () => {
      const result = injectSkill(testSkill, 'anthropic', 'claude-3-opus', { format: 'json' })

      expect(result.content).toContain('"skill"')
    })
  })
})
