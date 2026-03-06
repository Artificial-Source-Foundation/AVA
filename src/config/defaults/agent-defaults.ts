/**
 * Agent Defaults
 *
 * Canonical types and default presets for AI agents.
 * Shared by the settings store and the AgentsTab UI.
 * Praxis agent presets live in praxis-presets.ts.
 */

import { Code, FileText, GitBranch, Terminal, Zap } from 'lucide-solid'
import type { Component } from 'solid-js'
import { AGENT_ICONS } from './agent-icons'

// Re-export for consumers
export { AGENT_ICONS } from './agent-icons'

// ============================================================================
// Types
// ============================================================================

export type IconComponent = Component<{ class?: string }>

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
// Icon resolver
// ============================================================================

/** Resolve an icon name to its component. Falls back to Code. */
export function resolveAgentIcon(iconName?: string): IconComponent {
  if (!iconName) return Code as IconComponent
  return AGENT_ICONS[iconName] ?? (Code as IconComponent)
}

// ============================================================================
// Legacy Agent Presets (kept for backward compat)
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
// Combined defaults (re-exported for consumers)
// ============================================================================

// Praxis presets (the 14 built-in Praxis agents)
import { praxisAgentPresets } from './praxis-presets'

export { praxisAgentPresets } from './praxis-presets'

export const defaultAgentPresets: AgentPreset[] = [...praxisAgentPresets, ...legacyAgentPresets]
