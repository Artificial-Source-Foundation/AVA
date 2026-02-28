import { afterEach, describe, expect, it } from 'vitest'
import type { AgentDefinition } from './agent-definition.js'
import {
  clearRegistry,
  getAgent,
  getAgentsByTier,
  getAllAgents,
  hasAgent,
  registerAgent,
  registerAgents,
} from './registry.js'

function makeAgent(overrides: Partial<AgentDefinition> = {}): AgentDefinition {
  return {
    id: 'test-agent',
    name: 'test-agent',
    displayName: 'Test Agent',
    description: 'A test agent',
    tier: 'worker',
    systemPrompt: 'You are a test agent',
    tools: ['read_file'],
    isBuiltIn: true,
    ...overrides,
  }
}

describe('Agent Registry', () => {
  afterEach(() => clearRegistry())

  it('registers and retrieves an agent', () => {
    const agent = makeAgent()
    registerAgent(agent)

    expect(getAgent('test-agent')).toEqual(agent)
    expect(hasAgent('test-agent')).toBe(true)
  })

  it('returns undefined for unknown agent', () => {
    expect(getAgent('nonexistent')).toBeUndefined()
    expect(hasAgent('nonexistent')).toBe(false)
  })

  it('dispose removes the agent', () => {
    const agent = makeAgent()
    const disposable = registerAgent(agent)

    expect(hasAgent('test-agent')).toBe(true)
    disposable.dispose()
    expect(hasAgent('test-agent')).toBe(false)
  })

  it('getAgentsByTier filters correctly', () => {
    registerAgent(makeAgent({ id: 'w1', tier: 'worker' }))
    registerAgent(makeAgent({ id: 'l1', tier: 'lead' }))
    registerAgent(makeAgent({ id: 'c1', tier: 'commander' }))
    registerAgent(makeAgent({ id: 'w2', tier: 'worker' }))

    expect(getAgentsByTier('worker')).toHaveLength(2)
    expect(getAgentsByTier('lead')).toHaveLength(1)
    expect(getAgentsByTier('commander')).toHaveLength(1)
  })

  it('getAllAgents returns all registered agents', () => {
    registerAgent(makeAgent({ id: 'a' }))
    registerAgent(makeAgent({ id: 'b' }))
    registerAgent(makeAgent({ id: 'c' }))

    expect(getAllAgents()).toHaveLength(3)
  })

  it('clearRegistry removes everything', () => {
    registerAgent(makeAgent({ id: 'a' }))
    registerAgent(makeAgent({ id: 'b' }))

    clearRegistry()
    expect(getAllAgents()).toHaveLength(0)
  })

  it('registerAgents registers multiple and dispose cleans all', () => {
    const agents = [makeAgent({ id: 'a' }), makeAgent({ id: 'b' }), makeAgent({ id: 'c' })]
    const disposable = registerAgents(agents)

    expect(getAllAgents()).toHaveLength(3)
    disposable.dispose()
    expect(getAllAgents()).toHaveLength(0)
  })

  it('overwriting an agent updates the registry', () => {
    registerAgent(makeAgent({ id: 'x', displayName: 'V1' }))
    registerAgent(makeAgent({ id: 'x', displayName: 'V2' }))

    expect(getAgent('x')?.displayName).toBe('V2')
    expect(getAllAgents()).toHaveLength(1)
  })
})
