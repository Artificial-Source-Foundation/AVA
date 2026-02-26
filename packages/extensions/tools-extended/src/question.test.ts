/**
 * question tool — ask the user for clarification.
 */

import { describe, expect, it } from 'vitest'
import { questionTool } from './question.js'

const dummyCtx = {
  sessionId: 'test',
  workingDirectory: '/tmp',
  signal: AbortSignal.timeout(5000),
}

describe('questionTool', () => {
  it('has correct name', () => {
    expect(questionTool.definition.name).toBe('question')
  })

  it('formats single question output', async () => {
    const result = await questionTool.execute(
      { questions: [{ text: 'What framework?' }] },
      dummyCtx
    )
    expect(result.success).toBe(true)
    expect(result.output).toContain('Question 1: What framework?')
  })

  it('formats multiple questions', async () => {
    const result = await questionTool.execute(
      {
        questions: [{ text: 'Question A?' }, { text: 'Question B?' }],
      },
      dummyCtx
    )
    expect(result.output).toContain('Question 1: Question A?')
    expect(result.output).toContain('Question 2: Question B?')
  })

  it('includes options when provided', async () => {
    const result = await questionTool.execute(
      {
        questions: [{ text: 'Pick one?', options: ['React', 'Vue', 'Solid'] }],
      },
      dummyCtx
    )
    expect(result.output).toContain('Options: React, Vue, Solid')
  })

  it('includes requiresUserResponse metadata', async () => {
    const result = await questionTool.execute({ questions: [{ text: 'Hello?' }] }, dummyCtx)
    expect(result.metadata?.requiresUserResponse).toBe(true)
  })
})
