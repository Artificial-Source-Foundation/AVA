import { Download, Eye, GitBranch, Trash2, Upload } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { UISettings } from '../../stores/settings'
import { useSettings } from '../../stores/settings'

const Toggle: Component<{ checked: boolean; onChange: (v: boolean) => void }> = (props) => (
  <button
    type="button"
    onClick={() => props.onChange(!props.checked)}
    class={`relative w-8 h-[18px] rounded-full transition-colors ${
      props.checked ? 'bg-[var(--accent)]' : 'bg-[var(--border-strong)]'
    }`}
  >
    <span
      class="absolute top-[2px] left-[2px] w-[14px] h-[14px] rounded-full bg-white transition-transform"
      style={{ transform: props.checked ? 'translateX(14px)' : 'translateX(0)' }}
    />
  </button>
)

const ToggleRow: Component<{
  label: string
  description?: string
  checked: boolean
  onChange: (v: boolean) => void
}> = (props) => (
  <div class="flex items-center justify-between py-1.5">
    <div>
      <span class="text-xs text-[var(--text-secondary)]">{props.label}</span>
      <Show when={props.description}>
        <p class="text-[10px] text-[var(--text-muted)]">{props.description}</p>
      </Show>
    </div>
    <Toggle checked={props.checked} onChange={props.onChange} />
  </div>
)

export const GeneralSection: Component = () => {
  const {
    settings,
    updateUI,
    updateAgentLimits,
    updateBehavior,
    updateGit,
    exportSettings,
    importSettings,
    resetSettings,
  } = useSettings()
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
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          <span class="inline-flex items-center gap-1.5">
            <GitBranch class="w-3 h-3" /> Git Integration
          </span>
        </h3>
        <ToggleRow
          label="Enable git integration"
          description="Detect git repos and enable auto-commit features"
          checked={settings().git.enabled}
          onChange={(v) => updateGit({ enabled: v })}
        />
        <Show when={settings().git.enabled}>
          <ToggleRow
            label="Auto-commit AI edits"
            description="Commit file changes after each successful AI edit. Enables undo."
            checked={settings().git.autoCommit}
            onChange={(v) => updateGit({ autoCommit: v })}
          />
          <Show when={settings().git.autoCommit}>
            <div class="flex items-center justify-between py-1.5 gap-3">
              <div>
                <span class="text-xs text-[var(--text-secondary)]">Commit prefix</span>
                <p class="text-[10px] text-[var(--text-muted)]">
                  Prepended to auto-commit messages
                </p>
              </div>
              <input
                type="text"
                value={settings().git.commitPrefix}
                onInput={(e) => updateGit({ commitPrefix: e.currentTarget.value })}
                class="w-28 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                placeholder="[ava]"
              />
            </div>
          </Show>
        </Show>
      </div>

      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          <span class="inline-flex items-center gap-1.5">
            <Eye class="w-3 h-3" /> Integrations
          </span>
        </h3>
        <ToggleRow
          label="Watch for AI comments"
          description="Detect // AI! and // AI? in project files"
          checked={settings().behavior.fileWatcher}
          onChange={(v) => updateBehavior({ fileWatcher: v })}
        />
        <ToggleRow
          label="Clipboard watcher"
          description="Detect clipboard changes with code or LLM output"
          checked={settings().behavior.clipboardWatcher}
          onChange={(v) => updateBehavior({ clipboardWatcher: v })}
        />
      </div>

      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <span class="text-[10px] font-mono text-[var(--text-muted)]">AVA v0.1.0-alpha</span>
      </div>
    </div>
  )
}
