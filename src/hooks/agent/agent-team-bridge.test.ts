import type { AgentEvent } from '@ava/core-v2/agent'
import { describe, expect, it, vi } from 'vitest'
import { createTeamBridge } from './agent-team-bridge'

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
  }
}

describe('Team Bridge', () => {
  describe('agent:start', () => {
    it('creates Team Lead on first agent:start', () => {
      const store = createMockTeamStore()
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
      const { bridgeToTeam } = createTeamBridge(store as never)

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
  })
})
