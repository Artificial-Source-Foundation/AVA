/**
 * Git tools smoke test — all 4 git tools.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import type { ToolContext } from '@ava/core-v2/tools'
import { beforeEach, describe, expect, it } from 'vitest'
import { createBranchTool, switchBranchTool } from './branch.js'
import { readIssueTool } from './issue.js'
import { createPrTool } from './pr.js'

const TOOLS = [createPrTool, createBranchTool, switchBranchTool, readIssueTool]

function createCtx(overrides?: Partial<ToolContext>): ToolContext {
  return {
    sessionId: 'smoke',
    workingDirectory: '/project',
    signal: new AbortController().signal,
    ...overrides,
  }
}

describe('Git tools smoke test', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
  })

  it('has 4 git tools', () => {
    expect(TOOLS).toHaveLength(4)
  })

  describe('tool definitions', () => {
    it.each(TOOLS.map((t) => [t.definition.name, t]))('%s has valid definition', (_name, tool) => {
      expect(tool.definition.name).toBeTruthy()
      expect(tool.definition.description).toBeTruthy()
      expect(tool.definition.input_schema).toBeTruthy()
    })
  })

  describe('create_pr', () => {
    it('calls gh pr create', async () => {
      platform.shell.setResult('cd "/project" && gh pr create --title \'Test PR\'', {
        stdout: 'https://github.com/org/repo/pull/1',
        stderr: '',
        exitCode: 0,
      })
      const result = await createPrTool.execute({ title: 'Test PR' }, createCtx())
      expect(result.success).toBe(true)
      expect(result.output).toContain('github.com')
    })

    it('handles gh not found', async () => {
      platform.shell.defaultResult = { stdout: '', stderr: 'gh not found', exitCode: 1 }
      const result = await createPrTool.execute({ title: 'Test' }, createCtx())
      expect(result.success).toBe(false)
    })

    it('respects abort', async () => {
      const controller = new AbortController()
      controller.abort()
      const result = await createPrTool.execute(
        { title: 'Test' },
        createCtx({ signal: controller.signal })
      )
      expect(result.success).toBe(false)
    })
  })

  describe('create_branch', () => {
    it('creates and switches to new branch', async () => {
      platform.shell.setResult('cd "/project" && git checkout -b feature/test', {
        stdout: '',
        stderr: "Switched to a new branch 'feature/test'",
        exitCode: 0,
      })
      const result = await createBranchTool.execute({ name: 'feature/test' }, createCtx())
      expect(result.success).toBe(true)
    })

    it('rejects invalid branch names', async () => {
      const result = await createBranchTool.execute(
        { name: 'invalid branch name with spaces' },
        createCtx()
      )
      expect(result.success).toBe(false)
    })
  })

  describe('switch_branch', () => {
    it('switches to existing branch', async () => {
      platform.shell.setResult('cd "/project" && git checkout main', {
        stdout: '',
        stderr: "Switched to branch 'main'",
        exitCode: 0,
      })
      const result = await switchBranchTool.execute({ name: 'main' }, createCtx())
      expect(result.success).toBe(true)
    })
  })

  describe('read_issue', () => {
    it('reads a GitHub issue', async () => {
      const issueJson = JSON.stringify({
        title: 'Bug report',
        body: 'Something is broken',
        state: 'OPEN',
        labels: [{ name: 'bug' }],
        comments: [],
      })
      platform.shell.setResult(
        'cd "/project" && gh issue view 42 --json title,body,comments,labels,state',
        { stdout: issueJson, stderr: '', exitCode: 0 }
      )
      const result = await readIssueTool.execute({ number: 42 }, createCtx())
      expect(result.success).toBe(true)
      expect(result.output).toContain('Bug report')
      expect(result.output).toContain('bug')
    })
  })
})
