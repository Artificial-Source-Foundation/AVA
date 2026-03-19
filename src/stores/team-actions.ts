/**
 * Team Actions
 * Functions that mutate team member state.
 * Accepts setter functions as parameters to avoid circular imports.
 */

import { log } from '../lib/logger'
import type {
  DelegationEvent,
  TeamMember,
  TeamMessage,
  TeamStatus,
  TeamToolCall,
} from '../types/team'

type TeamMemberSetter = (
  updater: (prev: Map<string, TeamMember>) => Map<string, TeamMember>
) => void

export function addMember(set: TeamMemberSetter, member: TeamMember): void {
  log.info('team', 'Worker spawned', {
    id: member.id,
    name: member.name,
    role: member.role,
    domain: member.domain,
  })
  set((prev) => {
    const next = new Map(prev)
    next.set(member.id, member)
    return next
  })
}

export function updateMember(
  set: TeamMemberSetter,
  id: string,
  updates: Partial<TeamMember>
): void {
  set((prev) => {
    const existing = prev.get(id)
    if (!existing) return prev
    const next = new Map(prev)
    next.set(id, { ...existing, ...updates })
    return next
  })
}

export function updateMemberStatus(set: TeamMemberSetter, id: string, status: TeamStatus): void {
  log.info('team', 'Worker status changed', { id, status })
  updateMember(set, id, { status, ...(status === 'done' ? { completedAt: Date.now() } : {}) })
}

export function removeMember(set: TeamMemberSetter, id: string): void {
  log.info('team', 'Worker removed', { id })
  set((prev) => {
    const next = new Map(prev)
    next.delete(id)
    return next
  })
}

export function addToolCall(set: TeamMemberSetter, memberId: string, toolCall: TeamToolCall): void {
  set((prev) => {
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

export function updateToolCall(
  set: TeamMemberSetter,
  memberId: string,
  toolId: string,
  updates: Partial<TeamToolCall>
): void {
  set((prev) => {
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

export function addMessage(set: TeamMemberSetter, memberId: string, message: TeamMessage): void {
  set((prev) => {
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

/** Update an existing message's content in-place (used for thought accumulation). */
export function updateMessage(
  set: TeamMemberSetter,
  memberId: string,
  messageId: string,
  content: string
): void {
  set((prev) => {
    const existing = prev.get(memberId)
    if (!existing) return prev
    const next = new Map(prev)
    next.set(memberId, {
      ...existing,
      messages: existing.messages.map((m) => (m.id === messageId ? { ...m, content } : m)),
    })
    return next
  })
}

/** Add a delegation event to the log */
export function addDelegation(
  setLog: (updater: (prev: DelegationEvent[]) => DelegationEvent[]) => void,
  event: DelegationEvent
): void {
  log.info('team', 'Delegation event', {
    from: event.fromMember,
    to: event.toMember,
    status: event.status,
  })
  setLog((prev) => [...prev, event])
}
