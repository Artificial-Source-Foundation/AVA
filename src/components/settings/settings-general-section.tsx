import { Bot, Download, Eye, GitBranch, Monitor, Trash2, Upload } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { UISettings } from '../../stores/settings'
import { useSettings } from '../../stores/settings'
import { Toggle } from '../ui/Toggle'
import { ToggleRow } from '../ui/ToggleRow'
import { SettingsCard } from './SettingsCard'

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

  const handleClearAll = (): void => {
    localStorage.clear()
    resetSettings()
    setConfirmClear(false)
    window.location.reload()
  }

  return (
    <div class="grid grid-cols-1" style={{ gap: '28px' }}>
      <SettingsCard icon={Monitor} title="Interface" description="Layout and display preferences">
        <div class="space-y-0.5">
          <For each={uiToggles}>
            {(toggle) => (
              <div class="flex items-center justify-between py-2">
                <span class="text-[14px] text-[var(--text-secondary)]">{toggle.label}</span>
                <Toggle
                  checked={!!settings().ui[toggle.key]}
                  onChange={() => updateUI({ [toggle.key]: !settings().ui[toggle.key] })}
                />
              </div>
            )}
          </For>
        </div>
      </SettingsCard>

      <SettingsCard icon={Bot} title="Agent" description="AI agent behavior settings">
        <ToggleRow
          label="Auto-fix lint errors after edits"
          description="Run linter after file changes and feed errors back to agent"
          checked={settings().agentLimits.autoFixLint}
          onChange={(v) => updateAgentLimits({ autoFixLint: v })}
        />
      </SettingsCard>

      <SettingsCard icon={GitBranch} title="Git Integration" description="Version control settings">
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
            <div class="flex items-center justify-between py-2 gap-3">
              <div>
                <span class="text-[14px] text-[var(--text-secondary)]">Commit prefix</span>
                <p class="text-[13px] text-[var(--gray-8)]">Prepended to auto-commit messages</p>
              </div>
              <input
                type="text"
                value={settings().git.commitPrefix}
                onInput={(e) => updateGit({ commitPrefix: e.currentTarget.value })}
                class="w-32 px-3 py-2 text-[14px] rounded-[var(--radius-md)] bg-[var(--gray-3)] text-[var(--text-primary)] border border-[var(--gray-5)] focus:border-[var(--accent)] outline-none"
                placeholder="[ava]"
              />
            </div>
          </Show>
        </Show>
      </SettingsCard>

      <SettingsCard icon={Eye} title="Integrations" description="File and clipboard watchers">
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
      </SettingsCard>

      <SettingsCard icon={Download} title="Data" description="Import, export, and reset">
        <div class="flex flex-wrap gap-2.5">
          <button
            type="button"
            onClick={() => exportSettings()}
            class="flex items-center gap-2 px-4 py-2.5 text-[13px] text-[var(--text-secondary)] bg-[var(--gray-2)] border border-[var(--gray-5)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
          >
            <Download class="w-4 h-4" />
            Export Settings
          </button>
          <button
            type="button"
            onClick={() => importSettings()}
            class="flex items-center gap-2 px-4 py-2.5 text-[13px] text-[var(--text-secondary)] bg-[var(--gray-2)] border border-[var(--gray-5)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
          >
            <Upload class="w-4 h-4" />
            Import Settings
          </button>
          <Show
            when={confirmClear()}
            fallback={
              <button
                type="button"
                onClick={() => setConfirmClear(true)}
                class="flex items-center gap-2 px-4 py-2.5 text-[13px] text-[var(--error)] bg-[var(--gray-2)] border border-[var(--gray-5)] rounded-[var(--radius-md)] hover:border-[var(--error)] transition-colors"
              >
                <Trash2 class="w-4 h-4" />
                Clear All Data
              </button>
            }
          >
            <div class="flex items-center gap-2">
              <span class="text-[13px] text-[var(--error)]">Are you sure?</span>
              <button
                type="button"
                onClick={handleClearAll}
                class="px-4 py-2.5 text-[13px] text-white bg-[var(--error)] rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
              >
                Yes, clear everything
              </button>
              <button
                type="button"
                onClick={() => setConfirmClear(false)}
                class="px-4 py-2.5 text-[13px] text-[var(--text-secondary)] bg-[var(--gray-2)] border border-[var(--gray-5)] rounded-[var(--radius-md)] transition-colors"
              >
                Cancel
              </button>
            </div>
          </Show>
        </div>
        <div class="pt-3">
          <span class="text-[12px] font-mono text-[var(--gray-8)]">AVA v0.1.0-alpha</span>
        </div>
      </SettingsCard>
    </div>
  )
}
