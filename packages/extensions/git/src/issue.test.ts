/**
 * Tests for read_issue tool.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { readIssueTool } from './issue.js'

let platform: MockPlatform

function makeCtx() {
  return {
    sessionId: 'test',
    workingDirectory: '/project',
    signal: AbortSignal.timeout(5000),
  }
}

const sampleIssue = {
  title: 'Fix login page crash',
  body: 'The login page crashes when clicking submit without a password.',
  state: 'OPEN',
  labels: [{ name: 'bug' }, { name: 'priority:high' }],
  comments: [
    { body: 'I can reproduce this on Chrome.', author: { login: 'alice' } },
    { body: 'Fixed in PR #45.', author: { login: 'bob' } },
  ],
}

beforeEach(() => {
  platform = installMockPlatform()
})

afterEach(() => {
  resetLogger()
})

describe('readIssueTool', () => {
  it('has correct name', () => {
    expect(readIssueTool.definition.name).toBe('read_issue')
  })

  it('reads an issue with all fields', async () => {
    platform.shell.setResult(
      'cd "/project" && gh issue view 42 --json title,body,comments,labels,state',
      { stdout: `${JSON.stringify(sampleIssue)}\n`, stderr: '', exitCode: 0 }
    )

    const result = await readIssueTool.execute({ number: 42 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('Issue #42: Fix login page crash')
    expect(result.output).toContain('State: OPEN')
    expect(result.output).toContain('Labels: bug, priority:high')
    expect(result.output).toContain('login page crashes')
    expect(result.output).toContain('**alice:**')
    expect(result.output).toContain('I can reproduce this on Chrome.')
    expect(result.output).toContain('**bob:**')
    expect(result.output).toContain('Fixed in PR #45.')
  })

  it('reads an issue with no labels', async () => {
    const issue = { ...sampleIssue, labels: [] }
    platform.shell.setResult(
      'cd "/project" && gh issue view 10 --json title,body,comments,labels,state',
      { stdout: `${JSON.stringify(issue)}\n`, stderr: '', exitCode: 0 }
    )

    const result = await readIssueTool.execute({ number: 10 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).not.toContain('Labels:')
  })

  it('reads an issue with no comments', async () => {
    const issue = { ...sampleIssue, comments: [] }
    platform.shell.setResult(
      'cd "/project" && gh issue view 7 --json title,body,comments,labels,state',
      { stdout: `${JSON.stringify(issue)}\n`, stderr: '', exitCode: 0 }
    )

    const result = await readIssueTool.execute({ number: 7 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).not.toContain('## Comments')
  })

  it('reads an issue with no body', async () => {
    const issue = { ...sampleIssue, body: '' }
    platform.shell.setResult(
      'cd "/project" && gh issue view 3 --json title,body,comments,labels,state',
      { stdout: `${JSON.stringify(issue)}\n`, stderr: '', exitCode: 0 }
    )

    const result = await readIssueTool.execute({ number: 3 }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('(no body)')
  })

  it('reads an issue from a specific repo', async () => {
    platform.shell.setResult(
      'cd "/project" && gh issue view 1 --json title,body,comments,labels,state --repo owner/other-repo',
      { stdout: `${JSON.stringify(sampleIssue)}\n`, stderr: '', exitCode: 0 }
    )

    const result = await readIssueTool.execute({ number: 1, repo: 'owner/other-repo' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('Issue #1')
  })

  it('returns error for non-existent issue', async () => {
    platform.shell.setResult(
      'cd "/project" && gh issue view 999 --json title,body,comments,labels,state',
      { stdout: '', stderr: 'Could not resolve to an issue or pull request\n', exitCode: 1 }
    )

    const result = await readIssueTool.execute({ number: 999 }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('Could not resolve')
  })

  it('returns error on gh failure', async () => {
    platform.shell.setResult(
      'cd "/project" && gh issue view 5 --json title,body,comments,labels,state',
      { stdout: '', stderr: 'gh: not logged in\n', exitCode: 1 }
    )

    const result = await readIssueTool.execute({ number: 5 }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('not logged in')
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await readIssueTool.execute(
      { number: 1 },
      { sessionId: 'test', workingDirectory: '/project', signal: controller.signal }
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })
})
