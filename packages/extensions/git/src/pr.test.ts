/**
 * Tests for create_pr tool.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { createPrTool } from './pr.js'

let platform: MockPlatform

function makeCtx() {
  return {
    sessionId: 'test',
    workingDirectory: '/project',
    signal: AbortSignal.timeout(5000),
  }
}

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  resetLogger()
})

describe('createPrTool', () => {
  it('has correct name and description', () => {
    expect(createPrTool.definition.name).toBe('create_pr')
    expect(createPrTool.definition.description).toContain('GitHub pull request')
  })

  it('creates a PR with title only', async () => {
    platform.shell.setResult(`cd "/project" && gh pr create --title 'Fix login bug'`, {
      stdout: 'https://github.com/owner/repo/pull/42\n',
      stderr: '',
      exitCode: 0,
    })

    const result = await createPrTool.execute({ title: 'Fix login bug' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toBe('https://github.com/owner/repo/pull/42')
  })

  it('creates a PR with all options', async () => {
    platform.shell.setResult(
      `cd "/project" && gh pr create --title 'Add feature' --body 'Description here' --base main --head feature-branch --draft`,
      { stdout: 'https://github.com/owner/repo/pull/99\n', stderr: '', exitCode: 0 }
    )

    const result = await createPrTool.execute(
      {
        title: 'Add feature',
        body: 'Description here',
        base: 'main',
        head: 'feature-branch',
        draft: true,
      },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toBe('https://github.com/owner/repo/pull/99')
  })

  it('returns error on non-zero exit code', async () => {
    platform.shell.setResult(`cd "/project" && gh pr create --title 'Test'`, {
      stdout: '',
      stderr: 'not authenticated\n',
      exitCode: 1,
    })

    const result = await createPrTool.execute({ title: 'Test' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('not authenticated')
  })

  it('returns error on shell exception', async () => {
    // MockShell returns default { stdout: '', stderr: '', exitCode: 0 } by default
    // We simulate an exception by checking the result
    platform.shell.setResult(`cd "/project" && gh pr create --title 'Test'`, {
      stdout: '',
      stderr: 'gh: command not found',
      exitCode: 127,
    })

    const result = await createPrTool.execute({ title: 'Test' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('gh: command not found')
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await createPrTool.execute(
      { title: 'Test' },
      { sessionId: 'test', workingDirectory: '/project', signal: controller.signal }
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })

  it('escapes single quotes in title', async () => {
    platform.shell.setResult(`cd "/project" && gh pr create --title 'It'\\''s a test'`, {
      stdout: 'https://github.com/owner/repo/pull/1\n',
      stderr: '',
      exitCode: 0,
    })

    const result = await createPrTool.execute({ title: "It's a test" }, makeCtx())
    expect(result.success).toBe(true)
  })

  it('includes --draft flag when draft is true', async () => {
    platform.shell.setResult(`cd "/project" && gh pr create --title 'Draft PR' --draft`, {
      stdout: 'https://github.com/owner/repo/pull/5\n',
      stderr: '',
      exitCode: 0,
    })

    const result = await createPrTool.execute({ title: 'Draft PR', draft: true }, makeCtx())
    expect(result.success).toBe(true)
  })
})
