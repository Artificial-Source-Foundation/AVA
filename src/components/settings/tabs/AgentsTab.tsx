/**
 * Agents Settings Tab
 *
 * Modern 2026 aesthetic using semantic CSS tokens.
 * Configure AI agent behavior, presets, and custom agents.
 */

import {
  Bot,
  Check,
  ChevronRight,
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
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
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
  type?: 'coding' | 'git' | 'terminal' | 'docs' | 'fast' | 'custom'
}

export interface AgentsTabProps {
  agents: AgentPreset[]
  onToggle?: (id: string, enabled: boolean) => void
  onEdit?: (id: string) => void
  onDelete?: (id: string) => void
  onCreate?: () => void
}

// ============================================================================
// Agent Type Configuration (uses CSS variables from tokens.css)
// ============================================================================

const agentTypeConfig: Record<
  string,
  { icon: IconComponent; colorVar: string; subtleVar: string }
> = {
  coding: {
    icon: Code as IconComponent,
    colorVar: '--agent-coding',
    subtleVar: '--agent-coding-subtle',
  },
  git: {
    icon: GitBranch as IconComponent,
    colorVar: '--agent-git',
    subtleVar: '--agent-git-subtle',
  },
  terminal: {
    icon: Terminal as IconComponent,
    colorVar: '--agent-terminal',
    subtleVar: '--agent-terminal-subtle',
  },
  docs: {
    icon: FileText as IconComponent,
    colorVar: '--agent-docs',
    subtleVar: '--agent-docs-subtle',
  },
  fast: {
    icon: Zap as IconComponent,
    colorVar: '--agent-fast',
    subtleVar: '--agent-fast-subtle',
  },
  custom: {
    icon: Wand2 as IconComponent,
    colorVar: '--agent-custom',
    subtleVar: '--agent-custom-subtle',
  },
  default: {
    icon: Bot as IconComponent,
    colorVar: '--text-muted',
    subtleVar: '--alpha-white-5',
  },
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
          <h3 class="text-[var(--text-lg)] font-semibold text-[var(--text-primary)]">AI Agents</h3>
          <p class="text-[var(--text-xs)] text-[var(--text-tertiary)] mt-[var(--space-0_5)]">
            {enabledCount()} of {props.agents.length} agents enabled
          </p>
        </div>
        <Show when={props.onCreate}>
          <button
            type="button"
            onClick={() => props.onCreate?.()}
            class="
              flex items-center gap-[var(--space-1_5)] px-[var(--space-3)] py-[var(--space-1_5)]
              bg-[var(--button-primary-bg)] hover:bg-[var(--button-primary-hover)]
              text-[var(--button-primary-text)] text-[var(--text-sm)] font-medium
              rounded-[var(--radius-lg)]
              transition-colors duration-[var(--duration-fast)]
            "
          >
            <Plus class="w-4 h-4" />
            Create Agent
          </button>
        </Show>
      </div>

      {/* Search and Filter */}
      <div class="flex items-center gap-[var(--space-3)]">
        <div class="flex-1 relative">
          <Search class="absolute left-[var(--space-3)] top-1/2 -translate-y-1/2 w-4 h-4 text-[var(--text-muted)]" />
          <input
            type="text"
            placeholder="Search agents..."
            value={searchQuery()}
            onInput={(e) => setSearchQuery(e.currentTarget.value)}
            class="
              w-full pl-[var(--space-10)] pr-[var(--space-4)] py-[var(--space-2_5)]
              bg-[var(--input-background)]
              border border-[var(--input-border)]
              rounded-[var(--radius-lg)]
              text-[var(--text-sm)] text-[var(--text-primary)]
              placeholder:text-[var(--input-placeholder)]
              focus:outline-none focus:border-[var(--input-border-focus)]
              focus:shadow-[0_0_0_3px_var(--input-focus-ring)]
              transition-colors duration-[var(--duration-fast)]
            "
          />
        </div>

        <button
          type="button"
          class="flex items-center gap-[var(--space-2)] text-[var(--text-sm)] text-[var(--text-secondary)] cursor-pointer select-none"
          onClick={() => setShowCustomOnly(!showCustomOnly())}
        >
          <div
            class={`
              w-4 h-4 rounded-[var(--radius-sm)] border flex items-center justify-center
              transition-colors duration-[var(--duration-fast)]
              ${
                showCustomOnly()
                  ? 'bg-[var(--accent)] border-[var(--accent)]'
                  : 'border-[var(--border-strong)] hover:border-[var(--alpha-white-25)]'
              }
            `}
          >
            <Show when={showCustomOnly()}>
              <Check class="w-3 h-3 text-white" />
            </Show>
          </div>
          Custom only
        </button>
      </div>

      {/* Agent List */}
      <div class="space-y-[var(--space-4)]">
        <Show
          when={filteredAgents().length > 0}
          fallback={
            <div class="py-[var(--space-12)] text-center">
              <div class="w-12 h-12 mx-auto mb-[var(--space-3)] rounded-full bg-[var(--alpha-white-5)] flex items-center justify-center">
                <Bot class="w-6 h-6 text-[var(--text-muted)]" />
              </div>
              <p class="text-[var(--text-sm)] text-[var(--text-secondary)]">No agents found</p>
              <Show when={showCustomOnly()}>
                <p class="text-[var(--text-xs)] text-[var(--text-muted)] mt-[var(--space-1)]">
                  Create a custom agent to get started
                </p>
              </Show>
            </div>
          }
        >
          {/* Custom Agents */}
          <Show when={groupedAgents().custom.length > 0}>
            <div>
              <h4 class="text-[var(--text-xs)] font-medium text-[var(--text-muted)] mb-[var(--space-3)] flex items-center gap-[var(--space-2)] uppercase tracking-wider">
                <Sparkles class="w-3 h-3" />
                Custom Agents
              </h4>
              <div class="space-y-[var(--space-2)]">
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
              <h4 class="text-[var(--text-xs)] font-medium text-[var(--text-muted)] mb-[var(--space-3)] flex items-center gap-[var(--space-2)] uppercase tracking-wider">
                <Bot class="w-3 h-3" />
                Built-in Agents
              </h4>
              <div class="space-y-[var(--space-2)]">
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

      {/* Info Banner */}
      <div
        class="
          flex items-start gap-[var(--space-3)] p-[var(--space-4)]
          bg-[var(--info-subtle)]
          border border-[var(--info-border)]
          rounded-[var(--radius-xl)]
        "
      >
        <div class="w-8 h-8 rounded-full bg-[var(--info-subtle)] flex items-center justify-center flex-shrink-0">
          <Bot class="w-4 h-4 text-[var(--info)]" />
        </div>
        <p class="text-[var(--text-sm)] text-[var(--text-secondary)] leading-relaxed">
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
  const config = createMemo(() => {
    const type = props.agent.type || (props.agent.isCustom ? 'custom' : 'default')
    return agentTypeConfig[type] || agentTypeConfig.default
  })

  return (
    <div
      class={`
        relative overflow-hidden
        rounded-[var(--radius-xl)]
        border transition-colors duration-[var(--duration-fast)]
        ${
          props.agent.enabled
            ? 'border-[var(--accent-border)] bg-[var(--accent-subtle)]'
            : 'border-[var(--card-border)] bg-[var(--card-background)] hover:border-[var(--card-hover-border)]'
        }
      `}
    >
      <div class="relative flex items-center gap-[var(--space-3)] p-[var(--space-3)]">
        {/* Icon with colored background */}
        <div
          class="p-[var(--space-2_5)] rounded-[var(--radius-lg)] transition-colors duration-[var(--duration-fast)]"
          style={{
            background: props.agent.enabled ? `var(${config().subtleVar})` : 'var(--alpha-white-5)',
          }}
        >
          <span
            style={{
              color: props.agent.enabled ? `var(${config().colorVar})` : 'var(--text-muted)',
            }}
          >
            <Dynamic component={config().icon} class="w-4 h-4" />
          </span>
        </div>

        {/* Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-[var(--space-2)]">
            <span
              class={`text-[var(--text-sm)] font-medium transition-colors duration-[var(--duration-fast)] ${
                props.agent.enabled ? 'text-[var(--text-primary)]' : 'text-[var(--text-secondary)]'
              }`}
            >
              {props.agent.name}
            </span>
            {/* Status dot */}
            <div
              class={`w-2 h-2 rounded-full transition-colors duration-[var(--duration-fast)] ${
                props.agent.enabled ? 'bg-[var(--success)]' : 'bg-[var(--text-muted)]'
              }`}
            />
            <Show when={props.agent.isCustom}>
              <span class="px-[var(--space-1_5)] py-[var(--space-0_5)] text-[10px] font-medium rounded-full bg-[var(--agent-custom-subtle)] text-[var(--agent-custom)]">
                Custom
              </span>
            </Show>
          </div>
          <div class="text-[var(--text-xs)] text-[var(--text-muted)] truncate mt-[var(--space-0_5)]">
            {props.agent.description}
          </div>
          <Show when={props.agent.capabilities.length > 0}>
            <div class="flex flex-wrap gap-[var(--space-1)] mt-[var(--space-2)]">
              <For each={props.agent.capabilities.slice(0, 3)}>
                {(cap) => (
                  <span class="px-[var(--space-1_5)] py-[var(--space-0_5)] text-[10px] bg-[var(--alpha-white-5)] text-[var(--text-tertiary)] rounded-[var(--radius-sm)]">
                    {cap}
                  </span>
                )}
              </For>
              <Show when={props.agent.capabilities.length > 3}>
                <span class="px-[var(--space-1_5)] py-[var(--space-0_5)] text-[10px] text-[var(--text-muted)]">
                  +{props.agent.capabilities.length - 3}
                </span>
              </Show>
            </div>
          </Show>
        </div>

        {/* Actions */}
        <div class="flex items-center gap-[var(--space-1)]">
          <Show when={props.onEdit}>
            <button
              type="button"
              onClick={(e) => {
                e.stopPropagation()
                props.onEdit?.()
              }}
              class="
                p-[var(--space-1_5)] rounded-[var(--radius-md)]
                text-[var(--text-muted)]
                hover:text-[var(--text-primary)]
                hover:bg-[var(--button-ghost-hover)]
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
                p-[var(--space-1_5)] rounded-[var(--radius-md)]
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
          <ChevronRight class="w-4 h-4 text-[var(--text-muted)] ml-[var(--space-1)]" />
        </div>
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
