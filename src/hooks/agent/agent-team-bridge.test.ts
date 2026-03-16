import { describe, expect, it, vi } from 'vitest'
import type { AgentEvent } from './agent-events'
import { createTeamBridge } from './agent-team-bridge'

vi.mock('../../services/logger', () => ({
  logDebug: vi.fn(),
  logInfo: vi.fn(),
  logWarn: vi.fn(),
  logError: vi.fn(),
}))

function createMockTeamStore() {
  const members = new Map<string, Record<string, unknown>>()

  return {
    teamMembers: () => members,
    teamLead: () =>
      members.size > 0 ? ([...members.values()].find((m) => m.role === 'team-lead') ?? null) : null,
    seniorLeads: () => [...members.values()].filter((m) => m.role === 'senior-lead'),
    agentTypeToRole: (type: string) => {
      if (type === 'commander') return 'team-lead'
      if (type === 'operator') return 'junior-dev'
      return 'senior-lead'
    },
    inferDomain: (_task?: string) => 'general' as const,
    generateName: (role: string, _domain: string) => {
      if (role === 'team-lead') return 'Team Lead'
      return 'Senior General Lead'
    },
    addMember: vi.fn((member: Record<string, unknown>) => {
      members.set(member.id as string, member)
    }),
    updateMember: vi.fn((id: string, updates: Record<string, unknown>) => {
      const existing = members.get(id)
      if (existing) members.set(id, { ...existing, ...updates })
    }),
    updateMemberStatus: vi.fn((id: string, status: string) => {
      const existing = members.get(id)
      if (existing) members.set(id, { ...existing, status })
    }),
    addToolCall: vi.fn(),
    updateToolCall: vi.fn(),
    addMessage: vi.fn(),
    updateMessage: vi.fn(),
  }
}

describe('Team Bridge', () => {
  describe('solo mode guard', () => {
    it('ignores events when isTeamMode returns false', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => false)

      bridgeToTeam({
        type: 'agent:start',
        agentId: 'agent-1',
        goal: 'Build a feature',
      })

      expect(store.addMember).not.toHaveBeenCalled()
    })
  })

  describe('agent:start', () => {
    it('creates Team Lead on first agent:start', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'agent:start',
        agentId: 'agent-1',
        goal: 'Build a feature',
      })

      expect(store.addMember).toHaveBeenCalledWith(
        expect.objectContaining({
          id: 'agent-1',
          role: 'team-lead',
          status: 'working',
        })
      )
    })

    it('skips agent:start if member already exists from delegation:start', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      // First, simulate delegation:start creating the member
      bridgeToTeam({
        type: 'delegation:start',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        workerName: 'coder',
        task: 'Write code',
      } as AgentEvent)

      store.addMember.mockClear()

      // Now agent:start should be a no-op for child-1
      bridgeToTeam({
        type: 'agent:start',
        agentId: 'child-1',
        goal: 'Write code',
      })

      expect(store.addMember).not.toHaveBeenCalled()
    })
  })

  describe('delegation:start', () => {
    it('creates Senior Lead member with correct parentId', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'delegation:start',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        workerName: 'coder',
        task: 'Implement feature',
      } as AgentEvent)

      expect(store.addMember).toHaveBeenCalledWith(
        expect.objectContaining({
          id: 'child-1',
          name: 'Coder',
          role: 'senior-lead',
          status: 'working',
          parentId: 'parent-1',
          task: 'Implement feature',
        })
      )
    })

    it('capitalizes worker name', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'delegation:start',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        workerName: 'tester',
        task: 'Write tests',
      } as AgentEvent)

      expect(store.addMember).toHaveBeenCalledWith(
        expect.objectContaining({
          name: 'Tester',
        })
      )
    })

    it('includes delegation context from last thought', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      // First emit a thought from the parent
      bridgeToTeam({
        type: 'thought',
        agentId: 'parent-1',
        content: 'I will delegate the coding task',
      })

      // Then delegate
      bridgeToTeam({
        type: 'delegation:start',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        workerName: 'coder',
        task: 'Write code',
      } as AgentEvent)

      expect(store.addMember).toHaveBeenCalledWith(
        expect.objectContaining({
          delegationContext: 'I will delegate the coding task',
        })
      )
    })
  })

  describe('delegation:complete', () => {
    it('updates member status to done on success', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      // Create the member first
      bridgeToTeam({
        type: 'delegation:start',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        workerName: 'coder',
        task: 'Write code',
      } as AgentEvent)

      bridgeToTeam({
        type: 'delegation:complete',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        success: true,
        output: 'Code written successfully',
      } as AgentEvent)

      expect(store.updateMemberStatus).toHaveBeenCalledWith('child-1', 'done')
      expect(store.updateMember).toHaveBeenCalledWith('child-1', {
        result: 'Code written successfully',
        completedAt: expect.any(Number),
      })
    })

    it('updates member status to error on failure', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'delegation:start',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        workerName: 'coder',
        task: 'Write code',
      } as AgentEvent)

      bridgeToTeam({
        type: 'delegation:complete',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        success: false,
        output: 'Failed to compile',
      } as AgentEvent)

      expect(store.updateMemberStatus).toHaveBeenCalledWith('child-1', 'error')
    })
  })

  describe('child tool events', () => {
    it('tracks tool:start events under child member', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'delegation:start',
        agentId: 'parent-1',
        childAgentId: 'child-1',
        workerName: 'coder',
        task: 'Write code',
      } as AgentEvent)

      bridgeToTeam({
        type: 'tool:start',
        agentId: 'child-1',
        toolName: 'read_file',
        args: { path: 'src/index.ts' },
      })

      expect(store.addToolCall).toHaveBeenCalledWith(
        'child-1',
        expect.objectContaining({
          name: 'read_file',
          status: 'running',
        })
      )
    })

    it('tracks tool:finish events under child member', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      // Start tool
      bridgeToTeam({
        type: 'tool:start',
        agentId: 'child-1',
        toolName: 'read_file',
        args: {},
      })

      // Finish tool
      bridgeToTeam({
        type: 'tool:finish',
        agentId: 'child-1',
        toolName: 'read_file',
        success: true,
        durationMs: 42,
      })

      expect(store.updateToolCall).toHaveBeenCalledWith(
        'child-1',
        expect.any(String),
        expect.objectContaining({
          status: 'success',
          durationMs: 42,
        })
      )
    })

    it('captures tool args on tool:start', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'tool:start',
        agentId: 'child-1',
        toolName: 'read_file',
        args: { path: 'src/index.ts' },
      })

      expect(store.addToolCall).toHaveBeenCalledWith(
        'child-1',
        expect.objectContaining({
          name: 'read_file',
          args: { path: 'src/index.ts' },
          startedAt: expect.any(Number),
        })
      )
    })

    it('captures tool output and completedAt on tool:finish', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'tool:start',
        agentId: 'child-1',
        toolName: 'read_file',
        args: {},
      })

      bridgeToTeam({
        type: 'tool:finish',
        agentId: 'child-1',
        toolName: 'read_file',
        success: true,
        durationMs: 50,
        output: 'file contents here',
      } as AgentEvent)

      expect(store.updateToolCall).toHaveBeenCalledWith(
        'child-1',
        expect.any(String),
        expect.objectContaining({
          status: 'success',
          durationMs: 50,
          output: 'file contents here',
          completedAt: expect.any(Number),
        })
      )
    })
  })

  describe('thought accumulation', () => {
    it('creates new message for first thought', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'thought',
        agentId: 'agent-1',
        content: 'Let me think...',
      })

      expect(store.addMessage).toHaveBeenCalledTimes(1)
      expect(store.addMessage).toHaveBeenCalledWith(
        'agent-1',
        expect.objectContaining({
          role: 'assistant',
          content: 'Let me think...',
        })
      )
    })

    it('accumulates subsequent thoughts into existing message', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      bridgeToTeam({
        type: 'thought',
        agentId: 'agent-1',
        content: 'First ',
      })

      bridgeToTeam({
        type: 'thought',
        agentId: 'agent-1',
        content: 'second ',
      })

      bridgeToTeam({
        type: 'thought',
        agentId: 'agent-1',
        content: 'third',
      })

      // Only one addMessage call (first thought)
      expect(store.addMessage).toHaveBeenCalledTimes(1)
      // Two updateMessage calls (second + third thoughts)
      expect(store.updateMessage).toHaveBeenCalledTimes(2)
      expect(store.updateMessage).toHaveBeenLastCalledWith(
        'agent-1',
        expect.any(String),
        'First second third'
      )
    })

    it('resets thought accumulation on tool:start', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never, () => true)

      // First thinking block
      bridgeToTeam({ type: 'thought', agentId: 'agent-1', content: 'block one' })
      expect(store.addMessage).toHaveBeenCalledTimes(1)

      // Tool interrupts
      bridgeToTeam({
        type: 'tool:start',
        agentId: 'agent-1',
        toolName: 'read_file',
        args: {},
      })

      // New thinking block after tool
      bridgeToTeam({ type: 'thought', agentId: 'agent-1', content: 'block two' })
      expect(store.addMessage).toHaveBeenCalledTimes(2)
    })
  })
})
