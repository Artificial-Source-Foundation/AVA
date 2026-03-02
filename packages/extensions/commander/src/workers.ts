/**
 * Built-in agent definitions — workers, leads, and commander.
 *
 * All 14 Praxis agents defined here. Legacy BUILTIN_WORKERS array is
 * maintained for backward compat with task.ts imports.
 */

import type { AgentDefinition } from './agent-definition.js'
import { EXPLORE_WORKER } from './explore.js'
import type { WorkerDefinition } from './types.js'

// ─── Tool sets by domain ────────────────────────────────────────────────────

const READ_TOOLS = ['read_file', 'grep', 'glob'] as const
const READ_TOOLS_PLUS = ['read_file', 'grep', 'glob', 'ls'] as const

/** Frontend Lead: core file tools + search, no DB/LSP tools */
const FRONTEND_LEAD_TOOLS = [
  'read_file',
  'write_file',
  'edit',
  'bash',
  'glob',
  'grep',
  'create_file',
  'ls',
  'websearch',
] as const

/** Backend Lead: all core tools + LSP tools */
const BACKEND_LEAD_TOOLS = [
  'read_file',
  'write_file',
  'edit',
  'bash',
  'glob',
  'grep',
  'create_file',
  'delete_file',
  'ls',
  'lsp_diagnostics',
  'lsp_hover',
  'lsp_definition',
] as const

/** QA Lead: read-only + test runner + diagnostics */
const QA_LEAD_TOOLS = ['read_file', 'bash', 'grep', 'glob', 'lsp_diagnostics'] as const

/** Fullstack Lead: all available tools */
const FULLSTACK_LEAD_TOOLS = [
  'read_file',
  'write_file',
  'edit',
  'bash',
  'glob',
  'grep',
  'create_file',
  'delete_file',
  'ls',
  'websearch',
  'lsp_diagnostics',
  'lsp_hover',
  'lsp_definition',
] as const

// ─── Worker Agents (tier: worker) ───────────────────────────────────────────

export const WORKER_AGENTS: AgentDefinition[] = [
  {
    id: 'coder',
    name: 'coder',
    displayName: 'Coder',
    description: 'Writes and modifies code files',
    tier: 'worker',
    systemPrompt:
      'You are a senior developer. Write clean, well-structured code. Focus on the task, make minimal changes, and follow existing patterns.',
    tools: ['read_file', 'write_file', 'create_file', 'delete_file', 'edit', 'grep', 'glob'],
    maxTurns: 15,
    maxTimeMinutes: 5,
    domain: 'fullstack',
    icon: 'Code',
    capabilities: ['code-generation', 'refactoring'],
    isBuiltIn: true,
  },
  {
    id: 'tester',
    name: 'tester',
    displayName: 'Tester',
    description: 'Writes and runs tests',
    tier: 'worker',
    systemPrompt:
      'You are a QA engineer. Write comprehensive tests covering happy paths, edge cases, and error cases. Run tests to verify they pass.',
    tools: ['read_file', 'write_file', 'create_file', 'bash', 'grep', 'glob'],
    maxTurns: 10,
    maxTimeMinutes: 5,
    domain: 'testing',
    icon: 'TestTube',
    capabilities: ['test-writing', 'test-running'],
    isBuiltIn: true,
  },
  {
    id: 'reviewer',
    name: 'reviewer',
    displayName: 'Reviewer',
    description: 'Reviews code for quality, bugs, and security',
    tier: 'worker',
    systemPrompt:
      'You are a code reviewer. Analyze code for bugs, security issues, and quality. You have read-only access.',
    tools: [...READ_TOOLS],
    maxTurns: 10,
    maxTimeMinutes: 5,
    domain: 'fullstack',
    icon: 'Eye',
    capabilities: ['code-review', 'security-analysis'],
    isBuiltIn: true,
  },
  {
    id: 'researcher',
    name: 'researcher',
    displayName: 'Researcher',
    description: 'Explores the codebase and gathers context',
    tier: 'worker',
    systemPrompt:
      'You are a codebase researcher. Explore the codebase to gather context and understand architecture. You have read-only access.',
    tools: [...READ_TOOLS_PLUS],
    maxTurns: 15,
    maxTimeMinutes: 5,
    domain: 'fullstack',
    icon: 'Search',
    capabilities: ['codebase-exploration', 'context-gathering'],
    isBuiltIn: true,
  },
  {
    id: 'debugger',
    name: 'debugger',
    displayName: 'Debugger',
    description: 'Debugs and fixes errors',
    tier: 'worker',
    systemPrompt:
      'You are a debugging specialist. Diagnose issues, trace errors, and apply fixes. Be methodical and verify fixes work.',
    tools: ['read_file', 'write_file', 'edit', 'bash', 'grep', 'glob'],
    maxTurns: 15,
    maxTimeMinutes: 5,
    domain: 'fullstack',
    icon: 'Bug',
    capabilities: ['debugging', 'error-diagnosis'],
    isBuiltIn: true,
  },
  {
    id: 'architect',
    name: 'architect',
    displayName: 'Architect',
    description: 'Reviews architecture and suggests patterns',
    tier: 'worker',
    systemPrompt:
      'You are a software architect. Review code architecture, suggest design patterns, and identify structural improvements. You have read-only access. Ask clarifying questions when needed.',
    tools: [...READ_TOOLS_PLUS, 'question'],
    maxTurns: 10,
    maxTimeMinutes: 5,
    domain: 'fullstack',
    icon: 'Building',
    capabilities: ['architecture-review', 'pattern-suggestion'],
    isBuiltIn: true,
  },
  {
    id: 'planner',
    name: 'planner',
    displayName: 'Planner',
    description: 'Breaks complex tasks into subtasks with file assignments',
    tier: 'worker',
    systemPrompt: `You are a task planner. Break complex tasks into clear subtasks.

For each subtask, specify:
- A clear description of what needs to be done
- The domain (frontend, backend, testing, devops, fullstack)
- Affected files
- Which lead should handle it (frontend-lead, backend-lead, qa-lead, fullstack-lead)

Return a structured plan as JSON in your final response:
{
  "subtasks": [
    { "description": "...", "domain": "frontend", "files": ["src/..."], "assignTo": "frontend-lead" }
  ],
  "dependencies": [[0, 1]]
}

Dependencies are [blocker, blocked] index pairs — subtask 1 waits for subtask 0.`,
    tools: [...READ_TOOLS_PLUS],
    maxTurns: 10,
    maxTimeMinutes: 3,
    domain: 'fullstack',
    icon: 'ListTodo',
    capabilities: ['task-planning', 'decomposition'],
    isBuiltIn: true,
  },
  {
    id: 'devops',
    name: 'devops',
    displayName: 'DevOps',
    description: 'Runs shell commands and manages build/deploy',
    tier: 'worker',
    systemPrompt:
      'You are a DevOps engineer. Run shell commands, manage builds, check CI/CD pipelines, and handle deployment tasks. Be careful with destructive operations.',
    tools: ['bash', 'read_file', 'glob', 'grep'],
    maxTurns: 10,
    maxTimeMinutes: 5,
    domain: 'devops',
    icon: 'Rocket',
    capabilities: ['shell-commands', 'build-management'],
    isBuiltIn: true,
  },
  EXPLORE_WORKER,
]

// ─── Lead Agents (tier: lead) ───────────────────────────────────────────────

export const LEAD_AGENTS: AgentDefinition[] = [
  {
    id: 'frontend-lead',
    name: 'frontend-lead',
    displayName: 'Frontend Lead',
    description: 'Manages frontend development — delegates to coder and tester',
    tier: 'lead',
    systemPrompt: `You are the Frontend Lead. You manage frontend development tasks.

You can delegate to your workers:
- **Coder** for writing/modifying code
- **Tester** for writing and running tests

For simple tasks, handle them yourself. For complex tasks, delegate to the right worker.
Always review worker results before reporting back to the commander.`,
    tools: [...FRONTEND_LEAD_TOOLS],
    delegates: ['coder', 'tester'],
    maxTurns: 12,
    maxTimeMinutes: 8,
    domain: 'frontend',
    icon: 'Layout',
    capabilities: ['frontend-management', 'delegation'],
    isBuiltIn: true,
  },
  {
    id: 'backend-lead',
    name: 'backend-lead',
    displayName: 'Backend Lead',
    description: 'Manages backend development — delegates to coder, tester, and debugger',
    tier: 'lead',
    systemPrompt: `You are the Backend Lead. You manage backend development tasks.

You can delegate to your workers:
- **Coder** for writing/modifying code
- **Tester** for writing and running tests
- **Debugger** for diagnosing and fixing issues

For simple tasks, handle them yourself. For complex tasks, delegate to the right worker.
Always review worker results before reporting back to the commander.`,
    tools: [...BACKEND_LEAD_TOOLS],
    delegates: ['coder', 'tester', 'debugger'],
    maxTurns: 12,
    maxTimeMinutes: 8,
    domain: 'backend',
    icon: 'Server',
    capabilities: ['backend-management', 'delegation'],
    isBuiltIn: true,
  },
  {
    id: 'qa-lead',
    name: 'qa-lead',
    displayName: 'QA Lead',
    description: 'Manages testing and review — delegates to tester and reviewer',
    tier: 'lead',
    systemPrompt: `You are the QA Lead. You manage testing and code review tasks.

You can delegate to your workers:
- **Tester** for writing and running tests
- **Reviewer** for code quality and security review

Ensure comprehensive test coverage and code quality before reporting back.`,
    tools: [...QA_LEAD_TOOLS],
    delegates: ['tester', 'reviewer'],
    maxTurns: 10,
    maxTimeMinutes: 8,
    domain: 'testing',
    icon: 'Shield',
    capabilities: ['qa-management', 'delegation'],
    isBuiltIn: true,
  },
  {
    id: 'fullstack-lead',
    name: 'fullstack-lead',
    displayName: 'Fullstack Lead',
    description: 'Manages cross-cutting work — can delegate to any worker',
    tier: 'lead',
    systemPrompt: `You are the Fullstack Lead. You handle cross-cutting tasks that span multiple domains.

You can delegate to any worker:
- **Coder** for writing/modifying code
- **Tester** for writing and running tests
- **Debugger** for diagnosing and fixing issues
- **Reviewer** for code quality review
- **DevOps** for shell commands and build tasks

For simple tasks, handle them yourself. For complex tasks, delegate appropriately.`,
    tools: [...FULLSTACK_LEAD_TOOLS],
    delegates: ['coder', 'tester', 'debugger', 'reviewer', 'devops'],
    maxTurns: 12,
    maxTimeMinutes: 10,
    domain: 'fullstack',
    icon: 'Layers',
    capabilities: ['fullstack-management', 'delegation'],
    isBuiltIn: true,
  },
]

// ─── Commander (tier: commander) ────────────────────────────────────────────

export const COMMANDER_AGENT: AgentDefinition = {
  id: 'commander',
  name: 'commander',
  displayName: 'Commander',
  description: 'Plans and coordinates the team — never writes code directly',
  tier: 'commander',
  systemPrompt: '', // Set dynamically in index.ts with available leads
  tools: ['question', 'attempt_completion'], // Only meta tools — delegate tools added dynamically
  delegates: ['frontend-lead', 'backend-lead', 'qa-lead', 'fullstack-lead', 'planner', 'architect'],
  maxTurns: 20,
  maxTimeMinutes: 15,
  domain: 'fullstack',
  icon: 'Crown',
  capabilities: ['coordination', 'planning', 'delegation'],
  isBuiltIn: true,
}

// ─── All built-in agents ────────────────────────────────────────────────────

export const BUILTIN_AGENTS: AgentDefinition[] = [COMMANDER_AGENT, ...LEAD_AGENTS, ...WORKER_AGENTS]

// ─── Legacy compat: BUILTIN_WORKERS as WorkerDefinition[] ───────────────────

/** @deprecated Use WORKER_AGENTS or BUILTIN_AGENTS instead */
export const BUILTIN_WORKERS: WorkerDefinition[] = WORKER_AGENTS.filter((a) =>
  ['coder', 'tester', 'reviewer', 'researcher', 'debugger'].includes(a.id)
).map((a) => ({
  name: a.name,
  displayName: a.displayName,
  description: a.description,
  systemPrompt: a.systemPrompt,
  tools: a.tools,
  maxTurns: a.maxTurns,
  maxTimeMinutes: a.maxTimeMinutes,
}))
