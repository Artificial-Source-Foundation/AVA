import {
  Bot,
  Brain,
  FolderOpen,
  GitCompareArrows,
  Route,
  ScrollText,
  Terminal,
  X,
} from 'lucide-solid'
import { type Component, lazy, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { useSession } from '../../stores/session'
import { useSettings } from '../../stores/settings'
import { AgentActivityPanel } from '../panels/AgentActivityPanel'
import { DiffReviewPanel } from '../panels/DiffReviewPanel'
import { FileOperationsPanel } from '../panels/FileOperationsPanel'
import { TerminalPanel } from '../panels/TerminalPanel'
import { TrajectoryInspector } from '../panels/TrajectoryInspector'
import { SettingsModal } from '../settings'
import { SidebarMemory } from '../sidebar/SidebarMemory'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'
import { ActivityBar } from './ActivityBar'
import { MainArea } from './MainArea'
import { SidebarPanel } from './SidebarPanel'
import { StatusBar } from './StatusBar'
import { createResizeHandlers } from './useResizeHandlers'

const XTerminal = lazy(() => import('../panels/XTerminal').then((m) => ({ default: m.XTerminal })))

export const AppShell: Component = () => {
  const {
    sidebarVisible,
    sidebarWidth,
    setSidebarWidth,
    rightPanelVisible,
    setRightPanelVisible,
    rightPanelTab,
    switchRightPanelTab,
    rightPanelWidth,
    setRightPanelWidth,
    bottomPanelVisible,
    setBottomPanelVisible,
    bottomPanelHeight,
    setBottomPanelHeight,
    bottomPanelTab,
    switchBottomPanelTab,
  } = useLayout()
  const { settings } = useSettings()
  const { currentSession } = useSession()

  const { startSidebarResize, startRightResize, startBottomResize } = createResizeHandlers({
    sidebarWidth,
    setSidebarWidth,
    rightPanelWidth,
    setRightPanelWidth,
    bottomPanelHeight,
    setBottomPanelHeight,
  })

  return (
    <div class="h-screen flex flex-col text-[var(--text-primary)] overflow-hidden">
      <StatusBar />

      <div class="flex-1 flex overflow-hidden">
        <ActivityBar />

        {/* Sidebar — width transition with overflow:hidden (no bleed, smooth) */}
        <div
          class="flex-shrink-0 overflow-hidden"
          style={{
            width: sidebarVisible() ? `${sidebarWidth()}px` : '0px',
            transition: 'width 120ms ease',
          }}
        >
          <SidebarPanel />
        </div>

        {/* Sidebar resize handle */}
        <Show when={sidebarVisible()}>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
          <div
            class="
              w-[3px] flex-shrink-0 cursor-col-resize
              bg-transparent hover:bg-[var(--accent-muted)]
              active:bg-[var(--accent)]
              transition-colors duration-150
            "
            onMouseDown={startSidebarResize}
          />
        </Show>

        {/* Center: Main content + optional bottom panel */}
        <div class="flex-1 flex flex-col overflow-hidden min-w-0">
          {/* Main content */}
          <div class="flex-1 overflow-hidden min-h-0">
            <MainArea />
          </div>

          {/* Bottom panel (Memory / Terminal / Output) — gated on ui.showBottomPanel */}
          <Show when={settings().ui.showBottomPanel && bottomPanelVisible()}>
            {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
            <div
              class="
                h-[3px] flex-shrink-0 cursor-row-resize
                bg-transparent hover:bg-[var(--accent-muted)]
                active:bg-[var(--accent)]
                transition-colors duration-150
              "
              onMouseDown={startBottomResize}
            />
            <div
              class="flex-shrink-0 overflow-hidden border-t border-[var(--border-subtle)]"
              style={{ height: `${bottomPanelHeight()}px`, 'max-height': '600px' }}
            >
              <div class="flex flex-col h-full bg-[var(--gray-1)]">
                {/* Tab header */}
                <div class="flex items-center h-8 flex-shrink-0 border-b border-[var(--border-subtle)]">
                  <button
                    type="button"
                    onClick={() => switchBottomPanelTab('memory')}
                    class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                    classList={{
                      'text-[var(--accent)] border-b border-[var(--accent)]':
                        bottomPanelTab() === 'memory',
                      'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                        bottomPanelTab() !== 'memory',
                    }}
                  >
                    <Brain class="w-3 h-3" />
                    Memory
                  </button>
                  <button
                    type="button"
                    onClick={() => switchBottomPanelTab('terminal')}
                    class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                    classList={{
                      'text-[var(--accent)] border-b border-[var(--accent)]':
                        bottomPanelTab() === 'terminal',
                      'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                        bottomPanelTab() !== 'terminal',
                    }}
                  >
                    <Terminal class="w-3 h-3" />
                    Terminal
                  </button>
                  <button
                    type="button"
                    onClick={() => switchBottomPanelTab('output')}
                    class="flex items-center gap-1.5 px-3 h-full text-[10px] font-semibold uppercase tracking-wider transition-colors"
                    classList={{
                      'text-[var(--accent)] border-b border-[var(--accent)]':
                        bottomPanelTab() === 'output',
                      'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                        bottomPanelTab() !== 'output',
                    }}
                  >
                    <ScrollText class="w-3 h-3" />
                    Output
                  </button>
                  <div class="flex-1" />
                  <button
                    type="button"
                    onClick={() => setBottomPanelVisible(false)}
                    class="p-1 mr-1.5 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
                    aria-label="Close bottom panel"
                  >
                    <X class="w-3 h-3" />
                  </button>
                </div>
                {/* Tab content — use display:none instead of <Show> so xterm DOM stays alive */}
                <div
                  class="flex-1 overflow-hidden"
                  style={{ display: bottomPanelTab() === 'memory' ? undefined : 'none' }}
                >
                  <PanelErrorBoundary panelName="Memory">
                    <SidebarMemory />
                  </PanelErrorBoundary>
                </div>
                <div
                  class="flex-1 overflow-hidden"
                  style={{ display: bottomPanelTab() === 'terminal' ? undefined : 'none' }}
                >
                  <PanelErrorBoundary panelName="Terminal">
                    <XTerminal />
                  </PanelErrorBoundary>
                </div>
                <div
                  class="flex-1 overflow-hidden"
                  style={{ display: bottomPanelTab() === 'output' ? undefined : 'none' }}
                >
                  <PanelErrorBoundary panelName="Output">
                    <TerminalPanel />
                  </PanelErrorBoundary>
                </div>
              </div>
            </div>
          </Show>
        </div>

        {/* Right panel resize handle + panel */}
        <Show when={settings().ui.showAgentActivity && rightPanelVisible()}>
          {/* biome-ignore lint/a11y/noStaticElementInteractions: resize handle uses mouse-only interaction by design */}
          <div
            class="
              w-[3px] flex-shrink-0 cursor-col-resize
              bg-transparent hover:bg-[var(--accent-muted)]
              active:bg-[var(--accent)]
              transition-colors duration-150
            "
            onMouseDown={startRightResize}
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
                    'text-[var(--accent)] border-b border-[var(--accent)]':
                      rightPanelTab() === 'files',
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
      </div>

      {/* Settings Modal (overlay) */}
      <SettingsModal />
    </div>
  )
}
