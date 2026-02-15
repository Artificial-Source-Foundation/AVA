/**
 * Agent Defaults
 *
 * Canonical types and default presets for AI agents.
 * Shared by the settings store and the AgentsTab UI.
 */

import { Code, FileText, GitBranch, Terminal, Zap } from 'lucide-solid'
import type { Component } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

type IconComponent = Component<{ class?: string }>

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
}

// ============================================================================
// Default Agent Presets
// ============================================================================

export const defaultAgentPresets: AgentPreset[] = [
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
