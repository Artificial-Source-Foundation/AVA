/**
 * Agent Defaults
 *
 * Canonical types and default presets for AI agents.
 * Shared by the settings store and the AgentsTab UI.
 */

import {
  Bug,
  Building,
  Code,
  Crown,
  Eye,
  FileText,
  GitBranch,
  Layers,
  Layout,
  ListTodo,
  Rocket,
  Search,
  Server,
  Shield,
  Terminal,
  TestTube,
  Zap,
} from 'lucide-solid'
import type { Component } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

type IconComponent = Component<{ class?: string }>

export type AgentTier = 'commander' | 'lead' | 'worker'

export interface AgentPreset {
  id: string
  name: string
  description: string
  icon: IconComponent
  enabled: boolean
  systemPrompt?: string
  capabilities: string[]
  model?: string
  isCustom?: boolean
  type?: 'coding' | 'git' | 'terminal' | 'docs' | 'fast' | 'custom'
  /** Praxis tier — commander, lead, or worker */
  tier?: AgentTier
  /** Concrete tool names this agent can use */
  tools?: string[]
  /** Agent IDs this agent can delegate to (leads + commander) */
  delegates?: string[]
  /** Per-agent provider override */
  provider?: string
  /** Domain specialization */
  domain?: string
}

// ============================================================================
// Icon lookup by name (for bridging AgentDefinition.icon → component)
// ============================================================================

export const AGENT_ICONS: Record<string, IconComponent> = {
  Code: Code as IconComponent,
  Crown: Crown as IconComponent,
  Layout: Layout as IconComponent,
  Server: Server as IconComponent,
  Shield: Shield as IconComponent,
  Layers: Layers as IconComponent,
  TestTube: TestTube as IconComponent,
  Eye: Eye as IconComponent,
  Search: Search as IconComponent,
  Bug: Bug as IconComponent,
  Building: Building as IconComponent,
  ListTodo: ListTodo as IconComponent,
  Rocket: Rocket as IconComponent,
  GitBranch: GitBranch as IconComponent,
  Terminal: Terminal as IconComponent,
  FileText: FileText as IconComponent,
  Zap: Zap as IconComponent,
}

/** Resolve an icon name to its component. Falls back to Code. */
export function resolveAgentIcon(iconName?: string): IconComponent {
  if (!iconName) return Code as IconComponent
  return AGENT_ICONS[iconName] ?? (Code as IconComponent)
}

// ============================================================================
// Default Agent Presets (legacy — kept for backward compat)
// ============================================================================

export const legacyAgentPresets: AgentPreset[] = [
  {
    id: 'coding-assistant',
    name: 'Coding Assistant',
    description: 'Expert at writing, reviewing, and debugging code',
    icon: Code as IconComponent,
    enabled: true,
    capabilities: ['code-generation', 'debugging', 'refactoring', 'code-review'],
    model: 'claude-3.5-sonnet',
    type: 'coding',
  },
  {
    id: 'git-assistant',
    name: 'Git Assistant',
    description: 'Helps with version control and git operations',
    icon: GitBranch as IconComponent,
    enabled: true,
    capabilities: ['git-status', 'commit-messages', 'branch-management', 'merge-resolution'],
    type: 'git',
  },
  {
    id: 'terminal-agent',
    name: 'Terminal Agent',
    description: 'Executes shell commands and manages processes',
    icon: Terminal as IconComponent,
    enabled: true,
    capabilities: ['command-execution', 'process-management', 'environment-setup'],
    type: 'terminal',
  },
  {
    id: 'documentation',
    name: 'Documentation Writer',
    description: 'Creates and maintains documentation',
    icon: FileText as IconComponent,
    enabled: false,
    capabilities: ['readme', 'api-docs', 'comments', 'tutorials'],
    type: 'docs',
  },
  {
    id: 'fast-responder',
    name: 'Fast Responder',
    description: 'Quick responses for simple queries',
    icon: Zap as IconComponent,
    enabled: false,
    capabilities: ['quick-answers', 'simple-tasks'],
    model: 'claude-3-haiku',
    type: 'fast',
  },
]

// ============================================================================
// Praxis Agent Presets (the 14 built-in Praxis agents)
// ============================================================================

export const praxisAgentPresets: AgentPreset[] = [
  // Commander
  {
    id: 'commander',
    name: 'Commander',
    description: 'Plans and coordinates the team — never writes code directly',
    icon: Crown as IconComponent,
    enabled: true,
    tier: 'commander',
    tools: ['question', 'attempt_completion'],
    delegates: [
      'frontend-lead',
      'backend-lead',
      'qa-lead',
      'fullstack-lead',
      'planner',
      'architect',
    ],
    domain: 'fullstack',
    capabilities: ['coordination', 'planning', 'delegation'],
  },
  // Leads
  {
    id: 'frontend-lead',
    name: 'Frontend Lead',
    description: 'Manages frontend development',
    icon: Layout as IconComponent,
    enabled: true,
    tier: 'lead',
    delegates: ['coder', 'tester'],
    domain: 'frontend',
    capabilities: ['frontend-management', 'delegation'],
  },
  {
    id: 'backend-lead',
    name: 'Backend Lead',
    description: 'Manages backend development',
    icon: Server as IconComponent,
    enabled: true,
    tier: 'lead',
    delegates: ['coder', 'tester', 'debugger'],
    domain: 'backend',
    capabilities: ['backend-management', 'delegation'],
  },
  {
    id: 'qa-lead',
    name: 'QA Lead',
    description: 'Manages testing and review',
    icon: Shield as IconComponent,
    enabled: true,
    tier: 'lead',
    delegates: ['tester', 'reviewer'],
    domain: 'testing',
    capabilities: ['qa-management', 'delegation'],
  },
  {
    id: 'fullstack-lead',
    name: 'Fullstack Lead',
    description: 'Manages cross-cutting work',
    icon: Layers as IconComponent,
    enabled: true,
    tier: 'lead',
    delegates: ['coder', 'tester', 'debugger', 'reviewer', 'devops'],
    domain: 'fullstack',
    capabilities: ['fullstack-management', 'delegation'],
  },
  // Workers
  {
    id: 'coder',
    name: 'Coder',
    description: 'Writes and modifies code files',
    icon: Code as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['read_file', 'write_file', 'create_file', 'delete_file', 'edit', 'grep', 'glob'],
    domain: 'fullstack',
    capabilities: ['code-generation', 'refactoring'],
  },
  {
    id: 'tester',
    name: 'Tester',
    description: 'Writes and runs tests',
    icon: TestTube as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['read_file', 'write_file', 'create_file', 'bash', 'grep', 'glob'],
    domain: 'testing',
    capabilities: ['test-writing', 'test-running'],
  },
  {
    id: 'reviewer',
    name: 'Reviewer',
    description: 'Reviews code for quality, bugs, and security',
    icon: Eye as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['read_file', 'grep', 'glob'],
    domain: 'fullstack',
    capabilities: ['code-review', 'security-analysis'],
  },
  {
    id: 'researcher',
    name: 'Researcher',
    description: 'Explores the codebase and gathers context',
    icon: Search as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['read_file', 'grep', 'glob', 'ls'],
    domain: 'fullstack',
    capabilities: ['codebase-exploration', 'context-gathering'],
  },
  {
    id: 'debugger',
    name: 'Debugger',
    description: 'Debugs and fixes errors',
    icon: Bug as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['read_file', 'write_file', 'edit', 'bash', 'grep', 'glob'],
    domain: 'fullstack',
    capabilities: ['debugging', 'error-diagnosis'],
  },
  {
    id: 'architect',
    name: 'Architect',
    description: 'Reviews architecture and suggests patterns',
    icon: Building as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['read_file', 'grep', 'glob', 'ls', 'question'],
    domain: 'fullstack',
    capabilities: ['architecture-review', 'pattern-suggestion'],
  },
  {
    id: 'planner',
    name: 'Planner',
    description: 'Breaks complex tasks into subtasks',
    icon: ListTodo as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['read_file', 'grep', 'glob', 'ls'],
    domain: 'fullstack',
    capabilities: ['task-planning', 'decomposition'],
  },
  {
    id: 'devops',
    name: 'DevOps',
    description: 'Runs shell commands and manages build/deploy',
    icon: Rocket as IconComponent,
    enabled: true,
    tier: 'worker',
    tools: ['bash', 'read_file', 'glob', 'grep'],
    domain: 'devops',
    capabilities: ['shell-commands', 'build-management'],
  },
]

// ============================================================================
// Combined defaults
// ============================================================================

export const defaultAgentPresets: AgentPreset[] = [...praxisAgentPresets, ...legacyAgentPresets]
