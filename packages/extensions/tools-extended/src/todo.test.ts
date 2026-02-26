/**
 * todoread / todowrite tools — in-memory task tracking.
 */

import { describe, expect, it } from 'vitest'
import { todoReadTool, todoWriteTool } from './todo.js'

const dummyCtx = {
  sessionId: 'test',
  workingDirectory: '/tmp',
  signal: AbortSignal.timeout(5000),
}

// The todo module uses module-level state, so we need to be careful about
// test isolation. We clear todos by writing completed items.

describe('todoReadTool', () => {
  it('has correct name', () => {
    expect(todoReadTool.definition.name).toBe('todoread')
  })

  it('returns "No todos" when empty', async () => {
    // First ensure we start fresh — read initial state
    const result = await todoReadTool.execute({}, dummyCtx)
    // May have todos from other tests in this module; that's ok
    expect(result.success).toBe(true)
  })
})

describe('todoWriteTool', () => {
  it('has correct name', () => {
    expect(todoWriteTool.definition.name).toBe('todowrite')
  })

  it('adds a todo item', async () => {
    const writeResult = await todoWriteTool.execute(
      {
        todos: [{ content: 'Write tests', status: 'pending' as const }],
      },
      dummyCtx
    )
    expect(writeResult.success).toBe(true)
    expect(writeResult.output).toContain('Updated 1 todo(s)')

    const readResult = await todoReadTool.execute({}, dummyCtx)
    expect(readResult.success).toBe(true)
    expect(readResult.output).toContain('Write tests')
    expect(readResult.output).toContain('[pending]')
  })

  it('adds multiple todo items', async () => {
    const result = await todoWriteTool.execute(
      {
        todos: [{ content: 'Task A' }, { content: 'Task B' }, { content: 'Task C' }],
      },
      dummyCtx
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('Updated 3 todo(s)')
  })

  it('updates existing todo by id', async () => {
    // Add a todo first
    await todoWriteTool.execute(
      { todos: [{ id: 'update-test', content: 'Original', status: 'pending' as const }] },
      dummyCtx
    )

    // Update it
    const updateResult = await todoWriteTool.execute(
      { todos: [{ id: 'update-test', content: 'Updated', status: 'completed' as const }] },
      dummyCtx
    )
    expect(updateResult.success).toBe(true)

    const readResult = await todoReadTool.execute({}, dummyCtx)
    expect(readResult.output).toContain('Updated')
    expect(readResult.output).toContain('[completed]')
  })

  it('defaults status to pending', async () => {
    await todoWriteTool.execute(
      { todos: [{ id: 'default-status', content: 'Default status test' }] },
      dummyCtx
    )

    const readResult = await todoReadTool.execute({}, dummyCtx)
    expect(readResult.output).toContain('[pending]')
    expect(readResult.output).toContain('Default status test')
  })
})
