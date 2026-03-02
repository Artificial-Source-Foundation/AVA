/**
 * Tests for create_branch and switch_branch tools.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetLogger } from '@ava/core-v2/logger'
import { afterEach, beforeEach, describe, expect, it } from 'vitest'
import { createBranchTool, switchBranchTool } from './branch.js'

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

describe('createBranchTool', () => {
  it('has correct name', () => {
    expect(createBranchTool.definition.name).toBe('create_branch')
  })

  it('creates a new branch', async () => {
    platform.shell.setResult('cd "/project" && git checkout -b feature/new-feature', {
      stdout: '',
      stderr: "Switched to a new branch 'feature/new-feature'\n",
      exitCode: 0,
    })

    const result = await createBranchTool.execute({ name: 'feature/new-feature' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('feature/new-feature')
  })

  it('creates a branch from a specific base', async () => {
    platform.shell.setResult('cd "/project" && git checkout -b hotfix/urgent main', {
      stdout: '',
      stderr: "Switched to a new branch 'hotfix/urgent'\n",
      exitCode: 0,
    })

    const result = await createBranchTool.execute(
      { name: 'hotfix/urgent', from: 'main' },
      makeCtx()
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('hotfix/urgent')
  })

  it('rejects invalid branch names', async () => {
    const result = await createBranchTool.execute({ name: 'invalid branch name!' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('Invalid branch name')
  })

  it('allows valid branch name characters', async () => {
    platform.shell.setResult('cd "/project" && git checkout -b feat/my-branch_v2.0', {
      stdout: '',
      stderr: "Switched to a new branch 'feat/my-branch_v2.0'\n",
      exitCode: 0,
    })

    const result = await createBranchTool.execute({ name: 'feat/my-branch_v2.0' }, makeCtx())
    expect(result.success).toBe(true)
  })

  it('returns error on failure', async () => {
    platform.shell.setResult('cd "/project" && git checkout -b existing-branch', {
      stdout: '',
      stderr: "fatal: a branch named 'existing-branch' already exists\n",
      exitCode: 128,
    })

    const result = await createBranchTool.execute({ name: 'existing-branch' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('already exists')
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await createBranchTool.execute(
      { name: 'test' },
      { sessionId: 'test', workingDirectory: '/project', signal: controller.signal }
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })

  it('uses stderr output when stdout is empty', async () => {
    platform.shell.setResult('cd "/project" && git checkout -b my-branch', {
      stdout: '',
      stderr: "Switched to a new branch 'my-branch'\n",
      exitCode: 0,
    })

    const result = await createBranchTool.execute({ name: 'my-branch' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('my-branch')
  })
})

describe('switchBranchTool', () => {
  it('has correct name', () => {
    expect(switchBranchTool.definition.name).toBe('switch_branch')
  })

  it('switches to an existing branch', async () => {
    platform.shell.setResult('cd "/project" && git checkout main', {
      stdout: '',
      stderr: "Switched to branch 'main'\n",
      exitCode: 0,
    })

    const result = await switchBranchTool.execute({ name: 'main' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain('main')
  })

  it('returns error for non-existent branch', async () => {
    platform.shell.setResult('cd "/project" && git checkout nonexistent', {
      stdout: '',
      stderr: "error: pathspec 'nonexistent' did not match any file(s) known to git\n",
      exitCode: 1,
    })

    const result = await switchBranchTool.execute({ name: 'nonexistent' }, makeCtx())
    expect(result.success).toBe(false)
    expect(result.error).toContain('nonexistent')
  })

  it('returns error when aborted', async () => {
    const controller = new AbortController()
    controller.abort()
    const result = await switchBranchTool.execute(
      { name: 'main' },
      { sessionId: 'test', workingDirectory: '/project', signal: controller.signal }
    )
    expect(result.success).toBe(false)
    expect(result.error).toBe('Aborted')
  })

  it('provides fallback output when both stdout and stderr are empty', async () => {
    platform.shell.setResult('cd "/project" && git checkout develop', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })

    const result = await switchBranchTool.execute({ name: 'develop' }, makeCtx())
    expect(result.success).toBe(true)
    expect(result.output).toContain("Switched to branch 'develop'")
  })
})
