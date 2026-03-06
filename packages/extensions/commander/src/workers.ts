/**
 * Built-in Praxis v2 agent definitions.
 */

import type { AgentDefinition } from './agent-definition.js'
import type { WorkerDefinition } from './types.js'

const DIRECTOR_TOOLS = [
  'read_file',
  'glob',
  'grep',
  'websearch',
  'webfetch',
  'invoke_team',
  'invoke_subagent',
  'attempt_completion',
  'remember',
  'recall',
] as const

const TECH_LEAD_TOOLS = ['*'] as const

const ENGINEER_TOOLS = ['*'] as const

const REVIEWER_TOOLS = ['read_file', 'glob', 'grep', 'bash', 'attempt_completion'] as const

export const DIRECTOR_AGENT: AgentDefinition = {
  id: 'director',
  name: 'director',
  displayName: 'Director',
  description: 'Plans, delegates, and summarizes. Never writes code directly.',
  tier: 'director',
  systemPrompt:
    'You are the DIRECTOR. You plan, orchestrate, and communicate. You NEVER write code directly. Decompose work, assign it to Tech Leads, use subagents for research, and produce clear user summaries.',
  tools: [...DIRECTOR_TOOLS],
  delegates: ['tech-lead'],
  maxTurns: 20,
  maxTimeMinutes: 15,
  domain: 'fullstack',
  capabilities: ['orchestration', 'planning', 'delegation'],
  isBuiltIn: true,
}

export const TECH_LEAD_AGENT: AgentDefinition = {
  id: 'tech-lead',
  name: 'tech-lead',
  displayName: 'Tech Lead',
  description: 'Supervises engineers, reviews work, merges branches, and validates quality.',
  tier: 'tech-lead',
  systemPrompt:
    'You are a TECH LEAD. Supervise Engineers, review their worktrees, make small fixes on reviewed files, merge branches, run integration tests, and report clean summaries to Director.',
  tools: [...TECH_LEAD_TOOLS],
  delegates: ['engineer'],
  maxTurns: 16,
  maxTimeMinutes: 12,
  domain: 'fullstack',
  capabilities: ['supervision', 'review', 'merge'],
  isBuiltIn: true,
}

export const ENGINEER_AGENT: AgentDefinition = {
  id: 'engineer',
  name: 'engineer',
  displayName: 'Engineer',
  description: 'Implements scoped coding tasks in an isolated worktree.',
  tier: 'engineer',
  systemPrompt:
    'You are an ENGINEER. Implement assigned code in an isolated worktree. Run reviewer subagent checks before reporting completion. You cannot invoke team members or use web search tools.',
  tools: [...ENGINEER_TOOLS],
  maxTurns: 15,
  maxTimeMinutes: 10,
  domain: 'fullstack',
  capabilities: ['coding', 'self-review'],
  isBuiltIn: true,
}

export const REVIEWER_AGENT: AgentDefinition = {
  id: 'reviewer',
  name: 'reviewer',
  displayName: 'Reviewer',
  description: 'Runs lint/typecheck/tests and performs correctness review.',
  tier: 'reviewer',
  systemPrompt:
    'You are a REVIEWER. Validate code quality with lint, typecheck, and affected tests. Return approved true/false and actionable feedback.',
  tools: [...REVIEWER_TOOLS],
  maxTurns: 12,
  maxTimeMinutes: 8,
  domain: 'testing',
  capabilities: ['validation', 'quality-gate'],
  isBuiltIn: true,
}

export const WORKER_AGENTS: AgentDefinition[] = [ENGINEER_AGENT, REVIEWER_AGENT]
export const LEAD_AGENTS: AgentDefinition[] = [TECH_LEAD_AGENT]
export const COMMANDER_AGENT: AgentDefinition = DIRECTOR_AGENT
export const BUILTIN_AGENTS: AgentDefinition[] = [
  DIRECTOR_AGENT,
  TECH_LEAD_AGENT,
  ENGINEER_AGENT,
  REVIEWER_AGENT,
]

/** Legacy compat: map engineer/reviewer into old worker shape. */
export const BUILTIN_WORKERS: WorkerDefinition[] = [ENGINEER_AGENT, REVIEWER_AGENT].map((a) => ({
  name: a.name,
  displayName: a.displayName,
  description: a.description,
  systemPrompt: a.systemPrompt,
  tools: a.tools,
  maxTurns: a.maxTurns,
  maxTimeMinutes: a.maxTimeMinutes,
}))
