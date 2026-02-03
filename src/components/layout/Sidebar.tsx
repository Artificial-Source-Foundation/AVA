/**
 * Sidebar Component
 *
 * Premium sidebar with session management, navigation, and settings.
 * Features glass effect on glass theme, proper theming.
 */

import { Settings, Sparkles } from 'lucide-solid'
import { type Component, createSignal } from 'solid-js'
import { SessionList } from '../sessions'
import { SettingsModal } from '../settings'

export const Sidebar: Component = () => {
  const [showSettings, setShowSettings] = createSignal(false)

  return (
    <>
      <aside
        class="
          w-72 flex flex-col
          bg-[var(--sidebar-background)]
          border-r border-[var(--sidebar-border)]
          transition-colors duration-[var(--duration-normal)]
        "
      >
        {/* Header / Brand */}
        <div class="p-5 border-b border-[var(--border-subtle)]">
          <div class="flex items-center gap-3">
            <div
              class="
                w-10 h-10 rounded-[var(--radius-xl)]
                bg-[var(--accent)]
                flex items-center justify-center
                shadow-sm
              "
            >
              <Sparkles class="w-5 h-5 text-white" />
            </div>
            <div>
              <h1 class="text-lg font-semibold text-[var(--text-primary)] font-display">Estela</h1>
              <p class="text-xs text-[var(--text-tertiary)]">AI Coding Assistant</p>
            </div>
          </div>
        </div>

        {/* Session list */}
        <div class="flex-1 overflow-hidden">
          <SessionList />
        </div>

        {/* Bottom actions */}
        <div class="p-3 border-t border-[var(--border-subtle)]">
          <button
            type="button"
            onClick={() => setShowSettings(true)}
            class="
              w-full flex items-center gap-3
              px-3 py-2.5
              rounded-[var(--radius-lg)]
              text-[var(--text-secondary)]
              transition-all duration-[var(--duration-fast)]
              hover:bg-[var(--sidebar-item-hover)]
              hover:text-[var(--text-primary)]
              active:scale-[0.98]
            "
          >
            <Settings class="w-5 h-5" />
            <span class="font-medium text-sm">Settings</span>
          </button>
        </div>
      </aside>

      {/* Settings Modal */}
      <SettingsModal isOpen={showSettings()} onClose={() => setShowSettings(false)} />
    </>
  )
}
