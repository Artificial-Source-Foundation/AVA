/**
 * Dashboard / Home Screen
 *
 * Shows stats, recent projects, current project info, and connected providers.
 * Matches the Pencil design node 5glp7.
 */

import { FolderOpen, FolderPlus, GitBranch, MessageSquare, Plus, Sparkles } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import type { ProjectWithStats } from '../../types'

// ============================================================================
// Helpers
// ============================================================================

function formatLastActive(timestamp: number | undefined): string {
  if (!timestamp) return 'never'
  const minutes = Math.max(1, Math.floor((Date.now() - timestamp) / 60_000))
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return `${hours}h ago`
  const days = Math.floor(hours / 24)
  if (days === 1) return 'Yesterday'
  if (days < 30) return `${days} days ago`
  const months = Math.floor(days / 30)
  return `${months}mo ago`
}

function shortenPath(directory: string): string {
  return directory.replace(/^\/home\/[^/]+/, '~')
}

function formatTokens(tokens: number): string {
  if (tokens >= 1_000_000) return `${(tokens / 1_000_000).toFixed(1)}M`
  if (tokens >= 1_000) return `${(tokens / 1_000).toFixed(1)}K`
  return String(tokens)
}

function formatCost(cost: number): string {
  return `$${cost.toFixed(2)}`
}

function collectEditedFiles(
  messages: Array<{ toolCalls?: { filePath?: string; args: Record<string, unknown> }[] }>
): number {
  const files = new Set<string>()
  for (const message of messages) {
    for (const toolCall of message.toolCalls ?? []) {
      const filePath =
        toolCall.filePath ??
        (typeof toolCall.args.path === 'string' ? toolCall.args.path : undefined) ??
        (typeof toolCall.args.file_path === 'string' ? toolCall.args.file_path : undefined)
      if (filePath) files.add(filePath)
    }
  }
  return files.size
}

function formatDate(): string {
  return new Date().toLocaleDateString('en-US', {
    month: 'short',
    day: 'numeric',
    year: 'numeric',
  })
}

// ============================================================================
// Stat Card
// ============================================================================

interface StatCardProps {
  label: string
  value: string
}

const StatCard: Component<StatCardProps> = (props) => (
  <div class="ui-surface-card flex min-w-0 flex-1 flex-col gap-1.5 px-[18px] py-4">
    <span class="text-[11px] text-[var(--text-muted)]">{props.label}</span>
    <span class="text-[28px] font-semibold leading-none text-[var(--text-primary)]">
      {props.value}
    </span>
  </div>
)

// ============================================================================
// Project Row (Recent Projects list)
// ============================================================================

interface ProjectRowProps {
  project: ProjectWithStats
  isActive: boolean
  onClick: () => void
}

const ProjectRow: Component<ProjectRowProps> = (props) => (
  <button
    type="button"
    onClick={() => props.onClick()}
    class="flex w-full items-center justify-between text-left transition-colors hover:bg-[var(--surface-raised)]"
    style={{
      padding: '14px 16px',
      background: 'var(--surface)',
      'border-radius': '10px',
      border: props.isActive ? '1px solid var(--accent-border)' : '1px solid var(--border-subtle)',
      cursor: 'pointer',
    }}
  >
    {/* Left: icon + info */}
    <div class="flex items-center gap-3 min-w-0">
      <div
        class="flex items-center justify-center flex-shrink-0"
        style={{
          width: '32px',
          height: '32px',
          'border-radius': '8px',
          background: props.isActive ? 'var(--accent-subtle)' : 'var(--alpha-white-5)',
        }}
      >
        <FolderOpen
          style={{
            width: '14px',
            height: '14px',
            color: props.isActive ? 'var(--accent)' : 'var(--text-muted)',
          }}
        />
      </div>
      <div class="flex flex-col min-w-0" style={{ gap: '2px' }}>
        <span
          class="truncate"
          style={{
            'font-size': '14px',
            'font-weight': props.isActive ? '500' : '400',
            color: props.isActive ? 'var(--text-primary)' : 'var(--text-secondary)',
            'font-family': 'Geist, sans-serif',
          }}
        >
          {props.project.name}
        </span>
        <span
          class="truncate"
          style={{
            'font-size': '10px',
            color: 'var(--text-muted)',
            'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
          }}
        >
          {shortenPath(props.project.directory)}
        </span>
      </div>
    </div>

    {/* Right: badge + time */}
    <div class="flex items-center gap-2 flex-shrink-0">
      <Show when={props.isActive}>
        <span
          style={{
            padding: '2px 6px',
            'border-radius': '4px',
            background: 'var(--accent-subtle)',
            'font-size': '10px',
            'font-weight': '500',
            color: 'var(--accent)',
            'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
          }}
        >
          Active
        </span>
      </Show>
      <span
        style={{
          'font-size': '10px',
          color: 'var(--text-muted)',
          'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
        }}
      >
        {formatLastActive(props.project.lastOpenedAt)}
      </span>
    </div>
  </button>
)

// ============================================================================
// Provider Row
// ============================================================================

interface ProviderRowProps {
  name: string
  model: string
  connected: boolean
}

const ProviderRow: Component<ProviderRowProps> = (props) => (
  <div
    class="flex items-center justify-between"
    style={{
      padding: '10px 14px',
      background: 'var(--surface)',
      'border-radius': '8px',
      border: '1px solid var(--border-subtle)',
    }}
  >
    <div class="flex items-center gap-2">
      <div
        class="flex-shrink-0 rounded-full"
        style={{
          width: '6px',
          height: '6px',
          background: props.connected ? 'var(--success)' : 'var(--text-muted)',
        }}
      />
      <span
        style={{
          'font-size': '13px',
          color: 'var(--text-secondary)',
          'font-family': 'Geist, sans-serif',
        }}
      >
        {props.name}
      </span>
    </div>
    <span
      style={{
        'font-size': '10px',
        color: 'var(--text-muted)',
        'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
      }}
    >
      {props.model}
    </span>
  </div>
)

// ============================================================================
// Dashboard View
// ============================================================================

export const DashboardView: Component = () => {
  const { currentProject, projects, switchProject } = useProject()
  const {
    createNewSession,
    sessions,
    selectedModel,
    loadSessionsForCurrentProject,
    messages,
    sessionTokenStats,
  } = useSession()
  const { closeDashboard, openProjectHub } = useLayout()
  const { settings } = useSettings()

  // Simple stats from session store — no async resource needed
  const recentProjects = createMemo(() => {
    try {
      const all = projects()
      if (!Array.isArray(all)) return []
      return all.filter((p) => p.id !== 'default-project').slice(0, 8)
    } catch {
      return []
    }
  })

  const connectedProviders = createMemo(() =>
    settings().providers.filter((p) => p.status === 'connected' && p.enabled)
  )

  const projectName = createMemo(() => currentProject()?.name || 'Project')
  const projectPath = createMemo(() => shortenPath(currentProject()?.directory || '~'))
  const projectBranch = createMemo(() => currentProject()?.git?.branch || 'main')

  const handleNewChat = async (): Promise<void> => {
    await createNewSession()
    closeDashboard()
  }

  const handleSwitchProject = async (project: ProjectWithStats): Promise<void> => {
    await switchProject(project.id)
    await loadSessionsForCurrentProject()
    closeDashboard()
  }

  const handleOpenAnother = (): void => {
    closeDashboard()
    openProjectHub()
  }

  const currentProjectStats = createMemo(
    () => recentProjects().find((project) => project.id === currentProject()?.id) ?? null
  )

  const totalSessions = createMemo(() =>
    String(currentProjectStats()?.sessionCount ?? sessions().length)
  )
  const projectTokens = createMemo(() =>
    formatTokens(sessions().reduce((sum, session) => sum + session.totalTokens, 0))
  )
  const filesEdited = createMemo(() => String(collectEditedFiles(messages())))
  const currentSessionCost = createMemo(() => formatCost(sessionTokenStats().totalCost))

  const modelDisplay = createMemo(() => {
    const model = selectedModel()
    if (!model) return 'No model'
    // Shorten common model names
    return model.replace('claude-', '').replace('gpt-', 'GPT-').split('/').pop() || model
  })

  return (
    <div class="flex h-full w-full flex-col bg-[var(--background)]">
      {/* Title Bar — 52px */}
      <div
        class="flex items-center justify-between flex-shrink-0"
        style={{
          height: '52px',
          padding: '0 28px',
          'border-bottom': '1px solid var(--border-subtle)',
        }}
      >
        <span class="text-base font-semibold text-[var(--text-primary)]">Home</span>
        <span class="font-ui-mono text-[11px] text-[var(--text-muted)]">{formatDate()}</span>
      </div>

      {/* Content */}
      <div
        class="flex-1 overflow-y-auto"
        style={{
          padding: '28px 32px',
          gap: '28px',
          display: 'flex',
          'flex-direction': 'column',
        }}
      >
        {/* Stats Row */}
        <div class="flex gap-3.5" style={{ 'flex-shrink': '0' }}>
          <StatCard label="Project Sessions" value={totalSessions()} />
          <StatCard label="Project Tokens" value={projectTokens()} />
          <StatCard label="Edited Files" value={filesEdited()} />
          <StatCard label="Current Session Cost" value={currentSessionCost()} />
        </div>

        {/* Two-Column Layout */}
        <div class="flex gap-5 flex-1 min-h-0">
          {/* Left — Recent Projects */}
          <div class="flex flex-1 min-w-0 flex-col gap-[14px]">
            <span class="text-[14px] font-medium text-[var(--text-primary)]">Recent Projects</span>
            <div class="flex flex-col flex-1 min-h-0 overflow-y-auto" style={{ gap: '6px' }}>
              <For each={recentProjects()}>
                {(project) => (
                  <ProjectRow
                    project={project}
                    isActive={currentProject()?.id === project.id}
                    onClick={() => handleSwitchProject(project)}
                  />
                )}
              </For>
              <Show when={recentProjects().length === 0}>
                <div class="flex items-center justify-center py-12 text-[13px] text-[var(--text-muted)]">
                  No projects yet. Open a directory to get started.
                </div>
              </Show>
            </div>
          </div>

          {/* Right — Current Project (340px) */}
          <div class="flex flex-shrink-0 flex-col gap-5" style={{ width: '340px' }}>
            {/* Section label */}
            <span class="text-[14px] font-medium text-[var(--text-primary)]">Current Project</span>

            {/* Project card */}
            <div class="ui-surface-card flex flex-col gap-[14px] p-[18px]">
              {/* Header: icon + name + path */}
              <div class="flex items-center gap-2.5">
                <div
                  class="flex items-center justify-center flex-shrink-0"
                  style={{
                    width: '36px',
                    height: '36px',
                    'border-radius': '8px',
                    background: 'var(--accent-subtle)',
                  }}
                >
                  <FolderOpen style={{ width: '18px', height: '18px', color: 'var(--accent)' }} />
                </div>
                <div class="flex flex-col min-w-0" style={{ gap: '2px' }}>
                  <span
                    class="truncate"
                    style={{
                      'font-size': '16px',
                      'font-weight': '600',
                      color: 'var(--text-primary)',
                      'font-family': 'Geist, sans-serif',
                    }}
                  >
                    {projectName()}
                  </span>
                  <span
                    class="truncate"
                    style={{
                      'font-size': '10px',
                      color: 'var(--text-muted)',
                      'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
                    }}
                  >
                    {projectPath()}
                  </span>
                </div>
              </div>

              {/* Metadata pills */}
              <div class="flex flex-wrap gap-2">
                {/* Branch */}
                <div class="ui-pill">
                  <GitBranch
                    style={{ width: '10px', height: '10px', color: 'var(--text-muted)' }}
                  />
                  <span
                    style={{
                      'font-size': '10px',
                      color: 'var(--text-muted)',
                      'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
                    }}
                  >
                    {projectBranch()}
                  </span>
                </div>
                {/* Model */}
                <div class="ui-pill ui-pill--accent">
                  <Sparkles style={{ width: '10px', height: '10px', color: 'var(--accent)' }} />
                  <span
                    style={{
                      'font-size': '10px',
                      color: 'var(--text-tertiary)',
                      'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
                    }}
                  >
                    {modelDisplay()}
                  </span>
                </div>
                {/* Sessions */}
                <div class="ui-pill">
                  <MessageSquare
                    style={{ width: '10px', height: '10px', color: 'var(--text-muted)' }}
                  />
                  <span
                    style={{
                      'font-size': '10px',
                      color: 'var(--text-muted)',
                      'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
                    }}
                  >
                    {sessions().length} session{sessions().length !== 1 ? 's' : ''}
                  </span>
                </div>
              </div>

              {/* Divider */}
              <div class="settings-divider" />

              {/* Actions */}
              <div class="flex flex-col" style={{ gap: '6px' }}>
                {/* Primary: New Chat */}
                <button
                  type="button"
                  onClick={() => void handleNewChat()}
                  class="flex w-full items-center justify-center gap-1.5 transition-colors hover:bg-[var(--accent-hover)]"
                  style={{
                    height: '36px',
                    'border-radius': '8px',
                    background: 'var(--accent)',
                    padding: '0 14px',
                    cursor: 'pointer',
                    border: 'none',
                  }}
                >
                  <Plus style={{ width: '14px', height: '14px', color: 'var(--text-on-accent)' }} />
                  <span
                    style={{
                      'font-size': '13px',
                      'font-weight': '500',
                      color: 'var(--text-on-accent)',
                      'font-family': 'Geist, sans-serif',
                    }}
                  >
                    New Chat in {projectName()}
                  </span>
                </button>
                {/* Secondary: Open Another */}
                <button
                  type="button"
                  onClick={handleOpenAnother}
                  class="flex w-full items-center justify-center gap-1.5 transition-colors hover:bg-[var(--alpha-white-10)]"
                  style={{
                    height: '36px',
                    'border-radius': '8px',
                    background: 'var(--alpha-white-8)',
                    border: '1px solid var(--border-default)',
                    padding: '0 14px',
                    cursor: 'pointer',
                  }}
                >
                  <FolderPlus
                    style={{ width: '14px', height: '14px', color: 'var(--text-secondary)' }}
                  />
                  <span
                    style={{
                      'font-size': '13px',
                      color: 'var(--text-secondary)',
                      'font-family': 'Geist, sans-serif',
                    }}
                  >
                    Open Another Project
                  </span>
                </button>
              </div>
            </div>

            {/* Connected Providers */}
            <div class="flex flex-col gap-[10px]">
              <span class="text-[14px] font-medium text-[var(--text-primary)]">
                Connected Providers
              </span>
              <For each={connectedProviders()}>
                {(provider) => (
                  <ProviderRow
                    name={provider.name}
                    model={
                      provider.models.find((m) => m.isDefault)?.name ||
                      provider.models[0]?.name ||
                      ''
                    }
                    connected={true}
                  />
                )}
              </For>
              <Show when={connectedProviders().length === 0}>
                <div class="flex items-center justify-center py-6 text-[12px] text-[var(--text-muted)]">
                  No providers connected
                </div>
              </Show>
            </div>
          </div>
        </div>
      </div>
    </div>
  )
}
