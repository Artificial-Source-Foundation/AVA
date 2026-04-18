import { isTauri } from '@tauri-apps/api/core'
import type { LucideProps } from 'lucide-solid'
import { Brain, ScrollText, Terminal, X } from 'lucide-solid'
import { type Component, For, type JSX, lazy, Show } from 'solid-js'
import { useLayout } from '../../stores/layout'
import { usePlanOverlay } from '../../stores/planOverlayStore'
import { useSettings } from '../../stores/settings'
import { PlanOverlay } from '../chat/PlanOverlay'

import { TerminalPanel } from '../panels/TerminalPanel'
import { SettingsModal } from '../settings'
import { SidebarMemory } from '../sidebar/SidebarMemory'
import { PanelErrorBoundary } from '../ui/PanelErrorBoundary'
import { MainArea } from './MainArea'
import { RightPanel } from './RightPanel'
import { SidebarPanel } from './SidebarPanel'
import { TitleBar } from './TitleBar'
import { createResizeHandlers } from './useResizeHandlers'

const XTerminal = lazy(() => import('../panels/XTerminal').then((m) => ({ default: m.XTerminal })))

type BottomPanelTab = 'memory' | 'terminal' | 'output'

const panelTabs: readonly {
  id: BottomPanelTab
  icon: (props: LucideProps) => JSX.Element
  label: string
}[] = [
  { id: 'memory', icon: Brain, label: 'Memory' },
  { id: 'terminal', icon: Terminal, label: 'Terminal' },
  { id: 'output', icon: ScrollText, label: 'Output' },
]

export const AppShell: Component = () => {
  const {
    sidebarVisible,
    sidebarWidth,
    setSidebarWidth,
    rightPanelWidth,
    setRightPanelWidth,
    bottomPanelVisible,
    setBottomPanelVisible,
    bottomPanelHeight,
    setBottomPanelHeight,
    bottomPanelTab,
    switchBottomPanelTab,
    viewingPlanId,
  } = useLayout()
  const { settings } = useSettings()

  const { settingsOpen } = useLayout()
  const { isOpen: planOverlayOpen } = usePlanOverlay()

  const { startSidebarResize, startRightResize, startBottomResize } = createResizeHandlers({
    sidebarWidth,
    setSidebarWidth,
    rightPanelWidth,
    setRightPanelWidth,
    bottomPanelHeight,
    setBottomPanelHeight,
  })
  const showChatSidebar = () => sidebarVisible()

  return (
    <div class="h-screen flex flex-col text-[var(--text-primary)] overflow-hidden">
      <Show when={isTauri()}>
        <TitleBar />
      </Show>

      <div
        class="flex-1 flex overflow-hidden"
        style={{ visibility: settingsOpen() ? 'hidden' : 'visible' }}
      >
        {/* Sidebar — unified panel (no separate activity bar) */}
        <div
          class="flex-shrink-0 overflow-hidden"
          style={{
            width: `${sidebarWidth()}px`,
            'margin-left': showChatSidebar() ? '0px' : `-${sidebarWidth()}px`,
            transition: 'margin-left 120ms var(--ease-out)',
          }}
        >
          <SidebarPanel />
        </div>

        {/* Sidebar resize handle */}
        <Show when={showChatSidebar()}>
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
          {/* Main content — fullscreen plan viewer (via viewingPlanId) takes precedence over overlay */}
          <div class="flex-1 overflow-hidden min-h-0">
            <Show when={!planOverlayOpen() || viewingPlanId()} fallback={<PlanOverlay />}>
              <MainArea />
            </Show>
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
                <div
                  role="tablist"
                  class="flex items-center h-8 flex-shrink-0 border-b border-[var(--border-subtle)]"
                >
                  <For each={panelTabs}>
                    {(tab) => (
                      <button
                        type="button"
                        role="tab"
                        aria-selected={bottomPanelTab() === tab.id}
                        onClick={() => switchBottomPanelTab(tab.id)}
                        class="flex items-center gap-1.5 px-3 h-full text-[var(--text-2xs)] font-semibold uppercase tracking-wider transition-colors"
                        classList={{
                          'text-[var(--accent)] border-b border-[var(--accent)]':
                            bottomPanelTab() === tab.id,
                          'text-[var(--text-muted)] hover:text-[var(--text-secondary)]':
                            bottomPanelTab() !== tab.id,
                        }}
                      >
                        {tab.icon({ class: 'w-3 h-3' })}
                        {tab.label}
                      </button>
                    )}
                  </For>
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

        <RightPanel startRightResize={startRightResize} />
      </div>

      {/* Settings Modal (overlay) */}
      <SettingsModal />

      {/* PlanOverlay is rendered inline above, replacing MainArea when open */}
    </div>
  )
}
