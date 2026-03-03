import type { MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import * as api from '@ava/core-v2/extensions'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createRuleTool } from './create-rule.js'

describe('create_rule tool', () => {
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
    expect(createRuleTool.definition.name).toBe('create_rule')
  })

  it('creates a rule file with frontmatter', async () => {
    const result = await createRuleTool.execute(
      {
        name: 'testing',
        description: 'Testing conventions',
        globs: ['**/*.test.ts'],
        activation: 'auto',
        content: 'Always use describe/it.',
      },
      ctx
    )

    expect(result.success).toBe(true)
    expect(result.output).toContain('testing')

    const content = mockPlatform.fs.files.get('/project/.ava/rules/testing.md')
    expect(content).toBeDefined()
    expect(content).toContain('description: Testing conventions')
    expect(content).toContain('**/*.test.ts')
    expect(content).toContain('Always use describe/it.')
  })

  it('creates an always rule without globs in frontmatter', async () => {
    const result = await createRuleTool.execute(
      {
        name: 'global-style',
        description: 'Global style',
        globs: [],
        activation: 'always',
        content: 'Use semicolons.',
      },
      ctx
    )

    expect(result.success).toBe(true)
    const content = mockPlatform.fs.files.get('/project/.ava/rules/global-style.md')!
    expect(content).toContain('activation: always')
    expect(content).not.toContain('globs:')
  })

  it('emits rules:register event', async () => {
    await createRuleTool.execute(
      {
        name: 'test-rule',
        description: 'Test',
        globs: ['*.ts'],
        activation: 'auto',
        content: 'Content',
      },
      ctx
    )

    expect(api.emitEvent).toHaveBeenCalledWith(
      'rules:register',
      expect.objectContaining({ name: 'test-rule', activation: 'auto' })
    )
  })
})
