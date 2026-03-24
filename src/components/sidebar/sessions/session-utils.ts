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

/** Compact relative time: "2m", "34m", "1h", "3d", "2w", "Jan 5" */
export const formatRelativeTime = (timestamp: number): string => {
  const now = Date.now()
  const diff = now - timestamp
  const minutes = Math.floor(diff / 60_000)
  const hours = Math.floor(diff / 3_600_000)
  const days = Math.floor(diff / 86_400_000)
  const weeks = Math.floor(days / 7)

  if (minutes < 1) return 'now'
  if (minutes < 60) return `${minutes}m`
  if (hours < 24) return `${hours}h`
  if (days < 7) return `${days}d`
  if (weeks < 5) return `${weeks}w`
  return new Date(timestamp).toLocaleDateString('en-US', { month: 'short', day: 'numeric' })
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
