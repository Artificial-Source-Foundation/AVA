import {
  Bot,
  CheckSquare,
  FileDiff,
  FolderOpen,
  GitCompareArrows,
  Route,
  Users,
  X,
} from 'lucide-solid'
import { createMemo, Show } from 'solid-js'
import { useRustAgent } from '../../hooks/use-rust-agent'
import { useAgent } from '../../hooks/useAgent'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { useTeam } from '../../stores/team'
import { AgentActivityPanel } from '../panels/AgentActivityPanel'
import { DiffReviewPanel } from '../panels/DiffReviewPanel'
import { FileOperationsPanel } from '../panels/FileOperationsPanel'
import { SessionDiffPanel } from '../panels/SessionDiffPanel'
import { TeamPanel } from '../panels/TeamPanel'
import { TodoPanel } from '../panels/TodoPanel'
import { TrajectoryInspector } from '../panels/TrajectoryInspector'
import { WorkerDetail } from '../panels/team/WorkerDetail'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'

interface RightPanelProps {
  startRightResize: (event: MouseEvent) => void
}

export function RightPanel(props: RightPanelProps) {
  const { settings } = useSettings()
  const { currentSession, messages } = useSession()
  const agent = useAgent()
  const team = useTeam()
  const rustAgent = useRustAgent()
  const {
    rightPanelVisible,
    rightPanelWidth,
    rightPanelTab,
    switchRightPanelTab,
    setRightPanelVisible,
  } = useLayout()

  const todoCount = createMemo(() => {
    const todos = rustAgent.todos()
    return todos.filter((t) => t.status === 'pending' || t.status === 'in_progress').length
  })

  /** Count of unique files modified in the current session (via tool calls) */
  const changesCount = createMemo(() => {
    const FILE_WRITE_TOOLS = new Set(['write', 'edit', 'apply_patch', 'multiedit'])
    const seen = new Set<string>()
    for (const msg of messages()) {
      if (!msg.toolCalls) continue
      for (const tc of msg.toolCalls) {
        if (!FILE_WRITE_TOOLS.has(tc.name)) continue
        if (tc.status !== 'success') continue
        const fp =
          tc.filePath ??
          (typeof tc.args.path === 'string' ? tc.args.path : null) ??
          (typeof tc.args.file_path === 'string' ? tc.args.file_path : null)
        if (fp) seen.add(fp)
      }
    }
    return seen.size
  })

  /** Stop all working team members */
  const handleStopAll = (): void => {
    for (const member of team.allMembers()) {
      if (member.status === 'working') {
        agent.stopAgent(member.id)
      }
    }
  }

  return (
    <Show when={settings().ui.showAgentActivity && rightPanelVisible()}>
      {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
      <div
        class="
          w-[3px] flex-shrink-0 cursor-col-resize
          bg-transparent hover:bg-[var(--accent-muted)]
          active:bg-[var(--accent)]
          transition-colors duration-150
        "
        onMouseDown={(event) => props.startRightResize(event)}
      />
      <div
        class="flex-shrink-0 overflow-hidden border-l border-[var(--border-subtle)]"
        style={{ width: `${rightPanelWidth()}px` }}
      >
        <div class="flex flex-col h-full bg-[var(--gray-1)]">
          <div class="flex items-center h-8 flex-shrink-0 border-b border-[var(--border-subtle)]">
            <button
              type="button"
              onClick={() => switchRightPanelTab('activity')}
              class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
              classList={{
                'text-[var(--accent)] border-b border-[var(--accent)]':
                  rightPanelTab() === 'activity',
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                  rightPanelTab() !== 'activity',
              }}
            >
              <Bot class="w-3 h-3" />
              Activity
            </button>
            <button
              type="button"
              onClick={() => switchRightPanelTab('files')}
              class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
              classList={{
                'text-[var(--accent)] border-b border-[var(--accent)]': rightPanelTab() === 'files',
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                  rightPanelTab() !== 'files',
              }}
            >
              <FolderOpen class="w-3 h-3" />
              Files
            </button>
            <button
              type="button"
              onClick={() => switchRightPanelTab('review')}
              class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
              classList={{
                'text-[var(--accent)] border-b border-[var(--accent)]':
                  rightPanelTab() === 'review',
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                  rightPanelTab() !== 'review',
              }}
            >
              <GitCompareArrows class="w-3 h-3" />
              Review
            </button>
            <button
              type="button"
              onClick={() => switchRightPanelTab('trajectory')}
              class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
              classList={{
                'text-[var(--accent)] border-b border-[var(--accent)]':
                  rightPanelTab() === 'trajectory',
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                  rightPanelTab() !== 'trajectory',
              }}
            >
              <Route class="w-3 h-3" />
              Trajectory
            </button>
            <Show when={settings().generation.delegationEnabled || team.hierarchy() !== null}>
              <button
                type="button"
                onClick={() => switchRightPanelTab('team')}
                class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                classList={{
                  'text-[var(--accent)] border-b border-[var(--accent)]':
                    rightPanelTab() === 'team',
                  'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                    rightPanelTab() !== 'team',
                }}
              >
                <Users class="w-3 h-3" />
                Team
              </button>
            </Show>
            <button
              type="button"
              onClick={() => switchRightPanelTab('todos')}
              class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
              classList={{
                'text-[var(--accent)] border-b border-[var(--accent)]': rightPanelTab() === 'todos',
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                  rightPanelTab() !== 'todos',
              }}
            >
              <CheckSquare class="w-3 h-3" />
              Todos
              <Show when={todoCount() > 0}>
                <span class="ml-1 px-1 py-0.5 rounded-full bg-[var(--accent-muted)] text-[var(--accent)] text-[9px] font-bold leading-none">
                  {todoCount()}
                </span>
              </Show>
            </button>
            <button
              type="button"
              onClick={() => switchRightPanelTab('changes')}
              class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
              classList={{
                'text-[var(--accent)] border-b border-[var(--accent)]':
                  rightPanelTab() === 'changes',
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                  rightPanelTab() !== 'changes',
              }}
            >
              <FileDiff class="w-3 h-3" />
              Changes
              <Show when={changesCount() > 0}>
                <span class="ml-1 px-1 py-0.5 rounded-full bg-[var(--accent-muted)] text-[var(--accent)] text-[9px] font-bold leading-none">
                  {changesCount()}
                </span>
              </Show>
            </button>
            <div class="flex-1" />
            <button
              type="button"
              onClick={() => setRightPanelVisible(false)}
              class="p-1 mr-1.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
              aria-label="Close right panel"
            >
              <X class="w-3 h-3" />
            </button>
          </div>

          <div class="flex-1 overflow-hidden">
            <Show when={rightPanelTab() === 'activity'}>
              <PanelErrorBoundary panelName="Agent Activity">
                <AgentActivityPanel compact />
              </PanelErrorBoundary>
            </Show>
            <Show when={rightPanelTab() === 'files'}>
              <PanelErrorBoundary panelName="File Operations">
                <FileOperationsPanel compact />
              </PanelErrorBoundary>
            </Show>
            <Show when={rightPanelTab() === 'review'}>
              <PanelErrorBoundary panelName="Diff Review">
                <DiffReviewPanel />
              </PanelErrorBoundary>
            </Show>
            <Show when={rightPanelTab() === 'trajectory'}>
              <PanelErrorBoundary panelName="Trajectory Inspector">
                <TrajectoryInspector sessionId={currentSession()?.id ?? 'unknown'} />
              </PanelErrorBoundary>
            </Show>
            <Show when={rightPanelTab() === 'team'}>
              <PanelErrorBoundary panelName="Team">
                <Show
                  when={!team.selectedMember() || team.selectedMember()?.role === 'team-lead'}
                  fallback={<WorkerDetail member={team.selectedMember()!} />}
                >
                  <TeamPanel onStopAgent={(id) => agent.stopAgent(id)} onStopAll={handleStopAll} />
                </Show>
              </PanelErrorBoundary>
            </Show>
            <Show when={rightPanelTab() === 'todos'}>
              <PanelErrorBoundary panelName="Todos">
                <TodoPanel />
              </PanelErrorBoundary>
            </Show>
            <Show when={rightPanelTab() === 'changes'}>
              <PanelErrorBoundary panelName="Session Changes">
                <SessionDiffPanel />
              </PanelErrorBoundary>
            </Show>
          </div>
        </div>
      </div>
    </Show>
  )
}
