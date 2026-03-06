import { Bot, FolderOpen, GitCompareArrows, Route, X } from 'lucide-solid'
import { Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { AgentActivityPanel } from '../panels/AgentActivityPanel'
import { DiffReviewPanel } from '../panels/DiffReviewPanel'
import { FileOperationsPanel } from '../panels/FileOperationsPanel'
import { TrajectoryInspector } from '../panels/TrajectoryInspector'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'

interface RightPanelProps {
  startRightResize: (event: MouseEvent) => void
}

export function RightPanel(props: RightPanelProps) {
  const { settings } = useSettings()
  const { currentSession } = useSession()
  const {
    rightPanelVisible,
    rightPanelWidth,
    rightPanelTab,
    switchRightPanelTab,
    setRightPanelVisible,
  } = useLayout()

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
          </div>
        </div>
      </div>
    </Show>
  )
}
