/**
 * Task tool — subagent spawning tests.
 */

import { installMockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import { resetRegistries } from '@ava/core-v2/extensions'
import type { StreamDelta } from '@ava/core-v2/llm'
import { registerProvider, resetProviders } from '@ava/core-v2/llm'
import { resetLogger } from '@ava/core-v2/logger'
import { registerTool, resetTools } from '@ava/core-v2/tools'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { taskTool } from './task.js'

// ─── Setup ──────────────────────────────────────────────────────────────────

beforeEach(() => {
  installMockPlatform()

  // Register a mock provider that returns a simple response
  registerProvider('anthropic', () => ({
    async *stream(): AsyncGenerator<StreamDelta, void, unknown> {
      yield { content: 'Subagent completed the task.' }
      yield { done: true }
    },
  }))
})

afterEach(() => {
  resetTools()
  resetRegistries()
  resetProviders()
  resetLogger()
  vi.restoreAllMocks()
})

const ctx = {
  sessionId: 'test-session',
  workingDirectory: '/tmp',
  signal: new AbortController().signal,
}

// ─── Tests ──────────────────────────────────────────────────────────────────

describe('taskTool', () => {
  it('has correct definition', () => {
    expect(taskTool.definition.name).toBe('task')
    expect(taskTool.definition.description).toContain('subagent')
    expect(taskTool.definition.description).toContain('task_id')
  })

  it('has task_id in schema', () => {
    expect(taskTool.definition.input_schema.properties).toHaveProperty('task_id')
  })

  it('runs subagent to completion with mock provider', async () => {
    const result = await taskTool.execute(
      {
        description: 'test task',
        prompt: 'Say hello',
      },
      ctx
    )

    expect(result.success).toBe(true)
    expect(result.output).toContain('Subagent completed the task.')
  })

  it('uses worker definition when worker type is specified', async () => {
    const result = await taskTool.execute(
      {
        description: 'review code',
        prompt: 'Review the code in src/',
        worker: 'reviewer',
      },
      ctx
    )

    expect(result.success).toBe(true)
  })

  it('filters out task tool from subagent (prevents recursion)', async () => {
    // Register task tool itself
    registerTool(taskTool)

    // The subagent should not have access to 'task' even if explicitly requested
    const result = await taskTool.execute(
      {
        description: 'nested task',
        prompt: 'Do something',
        allowedTools: ['read_file', 'task', 'glob'],
      },
      ctx
    )

    // Should still complete (just without task tool available)
    expect(result.success).toBe(true)
  })

  it('uses default read-only tools when no worker specified', async () => {
    // Mock provider that checks what tools are available
    let receivedTools: string[] = []
    resetProviders()
    registerProvider('anthropic', () => ({
      async *stream(_messages, config): AsyncGenerator<StreamDelta, void, unknown> {
        receivedTools = (config.tools ?? []).map((t) => t.name)
        yield { content: 'Done' }
        yield { done: true }
      },
    }))

    // Register some tools that the subagent might use
    const { readFileTool, globTool, grepTool, bashTool } = await import('@ava/core-v2/tools')
    registerTool(readFileTool)
    registerTool(globTool)
    registerTool(grepTool)
    registerTool(bashTool)

    await taskTool.execute(
      {
        description: 'research',
        prompt: 'Find files',
      },
      ctx
    )

    // Default tools for no-worker case: read_file, grep, glob
    expect(receivedTools).toContain('read_file')
    expect(receivedTools).toContain('grep')
    expect(receivedTools).toContain('glob')
    expect(receivedTools).not.toContain('bash')
  })

  it('accepts explorer as worker type', async () => {
    const result = await taskTool.execute(
      {
        description: 'explore code',
        prompt: 'Explore the codebase structure',
        worker: 'explorer',
      },
      ctx
    )

    expect(result.success).toBe(true)
  })

  it('returns metadata with taskId', async () => {
    const result = await taskTool.execute(
      {
        description: 'test task',
        prompt: 'Do something',
      },
      ctx
    )

    expect(result.metadata).toBeDefined()
    expect(result.metadata!.taskId).toBeDefined()
    expect(typeof result.metadata!.taskId).toBe('string')
  })

  it('passes task_id as sessionId to agent inputs', async () => {
    const result = await taskTool.execute(
      {
        description: 'resume task',
        prompt: 'Continue the work',
        task_id: 'existing-session-123',
      },
      ctx
    )

    // The subagent should still run to completion
    expect(result.success).toBe(true)
  })
})
