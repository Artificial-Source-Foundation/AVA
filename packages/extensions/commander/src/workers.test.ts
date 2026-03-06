import { describe, expect, it } from 'vitest'
import type { AgentRole } from './types.js'
import {
  BUILTIN_AGENTS,
  COMMANDER_AGENT,
  DIRECTOR_AGENT,
  ENGINEER_AGENT,
  LEAD_AGENTS,
  REVIEWER_AGENT,
  TECH_LEAD_AGENT,
  WORKER_AGENTS,
} from './workers.js'

describe('Praxis v2 workers', () => {
  it('includes director, tech-lead, engineer, reviewer roles', () => {
    const roles = new Set(BUILTIN_AGENTS.map((agent) => agent.tier))
    expect(roles.has('director')).toBe(true)
    expect(roles.has('tech-lead')).toBe(true)
    expect(roles.has('engineer')).toBe(true)
    expect(roles.has('reviewer')).toBe(true)
  })

  it('exports role union including reviewer', () => {
    const roles: AgentRole[] = ['director', 'tech-lead', 'engineer', 'reviewer', 'subagent']
    expect(roles).toHaveLength(5)
  })

  it('director prompt and tools are read-oriented with invoke capability', () => {
    expect(DIRECTOR_AGENT.systemPrompt).toContain('NEVER write code')
    expect(DIRECTOR_AGENT.tools).toContain('invoke_team')
    expect(DIRECTOR_AGENT.tools).toContain('read_file')
  })

  it('maintains compatibility aliases', () => {
    expect(COMMANDER_AGENT.id).toBe(DIRECTOR_AGENT.id)
    expect(LEAD_AGENTS[0]?.id).toBe(TECH_LEAD_AGENT.id)
    expect(WORKER_AGENTS.map((agent) => agent.id)).toEqual(
      expect.arrayContaining([ENGINEER_AGENT.id, REVIEWER_AGENT.id])
    )
  })
})
