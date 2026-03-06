/**
 * Team Helpers — Pure Functions
 * Domain inference, role mapping, name generation, and status aggregation.
 * No SolidJS signal dependencies — fully testable.
 */

import {
  TEAM_DOMAINS,
  type TeamDomain,
  type TeamMember,
  type TeamRole,
  type TeamStatus,
} from '../types/team'

/** Map internal agent type to team role */
export function agentTypeToRole(type: string): TeamRole {
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
export function inferDomain(task?: string, files?: string[]): TeamDomain {
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
export function generateName(role: TeamRole, domain: TeamDomain, index?: number): string {
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

/** Aggregate status for a team group */
export function aggregateStatus(lead: TeamMember, members: TeamMember[]): TeamStatus {
  const all = [lead, ...members]
  if (all.some((m) => m.status === 'error')) return 'error'
  if (all.some((m) => m.status === 'working')) return 'working'
  if (all.some((m) => m.status === 'reporting')) return 'reporting'
  if (all.every((m) => m.status === 'done')) return 'done'
  return 'idle'
}

/** Calculate progress for a team group (0-1) */
export function groupProgress(lead: TeamMember, members: TeamMember[]): number {
  const all = [lead, ...members]
  if (all.length === 0) return 0
  const done = all.filter((m) => m.status === 'done').length
  return done / all.length
}
