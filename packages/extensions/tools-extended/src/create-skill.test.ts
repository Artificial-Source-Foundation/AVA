import type { MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import * as api from '@ava/core-v2/extensions'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createSkillTool } from './create-skill.js'

describe('create_skill tool', () => {
  let mockPlatform: MockPlatform

  beforeEach(() => {
    mockPlatform = installMockPlatform()
    vi.spyOn(api, 'emitEvent').mockImplementation(() => {})
  })

  afterEach(() => {
    vi.restoreAllMocks()
  })

  const ctx = {
    workingDirectory: '/project',
    sessionId: 'test',
    signal: new AbortController().signal,
  }

  it('has correct tool name', () => {
    expect(createSkillTool.definition.name).toBe('create_skill')
  })

  it('creates a skill file in the correct directory', async () => {
    const result = await createSkillTool.execute(
      {
        name: 'react-hooks',
        description: 'React hooks patterns',
        globs: ['**/*.tsx'],
        activation: 'auto',
        content: 'Use custom hooks for shared logic.',
      },
      ctx
    )

    expect(result.success).toBe(true)
    expect(result.output).toContain('react-hooks')

    const content = mockPlatform.fs.files.get('/project/.ava/skills/react-hooks/SKILL.md')
    expect(content).toBeDefined()
    expect(content).toContain('name: react-hooks')
    expect(content).toContain('description: React hooks patterns')
    expect(content).toContain('**/*.tsx')
    expect(content).toContain('Use custom hooks for shared logic.')
  })

  it('includes activation mode when not auto', async () => {
    await createSkillTool.execute(
      {
        name: 'manual-skill',
        description: 'Manual only',
        globs: ['*.ts'],
        activation: 'manual',
        content: 'Manual content.',
      },
      ctx
    )

    const content = mockPlatform.fs.files.get('/project/.ava/skills/manual-skill/SKILL.md')!
    expect(content).toContain('activation: manual')
  })

  it('omits activation for auto (default)', async () => {
    await createSkillTool.execute(
      {
        name: 'auto-skill',
        description: 'Auto',
        globs: ['*.ts'],
        activation: 'auto',
        content: 'Auto content.',
      },
      ctx
    )

    const content = mockPlatform.fs.files.get('/project/.ava/skills/auto-skill/SKILL.md')!
    expect(content).not.toContain('activation:')
  })

  it('emits skills:register event', async () => {
    await createSkillTool.execute(
      {
        name: 'test-skill',
        description: 'Test',
        globs: ['*.ts'],
        activation: 'auto',
        content: 'Content',
      },
      ctx
    )

    expect(api.emitEvent).toHaveBeenCalledWith(
      'skills:register',
      expect.objectContaining({ name: 'test-skill' })
    )
  })
})
