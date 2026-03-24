import {
  Bot,
  CheckSquare,
  FileDiff,
  FolderOpen,
  GitCompareArrows,
  MoreHorizontal,
  Route,
  Users,
  X,
} from 'lucide-solid'
import { createMemo, createSignal, Show } from 'solid-js'
import { useRustAgent } from '../../hooks/use-rust-agent'
import { useAgent } from '../../hooks/useAgent'
import type { RightPanelTab } from '../../stores/layout'
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

// ---------------------------------------------------------------------------
// Primary tabs (always visible) + overflow tabs (behind "..." menu)
// ---------------------------------------------------------------------------

const ALL_PANEL_TABS: { id: RightPanelTab; icon: typeof Bot; label: string; primary: boolean }[] = [
  { id: 'activity', icon: Bot, label: 'Activity', primary: true },
  { id: 'changes', icon: FileDiff, label: 'Changes', primary: true },
  { id: 'todos', icon: CheckSquare, label: 'Todos', primary: true },
  { id: 'files', icon: FolderOpen, label: 'Files', primary: false },
  { id: 'review', icon: GitCompareArrows, label: 'Review', primary: false },
  { id: 'trajectory', icon: Route, label: 'Trajectory', primary: false },
  { id: 'team', icon: Users, label: 'Team', primary: false },
]

interface TabBarProps {
  currentTab: RightPanelTab
  onSwitch: (tab: RightPanelTab) => void
  onClose: () => void
  todoCount: number
  changesCount: number
  showTeam: boolean
}

function TabBar(props: TabBarProps) {
  const [moreOpen, setMoreOpen] = createSignal(false)
  let moreRef: HTMLDivElement | undefined

  const handleClickOutside = (e: MouseEvent) => {
    if (moreRef && !moreRef.contains(e.target as Node)) setMoreOpen(false)
  }

  const primaryTabs = () => ALL_PANEL_TABS.filter((t) => t.primary)
  const overflowTabs = () =>
    ALL_PANEL_TABS.filter((t) => !t.primary && (t.id !== 'team' || props.showTeam))

  const isOverflowActive = () => overflowTabs().some((t) => t.id === props.currentTab)

  const badge = (tabId: RightPanelTab) => {
    if (tabId === 'todos' && props.todoCount > 0) return props.todoCount
    if (tabId === 'changes' && props.changesCount > 0) return props.changesCount
    return 0
  }

  return (
    <div class="flex items-center h-8 flex-shrink-0 border-b border-[var(--border-subtle)]">
      {/* Primary tabs — icon-only with tooltip, always fit */}
      <div class="flex items-center flex-1 min-w-0">
        {primaryTabs().map((tab) => {
          const Icon = tab.icon
          const count = () => badge(tab.id)
          return (
            <button
              type="button"
              onClick={() => props.onSwitch(tab.id)}
              class="relative flex items-center justify-center w-8 h-8 transition-colors flex-shrink-0"
              classList={{
                'text-[var(--accent)] border-b-2 border-[var(--accent)]':
                  props.currentTab === tab.id,
                'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                  props.currentTab !== tab.id,
              }}
              title={tab.label}
            >
              <Icon class="w-4 h-4" />
              <Show when={count() > 0}>
                <span class="absolute -top-0.5 -right-0.5 min-w-[14px] h-[14px] px-0.5 flex items-center justify-center rounded-full bg-[var(--accent)] text-white text-[8px] font-bold leading-none">
                  {count()}
                </span>
              </Show>
            </button>
          )
        })}

        {/* More dropdown */}
        <div ref={moreRef} class="relative h-full flex-shrink-0">
          <button
            type="button"
            onClick={() => {
              setMoreOpen(!moreOpen())
              if (!moreOpen()) {
                document.addEventListener('click', handleClickOutside, { once: true })
              }
            }}
            class="flex items-center justify-center w-8 h-8 transition-colors"
            classList={{
              'text-[var(--accent)] border-b-2 border-[var(--accent)]': isOverflowActive(),
              'text-[var(--text-muted)] hover:text-[var(--text-secondary)]': !isOverflowActive(),
            }}
            title="More tabs"
          >
            <MoreHorizontal class="w-4 h-4" />
          </button>

          <Show when={moreOpen()}>
            <div class="absolute top-full right-0 mt-1 min-w-[140px] py-1 bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-md)] shadow-lg z-50">
              {overflowTabs().map((tab) => {
                const Icon = tab.icon
                return (
                  <button
                    type="button"
                    onClick={() => {
                      props.onSwitch(tab.id)
                      setMoreOpen(false)
                    }}
                    class="w-full flex items-center gap-2 px-3 py-1.5 text-[var(--text-xs)] transition-colors"
                    classList={{
                      'text-[var(--accent)] bg-[var(--alpha-white-5)]': props.currentTab === tab.id,
                      'text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)]':
                        props.currentTab !== tab.id,
                    }}
                  >
                    <Icon class="w-3.5 h-3.5" />
                    {tab.label}
                  </button>
                )
              })}
            </div>
          </Show>
        </div>
      </div>

      {/* Close button — always visible */}
      <button
        type="button"
        onClick={props.onClose}
        class="flex items-center justify-center w-8 h-8 flex-shrink-0 text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
        aria-label="Close panel"
        title="Close panel"
      >
        <X class="w-3.5 h-3.5" />
      </button>
    </div>
  )
}

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
          <TabBar
            currentTab={rightPanelTab()}
            onSwitch={switchRightPanelTab}
            onClose={() => setRightPanelVisible(false)}
            todoCount={todoCount()}
            changesCount={changesCount()}
            showTeam={settings().generation.delegationEnabled || team.hierarchy() !== null}
          />

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
                <TodoPanel todos={rustAgent.todos()} />
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
