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
import {
  TEAM_DOMAINS,
  type TeamDomain,
  type TeamGroup,
  type TeamHierarchy,
  type TeamMember,
  type TeamMessage,
  type TeamRole,
  type TeamStatus,
  type TeamToolCall,
} from '../types/team'

// ============================================================================
// State
// ============================================================================

const [teamMembers, setTeamMembers] = createSignal<Map<string, TeamMember>>(new Map())

/** Currently selected member (for viewing their chat) */
const [selectedMemberId, setSelectedMemberId] = createSignal<string | null>(null)

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

/** Aggregate status for a team group */
function aggregateStatus(lead: TeamMember, members: TeamMember[]): TeamStatus {
  const all = [lead, ...members]
  if (all.some((m) => m.status === 'error')) return 'error'
  if (all.some((m) => m.status === 'working')) return 'working'
  if (all.some((m) => m.status === 'reporting')) return 'reporting'
  if (all.every((m) => m.status === 'done')) return 'done'
  return 'idle'
}

/** Calculate progress for a team group (0-1) */
function groupProgress(lead: TeamMember, members: TeamMember[]): number {
  const all = [lead, ...members]
  if (all.length === 0) return 0
  const done = all.filter((m) => m.status === 'done').length
  return done / all.length
}

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

// ============================================================================
// Actions
// ============================================================================

function addMember(member: TeamMember): void {
  setTeamMembers((prev) => {
    const next = new Map(prev)
    next.set(member.id, member)
    return next
  })
}

function updateMember(id: string, updates: Partial<TeamMember>): void {
  setTeamMembers((prev) => {
    const existing = prev.get(id)
    if (!existing) return prev
    const next = new Map(prev)
    next.set(id, { ...existing, ...updates })
    return next
  })
}

function updateMemberStatus(id: string, status: TeamStatus): void {
  updateMember(id, { status, ...(status === 'done' ? { completedAt: Date.now() } : {}) })
}

function removeMember(id: string): void {
  setTeamMembers((prev) => {
    const next = new Map(prev)
    next.delete(id)
    return next
  })
}

function addToolCall(memberId: string, toolCall: TeamToolCall): void {
  setTeamMembers((prev) => {
    const existing = prev.get(memberId)
    if (!existing) return prev
    const next = new Map(prev)
    next.set(memberId, {
      ...existing,
      toolCalls: [...existing.toolCalls, toolCall],
    })
    return next
  })
}

function updateToolCall(memberId: string, toolId: string, updates: Partial<TeamToolCall>): void {
  setTeamMembers((prev) => {
    const existing = prev.get(memberId)
    if (!existing) return prev
    const next = new Map(prev)
    next.set(memberId, {
      ...existing,
      toolCalls: existing.toolCalls.map((tc) => (tc.id === toolId ? { ...tc, ...updates } : tc)),
    })
    return next
  })
}

function addMessage(memberId: string, message: TeamMessage): void {
  setTeamMembers((prev) => {
    const existing = prev.get(memberId)
    if (!existing) return prev
    const next = new Map(prev)
    next.set(memberId, {
      ...existing,
      messages: [...existing.messages, message],
    })
    return next
  })
}

/** Clear entire team (e.g., new session) */
function clearTeam(): void {
  setTeamMembers(new Map())
  setSelectedMemberId(null)
}

// ============================================================================
// Helpers — Mapping from agent events to team model
// ============================================================================

/** Map internal agent type to team role */
function agentTypeToRole(type: string): TeamRole {
  switch (type) {
    case 'commander':
      return 'team-lead'
    case 'operator':
      return 'junior-dev'
    case 'validator':
      return 'junior-dev'
    default:
      return 'senior-lead'
  }
}

/** Infer domain from task description or file patterns */
function inferDomain(task?: string, files?: string[]): TeamDomain {
  const text = [task, ...(files || [])].join(' ').toLowerCase()

  // Frontend signals
  if (text.match(/\.(tsx|jsx|css|scss|html|svelte|vue|astro)\b/)) return 'frontend'
  if (text.match(/\b(component|style|layout|ui|ux|tailwind|react|solid)\b/)) return 'frontend'

  // Backend signals
  if (text.match(/\.(py|go|rs|java|rb|php|sql)\b/)) return 'backend'
  if (text.match(/\b(api|server|database|endpoint|migration|route)\b/)) return 'backend'

  // Testing signals
  if (text.match(/\b(test|spec|vitest|jest|playwright|cypress|coverage)\b/)) return 'testing'

  // DevOps signals
  if (text.match(/\b(docker|ci|cd|deploy|k8s|terraform|nginx|helm)\b/)) return 'devops'

  // Docs signals
  if (text.match(/\b(doc|readme|changelog|guide|tutorial|mdx?)\b/)) return 'docs'

  // Design signals
  if (text.match(/\b(design|theme|token|color|typography|icon|animation)\b/)) return 'design'

  // Data signals
  if (text.match(/\b(data|analytics|pipeline|etl|schema|model)\b/)) return 'data'

  // Security signals
  if (text.match(/\b(security|auth|permission|owasp|vulnerability|secret)\b/)) return 'security'

  // Fullstack if mixed signals
  if (text.match(/\b(fullstack|full-stack)\b/)) return 'fullstack'

  return 'general'
}

/** Generate display name from role and domain */
function generateName(role: TeamRole, domain: TeamDomain, index?: number): string {
  const config = TEAM_DOMAINS[domain]
  switch (role) {
    case 'team-lead':
      return 'Team Lead'
    case 'senior-lead':
      return `Senior ${config.label.replace(' Team', '')} Lead`
    case 'junior-dev': {
      const base = `${config.label.replace(' Team', '')} Dev`
      return index !== undefined ? `${base} #${index + 1}` : base
    }
  }
}

/** Count existing junior devs under a lead (for naming) */
function countJuniorDevs(parentId: string): number {
  return getChildren(parentId).filter((m) => m.role === 'junior-dev').length
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

    // Computed
    teamLead,
    allMembers,
    seniorLeads,
    teamGroups,
    hierarchy,
    selectedMember,
    getMemberTeam,
    teamStats,

    // Actions
    addMember,
    updateMember,
    updateMemberStatus,
    removeMember,
    addToolCall,
    updateToolCall,
    addMessage,
    clearTeam,

    // Helpers
    agentTypeToRole,
    inferDomain,
    generateName,
    getChildren,
    countJuniorDevs,
  }
}
