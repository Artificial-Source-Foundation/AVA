/**
 * Team Store
 *
 * Manages the dev team hierarchy state with real team identities.
 * Maps agent events to team members for UI visualization.
 *
 * Structure:
 *   Team Lead (orchestrator) — you talk to this one
 *   ├─ Frontend Team   (Senior Frontend Lead + Junior Devs)
 *   ├─ Backend Team    (Senior Backend Lead + Junior Devs)
 *   ├─ QA Team         (Senior QA Lead + Junior Devs)
 *   └─ ...
 *
 * Each Senior Lead IS the team — when they get delegated to,
 * it's like spinning up that whole team.
 */

import { createMemo, createSignal } from 'solid-js'
import { log } from '../lib/logger'
import type {
  DelegationEvent,
  TeamGroup,
  TeamHierarchy,
  TeamMember,
  TeamMessage,
  TeamStatus,
  TeamToolCall,
} from '../types/team'
import { TEAM_DOMAINS } from '../types/team'
import * as actions from './team-actions'
import {
  agentTypeToRole,
  aggregateStatus,
  generateName,
  groupProgress,
  inferDomain,
} from './team-helpers'

// ============================================================================
// State
// ============================================================================

const [teamMembers, setTeamMembers] = createSignal<Map<string, TeamMember>>(new Map())

/** Currently selected member (for viewing their chat) */
const [selectedMemberId, _setSelectedMemberId] = createSignal<string | null>(null)

/** Navigation history stack for back-navigation in team chat */
const [viewStack, setViewStack] = createSignal<Array<string | null>>([])

/** Navigate to a member's chat (pushes current view onto stack) */
function setSelectedMemberId(id: string | null): void {
  const current = selectedMemberId()
  if (current !== id) {
    setViewStack((prev) => [...prev, current])
    _setSelectedMemberId(id)
  }
}

/** Navigate back in the view stack */
function navigateBack(): void {
  setViewStack((prev) => {
    if (prev.length === 0) return prev
    const next = [...prev]
    const target = next.pop()!
    _setSelectedMemberId(target)
    return next
  })
}

/** Chronological log of all delegation events */
const [delegationLog, setDelegationLog] = createSignal<DelegationEvent[]>([])

// ============================================================================
// Computed
// ============================================================================

/** Get the Team Lead (root of hierarchy) */
const teamLead = createMemo((): TeamMember | null => {
  for (const member of teamMembers().values()) {
    if (member.role === 'team-lead') return member
  }
  return null
})

/** Get all members as array */
const allMembers = createMemo((): TeamMember[] => {
  return Array.from(teamMembers().values())
})

/** Get children of a specific member */
function getChildren(parentId: string): TeamMember[] {
  return allMembers().filter((m) => m.parentId === parentId)
}

/** Get Senior Leads (each one represents a team) */
const seniorLeads = createMemo((): TeamMember[] => {
  return allMembers().filter((m) => m.role === 'senior-lead')
})

/**
 * Build team groups — the primary data structure for UI rendering.
 * Each Senior Lead = one team card.
 */
const teamGroups = createMemo((): TeamGroup[] => {
  return seniorLeads().map((lead) => {
    const members = getChildren(lead.id)
    return {
      lead,
      config: TEAM_DOMAINS[lead.domain],
      members,
      status: aggregateStatus(lead, members),
      progress: groupProgress(lead, members),
    }
  })
})

/**
 * Full team hierarchy for rendering.
 * Returns null if no Team Lead exists yet.
 */
const hierarchy = createMemo((): TeamHierarchy | null => {
  const lead = teamLead()
  if (!lead) return null
  return {
    teamLead: lead,
    teams: teamGroups(),
  }
})

/** Currently selected member */
const selectedMember = createMemo((): TeamMember | null => {
  const id = selectedMemberId()
  if (!id) return null
  return teamMembers().get(id) ?? null
})

/** Get the team group that a member belongs to */
function getMemberTeam(memberId: string): TeamGroup | null {
  const member = teamMembers().get(memberId)
  if (!member) return null

  // If this IS a senior lead, find their group directly
  if (member.role === 'senior-lead') {
    return teamGroups().find((g) => g.lead.id === memberId) ?? null
  }

  // If this is a junior dev, find their parent's group
  if (member.parentId) {
    return teamGroups().find((g) => g.lead.id === member.parentId) ?? null
  }

  return null
}

/** Team stats */
const teamStats = createMemo(() => {
  const members = allMembers()
  const groups = teamGroups()
  return {
    totalMembers: members.length,
    totalTeams: groups.length,
    activeTeams: groups.filter((g) => g.status === 'working').length,
    doneTeams: groups.filter((g) => g.status === 'done').length,
    errorTeams: groups.filter((g) => g.status === 'error').length,
  }
})

/** Aggregate token usage across all team members */
const teamTokenUsage = createMemo(() => {
  let input = 0
  let output = 0
  for (const member of allMembers()) {
    if (member.tokenUsage) {
      input += member.tokenUsage.input
      output += member.tokenUsage.output
    }
  }
  return { input, output }
})

/** Unique files changed across all team members */
const teamFilesChanged = createMemo(() => {
  const files = new Set<string>()
  for (const member of allMembers()) {
    if (member.filesChanged) {
      for (const file of member.filesChanged) {
        files.add(file)
      }
    }
  }
  return Array.from(files)
})

// ============================================================================
// Bound Actions (curry setTeamMembers into extracted action functions)
// ============================================================================

/** Count existing junior devs under a lead (for naming) */
function countJuniorDevs(parentId: string): number {
  return getChildren(parentId).filter((m) => m.role === 'junior-dev').length
}

/** Clear entire team (e.g., new session) */
function clearTeam(): void {
  log.info('team', 'Team cleared')
  setTeamMembers(new Map())
  _setSelectedMemberId(null)
  setViewStack([])
  setDelegationLog([])
}

// ============================================================================
// Hook
// ============================================================================

export function useTeam() {
  return {
    // State
    teamMembers,
    selectedMemberId,
    setSelectedMemberId,
    viewStack,
    navigateBack,
    delegationLog,

    // Computed
    teamLead,
    allMembers,
    seniorLeads,
    teamGroups,
    hierarchy,
    selectedMember,
    getMemberTeam,
    teamStats,
    teamTokenUsage,
    teamFilesChanged,

    // Actions (bound to setTeamMembers)
    addMember: (member: TeamMember) => actions.addMember(setTeamMembers, member),
    updateMember: (id: string, updates: Partial<TeamMember>) =>
      actions.updateMember(setTeamMembers, id, updates),
    updateMemberStatus: (id: string, status: TeamStatus) =>
      actions.updateMemberStatus(setTeamMembers, id, status),
    removeMember: (id: string) => actions.removeMember(setTeamMembers, id),
    addToolCall: (memberId: string, toolCall: TeamToolCall) =>
      actions.addToolCall(setTeamMembers, memberId, toolCall),
    updateToolCall: (memberId: string, toolId: string, updates: Partial<TeamToolCall>) =>
      actions.updateToolCall(setTeamMembers, memberId, toolId, updates),
    addMessage: (memberId: string, message: TeamMessage) =>
      actions.addMessage(setTeamMembers, memberId, message),
    updateMessage: (memberId: string, messageId: string, content: string) =>
      actions.updateMessage(setTeamMembers, memberId, messageId, content),
    addDelegation: (event: DelegationEvent) => actions.addDelegation(setDelegationLog, event),
    clearTeam,

    // Helpers
    agentTypeToRole,
    inferDomain,
    generateName,
    getChildren,
    countJuniorDevs,
  }
}
