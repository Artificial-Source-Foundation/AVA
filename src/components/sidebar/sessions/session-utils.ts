/**
 * Session display utilities
 *
 * Formatting helpers and date-grouping logic for sidebar session lists.
 */

import type { SessionWithStats } from '../../../types'

export const formatSessionName = (name: string): string => {
  if (name.length > 28) return `${name.slice(0, 28)}...`
  return name
}

export const formatDate = (timestamp: number): string => {
  const date = new Date(timestamp)
  const now = new Date()
  const diff = now.getTime() - date.getTime()
  const days = Math.floor(diff / (1000 * 60 * 60 * 24))

  if (days === 0) return 'Today'
  if (days === 1) return 'Yesterday'
  if (days < 7) return `${days}d ago`
  return date.toLocaleDateString('en-US', { month: 'short', day: 'numeric' })
}

const getDateGroup = (timestamp: number): string => {
  const now = new Date()
  const date = new Date(timestamp)
  const diff = now.getTime() - date.getTime()
  const days = Math.floor(diff / (1000 * 60 * 60 * 24))

  if (days === 0) return 'Today'
  if (days === 1) return 'Yesterday'
  if (days < 7) return 'This Week'
  return 'Older'
}

export interface SessionGroup {
  label: string
  sessions: SessionWithStats[]
}

export function groupSessionsByDate(sessions: SessionWithStats[]): SessionGroup[] {
  const groups: SessionGroup[] = []
  const order = ['Today', 'Yesterday', 'This Week', 'Older']
  const map = new Map<string, SessionWithStats[]>()

  for (const session of sessions) {
    const group = getDateGroup(session.updatedAt)
    if (!map.has(group)) map.set(group, [])
    map.get(group)!.push(session)
  }

  for (const label of order) {
    const sessions = map.get(label)
    if (sessions?.length) groups.push({ label, sessions })
  }

  return groups
}
