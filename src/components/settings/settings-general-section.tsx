import { Download, Trash2, Upload } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { UISettings } from '../../stores/settings'
import { useSettings } from '../../stores/settings'

export const GeneralSection: Component = () => {
  const { settings, updateUI, updateAgentLimits, exportSettings, importSettings, resetSettings } =
    useSettings()
  const [confirmClear, setConfirmClear] = createSignal(false)

  const uiToggles: { key: keyof UISettings; label: string }[] = [
    { key: 'showBottomPanel', label: 'Show memory panel on start' },
    { key: 'showAgentActivity', label: 'Show agent activity panel' },
    { key: 'compactMessages', label: 'Compact message layout' },
    { key: 'showInfoBar', label: 'Show chat info bar' },
    { key: 'showTokenCount', label: 'Show token count' },
    { key: 'showModelInTitleBar', label: 'Show model in title bar' },
  ]

  const handleClearAll = () => {
    localStorage.clear()
    resetSettings()
    setConfirmClear(false)
    window.location.reload()
  }

  return (
    <div class="space-y-4">
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Interface
        </h3>
        <div class="space-y-0.5">
          <For each={uiToggles}>
            {(toggle) => (
              <div class="flex items-center justify-between py-1.5">
                <span class="text-xs text-[var(--text-secondary)]">{toggle.label}</span>
                <button
                  type="button"
                  onClick={() => updateUI({ [toggle.key]: !settings().ui[toggle.key] })}
                  class={`w-9 h-5 rounded-full transition-colors flex-shrink-0 flex items-center ${settings().ui[toggle.key] ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'}`}
                >
                  <span
                    class={`w-4 h-4 rounded-full bg-white shadow-sm transition-transform duration-150 ${settings().ui[toggle.key] ? 'translate-x-[18px]' : 'translate-x-[2px]'}`}
                  />
                </button>
              </div>
            )}
          </For>
        </div>
      </div>

      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Agent
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">
              Auto-fix lint errors after edits
            </span>
            <p class="text-[10px] text-[var(--text-muted)]">
              Run linter after file changes and feed errors back to agent
            </p>
          </div>
          <button
            type="button"
            onClick={() => updateAgentLimits({ autoFixLint: !settings().agentLimits.autoFixLint })}
            class={`w-9 h-5 rounded-full transition-colors flex-shrink-0 flex items-center ${settings().agentLimits.autoFixLint ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'}`}
          >
            <span
              class={`w-4 h-4 rounded-full bg-white shadow-sm transition-transform duration-150 ${settings().agentLimits.autoFixLint ? 'translate-x-[18px]' : 'translate-x-[2px]'}`}
            />
          </button>
        </div>
      </div>

      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Data
        </h3>
        <div class="flex flex-wrap gap-2">
          <button
            type="button"
            onClick={() => exportSettings()}
            class="flex items-center gap-1.5 px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
          >
            <Download class="w-3 h-3" />
            Export Settings
          </button>
          <button
            type="button"
            onClick={() => importSettings()}
            class="flex items-center gap-1.5 px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
          >
            <Upload class="w-3 h-3" />
            Import Settings
          </button>
          <Show
            when={confirmClear()}
            fallback={
              <button
                type="button"
                onClick={() => setConfirmClear(true)}
                class="flex items-center gap-1.5 px-2.5 py-1.5 text-[11px] text-[var(--error)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--error)] transition-colors"
              >
                <Trash2 class="w-3 h-3" />
                Clear All Data
              </button>
            }
          >
            <div class="flex items-center gap-1.5">
              <span class="text-[11px] text-[var(--error)]">Are you sure?</span>
              <button
                type="button"
                onClick={handleClearAll}
                class="px-2.5 py-1.5 text-[11px] text-white bg-[var(--error)] rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
              >
                Yes, clear everything
              </button>
              <button
                type="button"
                onClick={() => setConfirmClear(false)}
                class="px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
              >
                Cancel
              </button>
            </div>
          </Show>
        </div>
      </div>

      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <span class="text-[10px] font-mono text-[var(--text-muted)]">AVA v0.1.0-alpha</span>
      </div>
    </div>
  )
}
