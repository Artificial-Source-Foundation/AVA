import { describe, expect, it } from 'vitest'
import { createInvokeTeamTool } from './invoke-team.js'

describe('invoke_team', () => {
  it('director can invoke tech-lead', async () => {
    const tool = createInvokeTeamTool('director')
    expect(tool.definition.name).toBe('invoke_team')
    expect(tool.definition.input_schema.required).toContain('role')
  })

  it('engineer cannot invoke team', async () => {
    const tool = createInvokeTeamTool('engineer')
    const result = await tool.execute(
      { role: 'engineer', task: 'do thing' },
      {
        sessionId: 's1',
        signal: new AbortController().signal,
        onEvent: () => undefined,
        delegationDepth: 0,
        workingDirectory: process.cwd(),
      }
    )
    expect(result.success).toBe(false)
    expect(result.output).toContain('cannot invoke team')
  })
})
