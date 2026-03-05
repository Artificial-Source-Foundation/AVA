/**
 * Tests for per-tool-call checkpoint middleware.
 */

import type { ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'
import { describe, expect, it } from 'vitest'
import { MockShell } from '../../../core-v2/src/__test-utils__/mock-platform.js'
import { createCheckpointMiddleware } from './checkpoints.js'

function makeMiddlewareCtx(toolName: string, cwd = '/project'): ToolMiddlewareContext {
  return {
    toolName,
    args: {},
    ctx: {
      sessionId: 'test',
      workingDirectory: cwd,
      signal: AbortSignal.timeout(5000),
    },
    definition: {
      name: toolName,
      description: '',
      input_schema: { type: 'object', properties: {} },
    },
  }
}

function makeResult(success = true): ToolResult {
  return { success, output: 'ok' }
}

function mockGitRepo(shell: MockShell): void {
  shell.setResult('cd "/project" && git rev-parse --is-inside-work-tree', {
    stdout: 'true\n',
    stderr: '',
    exitCode: 0,
  })
}

function mockCommitTreeCheckpoint(
  shell: MockShell,
  {
    id,
    tool,
    commit,
    tree,
    parent,
    parentTree,
  }: {
    id: number
    tool: string
    commit: string
    tree: string
    parent: string
    parentTree: string
  }
): void {
  shell.setResult('cd "/project" && git add -A', {
    stdout: '',
    stderr: '',
    exitCode: 0,
  })
  shell.setResult('cd "/project" && git rev-parse --verify HEAD', {
    stdout: `${parent}\n`,
    stderr: '',
    exitCode: 0,
  })
  shell.setResult('cd "/project" && git write-tree', {
    stdout: `${tree}\n`,
    stderr: '',
    exitCode: 0,
  })
  shell.setResult(`cd "/project" && git rev-parse "${parent}^{tree}"`, {
    stdout: `${parentTree}\n`,
    stderr: '',
    exitCode: 0,
  })

  if (tree === parentTree) {
    return
  }

  shell.setResult(
    `cd "/project" && git commit-tree "${tree}" -p "${parent}" -m "ava-checkpoint-${id}-${tool}"`,
    {
      stdout: `${commit}\n`,
      stderr: '',
      exitCode: 0,
    }
  )
  shell.setResult(`cd "/project" && git update-ref "refs/ava/checkpoints/${commit}" "${commit}"`, {
    stdout: '',
    stderr: '',
    exitCode: 0,
  })
}

describe('createCheckpointMiddleware', () => {
  it('creates a checkpoint after a modifying tool', async () => {
    const shell = new MockShell()
    mockGitRepo(shell)
    mockCommitTreeCheckpoint(shell, {
      id: 1,
      tool: 'write_file',
      commit: 'abc123',
      tree: 'tree1',
      parent: 'head1',
      parentTree: 'head-tree',
    })

    const { middleware, store } = createCheckpointMiddleware(shell)
    const ctx = makeMiddlewareCtx('write_file')
    const result = await middleware.after!(ctx, makeResult())

    expect(result).toBeUndefined()
    expect(store.getCheckpoints()).toHaveLength(1)
    expect(store.getCheckpoints()[0]!.toolName).toBe('write_file')
    expect(store.getCheckpoints()[0]!.commit).toBe('abc123')
    expect(store.getCheckpoints()[0]!.id).toBe(1)
  })

  it('skips non-modifying tools', async () => {
    const shell = new MockShell()
    const { middleware, store } = createCheckpointMiddleware(shell)

    const ctx = makeMiddlewareCtx('read_file')
    const result = await middleware.after!(ctx, makeResult())

    expect(result).toBeUndefined()
    expect(store.getCheckpoints()).toHaveLength(0)
  })

  it('skips failed tool results', async () => {
    const shell = new MockShell()
    const { middleware, store } = createCheckpointMiddleware(shell)

    const ctx = makeMiddlewareCtx('write_file')
    const result = await middleware.after!(ctx, makeResult(false))

    expect(result).toBeUndefined()
    expect(store.getCheckpoints()).toHaveLength(0)
  })

  it('skips when not in a git repo', async () => {
    const shell = new MockShell()
    shell.setResult('cd "/project" && git rev-parse --is-inside-work-tree', {
      stdout: '',
      stderr: 'fatal: not a git repository',
      exitCode: 128,
    })

    const { middleware, store } = createCheckpointMiddleware(shell)
    const ctx = makeMiddlewareCtx('edit')
    const result = await middleware.after!(ctx, makeResult())

    expect(result).toBeUndefined()
    expect(store.getCheckpoints()).toHaveLength(0)
  })

  it('skips when tree matches HEAD tree (no changes)', async () => {
    const shell = new MockShell()
    mockGitRepo(shell)
    mockCommitTreeCheckpoint(shell, {
      id: 1,
      tool: 'bash',
      commit: 'unused',
      tree: 'same-tree',
      parent: 'head1',
      parentTree: 'same-tree',
    })

    const { middleware, store } = createCheckpointMiddleware(shell)
    const ctx = makeMiddlewareCtx('bash')
    const result = await middleware.after!(ctx, makeResult())

    expect(result).toBeUndefined()
    expect(store.getCheckpoints()).toHaveLength(0)
  })

  it('increments checkpoint IDs', async () => {
    const shell = new MockShell()
    mockGitRepo(shell)
    shell.setResult('cd "/project" && git add -A', { stdout: '', stderr: '', exitCode: 0 })
    shell.setResult('cd "/project" && git rev-parse --verify HEAD', {
      stdout: 'head1\n',
      stderr: '',
      exitCode: 0,
    })
    shell.setResult('cd "/project" && git write-tree', {
      stdout: 'tree1\n',
      stderr: '',
      exitCode: 0,
    })
    shell.setResult('cd "/project" && git rev-parse "head1^{tree}"', {
      stdout: 'head-tree\n',
      stderr: '',
      exitCode: 0,
    })
    shell.setResult(
      'cd "/project" && git commit-tree "tree1" -p "head1" -m "ava-checkpoint-1-edit"',
      {
        stdout: 'hash1\n',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult(
      'cd "/project" && git commit-tree "tree1" -p "head1" -m "ava-checkpoint-2-create_file"',
      {
        stdout: 'hash2\n',
        stderr: '',
        exitCode: 0,
      }
    )
    shell.setResult('cd "/project" && git update-ref "refs/ava/checkpoints/hash1" "hash1"', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })
    shell.setResult('cd "/project" && git update-ref "refs/ava/checkpoints/hash2" "hash2"', {
      stdout: '',
      stderr: '',
      exitCode: 0,
    })

    const { middleware, store } = createCheckpointMiddleware(shell)

    await middleware.after!(makeMiddlewareCtx('edit'), makeResult())
    await middleware.after!(makeMiddlewareCtx('create_file'), makeResult())

    const checkpoints = store.getCheckpoints()
    expect(checkpoints).toHaveLength(2)
    expect(checkpoints[0]!.id).toBe(1)
    expect(checkpoints[1]!.id).toBe(2)
  })

  it('handles all modifying tool types', async () => {
    const modifyingTools = [
      'write_file',
      'edit',
      'create_file',
      'delete_file',
      'bash',
      'multiedit',
      'apply_patch',
    ]

    for (let i = 0; i < modifyingTools.length; i++) {
      const shell = new MockShell()
      mockGitRepo(shell)
      mockCommitTreeCheckpoint(shell, {
        id: 1,
        tool: modifyingTools[i]!,
        commit: `hash-${i}`,
        tree: `tree-${i}`,
        parent: 'head1',
        parentTree: 'head-tree',
      })

      const { middleware, store } = createCheckpointMiddleware(shell)
      await middleware.after!(makeMiddlewareCtx(modifyingTools[i]!), makeResult())
      expect(store.getCheckpoints()).toHaveLength(1)
    }
  })

  it('resets checkpoints via store', async () => {
    const shell = new MockShell()
    mockGitRepo(shell)
    mockCommitTreeCheckpoint(shell, {
      id: 1,
      tool: 'edit',
      commit: 'hash1',
      tree: 'tree1',
      parent: 'head1',
      parentTree: 'head-tree',
    })

    const { middleware, store } = createCheckpointMiddleware(shell)
    await middleware.after!(makeMiddlewareCtx('edit'), makeResult())
    expect(store.getCheckpoints()).toHaveLength(1)

    store.reset()
    expect(store.getCheckpoints()).toHaveLength(0)
  })

  it('has correct middleware name and priority', () => {
    const shell = new MockShell()
    const { middleware } = createCheckpointMiddleware(shell)
    expect(middleware.name).toBe('ava-checkpoints')
    expect(middleware.priority).toBe(20)
  })

  it('handles git errors gracefully (non-critical)', async () => {
    const shell = new MockShell()
    mockGitRepo(shell)
    shell.setResult('cd "/project" && git add -A', {
      stdout: '',
      stderr: '',
      exitCode: 1,
    })

    const { middleware, store } = createCheckpointMiddleware(shell)
    const ctx = makeMiddlewareCtx('edit')
    // Should not throw
    const result = await middleware.after!(ctx, makeResult())
    expect(result).toBeUndefined()
    expect(store.getCheckpoints()).toHaveLength(0)
  })

  it('returns a copy of checkpoints (not the internal array)', async () => {
    const shell = new MockShell()
    mockGitRepo(shell)
    mockCommitTreeCheckpoint(shell, {
      id: 1,
      tool: 'edit',
      commit: 'hash1',
      tree: 'tree1',
      parent: 'head1',
      parentTree: 'head-tree',
    })

    const { middleware, store } = createCheckpointMiddleware(shell)
    await middleware.after!(makeMiddlewareCtx('edit'), makeResult())

    const copy1 = store.getCheckpoints()
    const copy2 = store.getCheckpoints()
    expect(copy1).not.toBe(copy2)
    expect(copy1).toEqual(copy2)
  })
})
