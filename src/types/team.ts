/**
 * Team Types
 *
 * Dev Team hierarchy with real team identities.
 *
 * Structure:
 *   Team Lead (orchestrator)
 *   └─ Frontend Team (Senior Frontend Lead + Junior Devs)
 *   └─ Backend Team  (Senior Backend Lead + Junior Devs)
 *   └─ QA Team       (Senior QA Lead + Junior Devs)
 *   └─ DevOps Team   (Senior DevOps Lead + Junior Devs)
 *   └─ Docs Team     (Senior Docs Lead + Junior Devs)
 *
 * Each Senior Lead represents a team. When delegated to,
 * it's like spinning up that whole team.
 */

/** Team member roles */
export type TeamRole = 'team-lead' | 'senior-lead' | 'junior-dev'

/** Team member status */
export type TeamStatus = 'idle' | 'working' | 'reporting' | 'done' | 'error'

/**
 * Team domains — each maps to a named team.
 * Senior Leads own a domain; Junior Devs inherit from their lead.
 */
export type TeamDomain =
  | 'frontend'
  | 'backend'
  | 'fullstack'
  | 'testing'
  | 'devops'
  | 'docs'
  | 'design'
  | 'data'
  | 'security'
  | 'general'

/** Display config for each team domain */
export interface TeamDomainConfig {
  /** Human label, e.g., "Frontend Team" */
  label: string
  /** Short label for badges, e.g., "FE" */
  short: string
  /** CSS variable for team accent color */
  color: string
  /** CSS variable for subtle bg */
  colorSubtle: string
  /** Emoji/icon hint for the UI */
  emoji: string
}

/** Domain display configs */
export const TEAM_DOMAINS: Record<TeamDomain, TeamDomainConfig> = {
  frontend: {
    label: 'Frontend Team',
    short: 'FE',
    color: 'var(--violet-7)',
    colorSubtle: 'var(--violet-alpha-15)',
    emoji: '🎨',
  },
  backend: {
    label: 'Backend Team',
    short: 'BE',
    color: 'var(--teal-4)',
    colorSubtle: 'var(--teal-alpha-15)',
    emoji: '⚙️',
  },
  fullstack: {
    label: 'Fullstack Team',
    short: 'FS',
    color: 'var(--blue-4)',
    colorSubtle: 'var(--blue-alpha-15)',
    emoji: '🔧',
  },
  testing: {
    label: 'QA Team',
    short: 'QA',
    color: 'var(--green-4)',
    colorSubtle: 'var(--green-alpha-15)',
    emoji: '🧪',
  },
  devops: {
    label: 'DevOps Team',
    short: 'OPS',
    color: 'var(--orange-4)',
    colorSubtle: 'var(--orange-alpha-15)',
    emoji: '🚀',
  },
  docs: {
    label: 'Docs Team',
    short: 'DOCS',
    color: 'var(--blue-4)',
    colorSubtle: 'var(--blue-alpha-15)',
    emoji: '📝',
  },
  design: {
    label: 'Design Team',
    short: 'UI',
    color: 'var(--pink-4)',
    colorSubtle: 'var(--pink-alpha-15)',
    emoji: '✨',
  },
  data: {
    label: 'Data Team',
    short: 'DATA',
    color: 'var(--cyan-4)',
    colorSubtle: 'var(--cyan-alpha-15)',
    emoji: '📊',
  },
  security: {
    label: 'Security Team',
    short: 'SEC',
    color: 'var(--red-4)',
    colorSubtle: 'var(--red-alpha-15)',
    emoji: '🛡️',
  },
  general: {
    label: 'General Team',
    short: 'GEN',
    color: 'var(--gray-9)',
    colorSubtle: 'var(--alpha-white-5)',
    emoji: '💻',
  },
}

/** A single member in the team hierarchy */
export interface TeamMember {
  id: string
  /** Display name (e.g., "Senior Frontend Lead", "Frontend Dev #2") */
  name: string
  role: TeamRole
  status: TeamStatus
  /** Parent member ID (null for Team Lead) */
  parentId: string | null
  /** Domain / team this member belongs to */
  domain: TeamDomain
  /** LLM model being used */
  model: string
  /** Current task description */
  task?: string
  /** Tool calls made by this member */
  toolCalls: TeamToolCall[]
  /** Messages from this member */
  messages: TeamMessage[]
  createdAt: number
  completedAt?: number
  /** Result summary when done */
  result?: string
  /** Error message if failed */
  error?: string
  /** When this member was delegated their task */
  delegatedAt?: number
  /** Context from parent when delegating (e.g., "Handle the frontend components") */
  delegationContext?: string
}

/** A tool call associated with a team member */
export interface TeamToolCall {
  id: string
  name: string
  status: 'running' | 'success' | 'error'
  durationMs?: number
  timestamp: number
}

/** A message in a team member's chat */
export interface TeamMessage {
  id: string
  role: 'user' | 'assistant'
  content: string
  timestamp: number
}

/**
 * A team group for rendering.
 * The Senior Lead is the "team identity" — Junior Devs are their crew.
 */
export interface TeamGroup {
  /** The Senior Lead who owns this team */
  lead: TeamMember
  /** Domain config for display */
  config: TeamDomainConfig
  /** Junior Devs under this lead */
  members: TeamMember[]
  /** Aggregate status: working if any member is working, etc. */
  status: TeamStatus
  /** Progress: done members / total members */
  progress: number
}

/** Full team tree for rendering */
export interface TeamHierarchy {
  /** The Team Lead (root orchestrator) */
  teamLead: TeamMember
  /** Active teams (each led by a Senior Lead) */
  teams: TeamGroup[]
}
