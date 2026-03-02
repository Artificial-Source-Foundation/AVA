/**
 * Extended tools smoke test — verifies all 20 extended tools load and have valid definitions.
 */

import { installMockPlatform, type MockPlatform } from '@ava/core-v2/__test-utils__/mock-platform'
import type { ToolContext } from '@ava/core-v2/tools'
import { beforeEach, describe, expect, it } from 'vitest'
import { applyPatchTool } from './apply-patch/index.js'
import { bashBackgroundTool } from './bash-background.js'
import { bashKillTool } from './bash-kill.js'
import { bashOutputTool } from './bash-output.js'
import { batchTool } from './batch.js'
import { codesearchTool } from './codesearch.js'
import { completionTool } from './completion.js'
import { createFileTool } from './create.js'
import { deleteFileTool } from './delete.js'
import { lsTool } from './ls.js'
import { multieditTool } from './multiedit.js'
import { planEnterTool, planExitTool } from './plan-mode-tools.js'
import { questionTool } from './question.js'
import { repoMapTool } from './repo-map.js'
import { taskTool } from './task.js'
import { todoReadTool, todoWriteTool } from './todo.js'
import { webfetchTool } from './webfetch.js'
import { websearchTool } from './websearch.js'

const ALL_TOOLS = [
  createFileTool,
  deleteFileTool,
  lsTool,
  completionTool,
  todoReadTool,
  todoWriteTool,
  batchTool,
  questionTool,
  multieditTool,
  taskTool,
  websearchTool,
  webfetchTool,
  applyPatchTool,
  codesearchTool,
  repoMapTool,
  planEnterTool,
  planExitTool,
  bashBackgroundTool,
  bashOutputTool,
  bashKillTool,
]

function createCtx(overrides?: Partial<ToolContext>): ToolContext {
  return {
    sessionId: 'smoke-test',
    workingDirectory: '/project',
    signal: new AbortController().signal,
    ...overrides,
  }
}

describe('Extended tools smoke test', () => {
  let platform: MockPlatform

  beforeEach(() => {
    platform = installMockPlatform()
    platform.fs.addDir('/project')
    platform.fs.addDir('/project/src')
    platform.fs.addFile('/project/src/app.ts', 'const app = true\n')
  })

  it('has 20 tools', () => {
    expect(ALL_TOOLS).toHaveLength(20)
  })

  describe('tool definitions', () => {
    it.each(
      ALL_TOOLS.map((t) => [t.definition.name, t])
    )('%s has valid definition', (_name, tool) => {
      expect(tool.definition.name).toBeTruthy()
      expect(tool.definition.description).toBeTruthy()
      expect(tool.definition.input_schema).toBeTruthy()
      expect(tool.definition.input_schema.type).toBe('object')
    })
  })

  describe('basic execution', () => {
    it('create_file creates a new file', async () => {
      const result = await createFileTool.execute(
        { path: '/project/src/new.ts', content: 'new file' },
        createCtx()
      )
      expect(result.success).toBe(true)
    })

    it('create_file fails on existing file', async () => {
      const result = await createFileTool.execute(
        { path: '/project/src/app.ts', content: 'overwrite' },
        createCtx()
      )
      expect(result.success).toBe(false)
    })

    it('delete_file removes a file', async () => {
      const result = await deleteFileTool.execute({ path: '/project/src/app.ts' }, createCtx())
      expect(result.success).toBe(true)
    })

    it('ls lists directory contents', async () => {
      const result = await lsTool.execute({ path: '/project/src' }, createCtx())
      expect(result.success).toBe(true)
      expect(result.output).toContain('app.ts')
    })

    it('todoread returns empty for new session', async () => {
      const result = await todoReadTool.execute({}, createCtx())
      expect(result.success).toBe(true)
    })

    it('todowrite updates the todo list', async () => {
      const result = await todoWriteTool.execute(
        { todos: [{ id: '1', task: 'test', status: 'pending' }] },
        createCtx()
      )
      expect(result.success).toBe(true)
    })

    it('completionTool returns success', async () => {
      const result = await completionTool.execute(
        { result: 'Task completed successfully' },
        createCtx()
      )
      expect(result.success).toBe(true)
    })

    it('questionTool returns the question', async () => {
      const result = await questionTool.execute(
        { questions: [{ text: 'Should I proceed?', options: ['yes', 'no'] }] },
        createCtx()
      )
      expect(result.success).toBe(true)
      expect(result.output).toContain('Should I proceed')
    })

    it('planEnterTool enters plan mode', async () => {
      const result = await planEnterTool.execute({}, createCtx())
      expect(result.success).toBe(true)
    })

    it('planExitTool exits plan mode', async () => {
      const result = await planExitTool.execute({}, createCtx())
      expect(result.success).toBe(true)
    })
  })

  describe('abort signal handling', () => {
    it('create_file respects abort', async () => {
      const controller = new AbortController()
      controller.abort()
      const result = await createFileTool.execute(
        { path: '/project/x.ts', content: 'x' },
        createCtx({ signal: controller.signal })
      )
      expect(result.success).toBe(false)
    })
  })
})
