/**
 * Agents Settings Tab
 *
 * Configure AI agent behavior, presets, and custom agents.
 */

import {
  Bot,
  Check,
  Code,
  FileText,
  GitBranch,
  Plus,
  Search,
  Settings,
  Sparkles,
  Terminal,
  Trash2,
  Wand2,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from '../../ui/Button'
import { Toggle } from '../../ui/Toggle'

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
}

export interface AgentsTabProps {
  agents: AgentPreset[]
  onToggle?: (id: string, enabled: boolean) => void
  onEdit?: (id: string) => void
  onDelete?: (id: string) => void
  onCreate?: () => void
}

// ============================================================================
// Default Agent Icons
// ============================================================================

const agentIcons: Record<string, IconComponent> = {
  default: Bot as IconComponent,
  code: Code as IconComponent,
  writing: FileText as IconComponent,
  git: GitBranch as IconComponent,
  terminal: Terminal as IconComponent,
  magic: Wand2 as IconComponent,
  fast: Zap as IconComponent,
}

// ============================================================================
// Agents Tab Component
// ============================================================================

export const AgentsTab: Component<AgentsTabProps> = (props) => {
  const [searchQuery, setSearchQuery] = createSignal('')
  const [showCustomOnly, setShowCustomOnly] = createSignal(false)

  const enabledCount = () => props.agents.filter((a) => a.enabled).length

  const filteredAgents = () => {
    let agents = props.agents

    if (showCustomOnly()) {
      agents = agents.filter((a) => a.isCustom)
    }

    const query = searchQuery().toLowerCase()
    if (query) {
      agents = agents.filter(
        (a) =>
          a.name.toLowerCase().includes(query) ||
          a.description.toLowerCase().includes(query) ||
          a.capabilities.some((c) => c.toLowerCase().includes(query))
      )
    }

    return agents
  }

  // Group agents: custom first, then built-in
  const groupedAgents = () => {
    const custom = filteredAgents().filter((a) => a.isCustom)
    const builtin = filteredAgents().filter((a) => !a.isCustom)
    return { custom, builtin }
  }

  return (
    <div class="space-y-6">
      {/* Header */}
      <div class="flex items-center justify-between">
        <div>
          <h3 class="text-sm font-medium text-[var(--text-primary)]">AI Agents</h3>
          <p class="text-xs text-[var(--text-muted)] mt-0.5">
            {enabledCount()} of {props.agents.length} agents enabled
          </p>
        </div>
        <Show when={props.onCreate}>
          <Button variant="primary" size="sm" onClick={props.onCreate}>
            <Plus class="w-4 h-4 mr-1" />
            Create Agent
          </Button>
        </Show>
      </div>

      {/* Search and Filter */}
      <div class="flex items-center gap-3">
        <div class="flex-1 relative">
          <Search class="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search agents..."
            value={searchQuery()}
            onInput={(e) => setSearchQuery(e.currentTarget.value)}
            class="
              w-full pl-10 pr-4 py-2
              bg-[var(--input-background)]
              border border-[var(--input-border)]
              rounded-[var(--radius-lg)]
              text-sm text-[var(--text-primary)]
              placeholder:text-[var(--text-muted)]
              focus:outline-none focus:border-[var(--accent)]
              transition-colors duration-[var(--duration-fast)]
            "
          />
        </div>

        <label class="flex items-center gap-2 text-sm text-[var(--text-secondary)] cursor-pointer">
          <input
            type="checkbox"
            checked={showCustomOnly()}
            onChange={(e) => setShowCustomOnly(e.currentTarget.checked)}
            class="sr-only"
          />
          <div
            class={`
              w-4 h-4 rounded border flex items-center justify-center
              transition-colors duration-[var(--duration-fast)]
              ${
                showCustomOnly()
                  ? 'bg-[var(--accent)] border-[var(--accent)]'
                  : 'border-[var(--border-default)]'
              }
            `}
          >
            <Show when={showCustomOnly()}>
              <Check class="w-3 h-3 text-white" />
            </Show>
          </div>
          Custom only
        </label>
      </div>

      {/* Agent List */}
      <div class="max-h-80 overflow-y-auto space-y-4 -mx-4 px-4">
        <Show
          when={filteredAgents().length > 0}
          fallback={
            <div class="py-8 text-center">
              <Bot class="w-10 h-10 mx-auto mb-3 text-[var(--text-muted)]" />
              <p class="text-sm text-[var(--text-secondary)]">No agents found</p>
              <Show when={showCustomOnly()}>
                <p class="text-xs text-[var(--text-muted)] mt-1">
                  Create a custom agent to get started
                </p>
              </Show>
            </div>
          }
        >
          {/* Custom Agents */}
          <Show when={groupedAgents().custom.length > 0}>
            <div>
              <h4 class="text-xs font-medium text-[var(--text-muted)] mb-2 flex items-center gap-2">
                <Sparkles class="w-3 h-3" />
                Custom Agents
              </h4>
              <div class="space-y-2">
                <For each={groupedAgents().custom}>
                  {(agent) => (
                    <AgentCard
                      agent={agent}
                      onToggle={(enabled) => props.onToggle?.(agent.id, enabled)}
                      onEdit={() => props.onEdit?.(agent.id)}
                      onDelete={() => props.onDelete?.(agent.id)}
                    />
                  )}
                </For>
              </div>
            </div>
          </Show>

          {/* Built-in Agents */}
          <Show when={groupedAgents().builtin.length > 0 && !showCustomOnly()}>
            <div>
              <h4 class="text-xs font-medium text-[var(--text-muted)] mb-2 flex items-center gap-2">
                <Bot class="w-3 h-3" />
                Built-in Agents
              </h4>
              <div class="space-y-2">
                <For each={groupedAgents().builtin}>
                  {(agent) => (
                    <AgentCard
                      agent={agent}
                      onToggle={(enabled) => props.onToggle?.(agent.id, enabled)}
                      onEdit={() => props.onEdit?.(agent.id)}
                    />
                  )}
                </For>
              </div>
            </div>
          </Show>
        </Show>
      </div>

      {/* Info */}
      <div class="flex items-start gap-3 p-3 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)]">
        <Bot class="w-5 h-5 text-[var(--info)] flex-shrink-0 mt-0.5" />
        <p class="text-sm text-[var(--text-secondary)]">
          Agents are specialized AI assistants with specific capabilities. Enable the agents you
          need for your workflow.
        </p>
      </div>
    </div>
  )
}

// ============================================================================
// Agent Card Component
// ============================================================================

interface AgentCardProps {
  agent: AgentPreset
  onToggle?: (enabled: boolean) => void
  onEdit?: () => void
  onDelete?: () => void
}

const AgentCard: Component<AgentCardProps> = (props) => {
  return (
    <div
      class={`
        flex items-center gap-3 p-3
        border rounded-[var(--radius-lg)]
        transition-colors duration-[var(--duration-fast)]
        ${
          props.agent.enabled
            ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
            : 'border-[var(--border-subtle)] hover:bg-[var(--surface-raised)]'
        }
      `}
    >
      {/* Icon */}
      <div
        class={`
          p-2 rounded-[var(--radius-md)]
          ${
            props.agent.enabled
              ? 'bg-[var(--accent)] text-white'
              : 'bg-[var(--surface-sunken)] text-[var(--text-muted)]'
          }
        `}
      >
        <Dynamic component={props.agent.icon || agentIcons.default} class="w-4 h-4" />
      </div>

      {/* Info */}
      <div class="flex-1 min-w-0">
        <div class="flex items-center gap-2">
          <span
            class={`text-sm font-medium ${
              props.agent.enabled ? 'text-[var(--accent)]' : 'text-[var(--text-primary)]'
            }`}
          >
            {props.agent.name}
          </span>
          <Show when={props.agent.isCustom}>
            <span class="px-1.5 py-0.5 text-[10px] font-medium rounded-full bg-[var(--info-subtle)] text-[var(--info)]">
              Custom
            </span>
          </Show>
          <Show when={props.agent.model}>
            <span class="px-1.5 py-0.5 text-[10px] font-medium rounded-full bg-[var(--surface-sunken)] text-[var(--text-muted)]">
              {props.agent.model}
            </span>
          </Show>
        </div>
        <div class="text-xs text-[var(--text-muted)] truncate">{props.agent.description}</div>
        <Show when={props.agent.capabilities.length > 0}>
          <div class="flex flex-wrap gap-1 mt-1.5">
            <For each={props.agent.capabilities.slice(0, 3)}>
              {(cap) => (
                <span class="px-1.5 py-0.5 text-[10px] bg-[var(--surface-sunken)] text-[var(--text-tertiary)] rounded">
                  {cap}
                </span>
              )}
            </For>
            <Show when={props.agent.capabilities.length > 3}>
              <span class="px-1.5 py-0.5 text-[10px] text-[var(--text-muted)]">
                +{props.agent.capabilities.length - 3} more
              </span>
            </Show>
          </div>
        </Show>
      </div>

      {/* Actions */}
      <div class="flex items-center gap-2">
        <Show when={props.onEdit}>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onEdit?.()
            }}
            class="
              p-1.5 rounded-[var(--radius-md)]
              text-[var(--text-muted)]
              hover:text-[var(--text-primary)]
              hover:bg-[var(--surface-raised)]
              transition-colors duration-[var(--duration-fast)]
            "
          >
            <Settings class="w-4 h-4" />
          </button>
        </Show>
        <Show when={props.agent.isCustom && props.onDelete}>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onDelete?.()
            }}
            class="
              p-1.5 rounded-[var(--radius-md)]
              text-[var(--text-muted)]
              hover:text-[var(--error)]
              hover:bg-[var(--error-subtle)]
              transition-colors duration-[var(--duration-fast)]
            "
          >
            <Trash2 class="w-4 h-4" />
          </button>
        </Show>
        <Toggle
          checked={props.agent.enabled}
          onChange={(checked) => props.onToggle?.(checked)}
          size="sm"
        />
      </div>
    </div>
  )
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
  },
  {
    id: 'git-assistant',
    name: 'Git Assistant',
    description: 'Helps with version control and git operations',
    icon: GitBranch as IconComponent,
    enabled: true,
    capabilities: ['git-status', 'commit-messages', 'branch-management', 'merge-resolution'],
  },
  {
    id: 'terminal-agent',
    name: 'Terminal Agent',
    description: 'Executes shell commands and manages processes',
    icon: Terminal as IconComponent,
    enabled: true,
    capabilities: ['command-execution', 'process-management', 'environment-setup'],
  },
  {
    id: 'documentation',
    name: 'Documentation Writer',
    description: 'Creates and maintains documentation',
    icon: FileText as IconComponent,
    enabled: false,
    capabilities: ['readme', 'api-docs', 'comments', 'tutorials'],
  },
  {
    id: 'fast-responder',
    name: 'Fast Responder',
    description: 'Quick responses for simple queries',
    icon: Zap as IconComponent,
    enabled: false,
    capabilities: ['quick-answers', 'simple-tasks'],
    model: 'claude-3-haiku',
  },
]
