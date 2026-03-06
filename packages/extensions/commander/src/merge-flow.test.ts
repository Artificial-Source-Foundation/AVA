import { describe, expect, it, vi } from 'vitest'
import { techLeadMerge } from './merge-flow.js'

describe('techLeadMerge', () => {
  it('merges approved engineer branches', async () => {
    const runCommand = vi.fn().mockResolvedValue({ success: true, output: 'ok' })
    const result = await techLeadMerge(
      'lead-1',
      [
        {
          agentId: 'eng-1',
          success: true,
          summary: 'done',
          reviewAttempts: 1,
          approved: true,
          warnings: [],
          worktreeBranch: 'ava/engineer/eng-1',
        },
      ],
      {
        sessionId: 's1',
        signal: new AbortController().signal,
        onEvent: vi.fn(),
        delegationDepth: 0,
        workingDirectory: process.cwd(),
      },
      { runCommand }
    )
    expect(result.success).toBe(true)
    expect(result.mergedBranches).toContain('ava/engineer/eng-1')
  })

  it('redelegates non-approved results', async () => {
    const result = await techLeadMerge(
      'lead-1',
      [
        {
          agentId: 'eng-2',
          success: false,
          summary: 'failed',
          reviewAttempts: 3,
          approved: false,
          warnings: ['bad'],
          worktreeBranch: 'ava/engineer/eng-2',
        },
      ],
      {
        sessionId: 's1',
        signal: new AbortController().signal,
        onEvent: vi.fn(),
        delegationDepth: 0,
        workingDirectory: process.cwd(),
      }
    )
    expect(result.redelegated).toContain('eng-2')
  })
})
