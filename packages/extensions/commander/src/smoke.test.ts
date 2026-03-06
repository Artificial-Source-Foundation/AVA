import { describe, expect, it } from 'vitest'
import { createInvokeSubagentTool } from './invoke-subagent.js'
import { createInvokeTeamTool } from './invoke-team.js'
import { BUILTIN_AGENTS } from './workers.js'

describe('Commander Praxis v2 smoke test', () => {
  it('has 4 built-in hierarchy agents', () => {
    expect(BUILTIN_AGENTS).toHaveLength(4)
    expect(BUILTIN_AGENTS.map((agent) => agent.id)).toEqual([
      'director',
      'tech-lead',
      'engineer',
      'reviewer',
    ])
  })

  it('registers invoke_team and invoke_subagent tools', () => {
    expect(createInvokeTeamTool().definition.name).toBe('invoke_team')
    expect(createInvokeSubagentTool().definition.name).toBe('invoke_subagent')
  })
})
