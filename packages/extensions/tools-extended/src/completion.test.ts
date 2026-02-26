/**
 * attempt_completion tool — signals task completion.
 */

import { describe, expect, it } from 'vitest'
import { completionTool } from './completion.js'

const dummyCtx = {
  sessionId: 'test',
  workingDirectory: '/tmp',
  signal: AbortSignal.timeout(5000),
}

describe('completionTool', () => {
  it('has correct name', () => {
    expect(completionTool.definition.name).toBe('attempt_completion')
  })

  it('returns result as output', async () => {
    const result = await completionTool.execute(
      { result: 'All tasks completed successfully' },
      dummyCtx
    )
    expect(result.success).toBe(true)
    expect(result.output).toBe('All tasks completed successfully')
  })

  it('includes completed metadata', async () => {
    const result = await completionTool.execute({ result: 'Done' }, dummyCtx)
    expect(result.metadata?.completed).toBe(true)
  })

  it('includes optional command in metadata', async () => {
    const result = await completionTool.execute({ result: 'Done', command: 'npm test' }, dummyCtx)
    expect(result.metadata?.command).toBe('npm test')
  })
})
