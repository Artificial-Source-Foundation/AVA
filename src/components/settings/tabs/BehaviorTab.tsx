/**
 * Behavior Settings Tab
 *
 * Input behavior (sendKey), chat (autoScroll, autoTitle),
 * code blocks (lineNumbers, wordWrap), and notifications.
 */

import {
  Bell,
  Clipboard,
  Code2,
  Eye,
  GitBranch,
  Keyboard,
  MessageCircle,
  SlidersHorizontal,
  Tag,
} from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { SendKey } from '../../../stores/settings'
import { useSettings } from '../../../stores/settings'
import { SettingsCard } from '../SettingsCard'

// ============================================================================
// Shared helpers (same patterns as AppearanceTab)
// ============================================================================

function segmentedBtn(active: boolean): string {
  return `px-2.5 py-1 text-[11px] rounded-[var(--radius-md)] transition-colors ${
    active
      ? 'bg-[var(--accent)] text-white'
      : 'bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)]'
  }`
}

const Toggle: Component<{ checked: boolean; onChange: (v: boolean) => void }> = (props) => (
  <button
    type="button"
    onClick={() => props.onChange(!props.checked)}
    class={`
      relative w-8 h-[18px] rounded-full transition-colors
      ${props.checked ? 'bg-[var(--accent)]' : 'bg-[var(--border-strong)]'}
    `}
  >
    <span
      class="absolute top-[2px] left-[2px] w-[14px] h-[14px] rounded-full bg-white transition-transform"
      style={{
        transform: props.checked ? 'translateX(14px)' : 'translateX(0)',
      }}
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

// ============================================================================
// Main Tab
// ============================================================================

export const BehaviorTab: Component = () => {
  const { settings, updateBehavior, updateNotifications, updateGit, updateSettings } = useSettings()

  return (
    <div class="grid grid-cols-1 gap-4">
      <SettingsCard icon={Keyboard} title="Input" description="Send key and input behavior">
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Send message with</span>
            <p class="text-[10px] text-[var(--text-muted)]">
              {settings().behavior.sendKey === 'enter'
                ? 'Shift+Enter for newline'
                : 'Enter for newline'}
            </p>
          </div>
          <div class="flex gap-1">
            <button
              type="button"
              onClick={() => updateBehavior({ sendKey: 'enter' as SendKey })}
              class={segmentedBtn(settings().behavior.sendKey === 'enter')}
            >
              Enter
            </button>
            <button
              type="button"
              onClick={() => updateBehavior({ sendKey: 'ctrl+enter' as SendKey })}
              class={segmentedBtn(settings().behavior.sendKey === 'ctrl+enter')}
            >
              Ctrl+Enter
            </button>
          </div>
        </div>
      </SettingsCard>

      <SettingsCard icon={MessageCircle} title="Chat" description="Scrolling and session behavior">
        <ToggleRow
          label="Auto-scroll to new messages"
          checked={settings().behavior.autoScroll}
          onChange={(v) => updateBehavior({ autoScroll: v })}
        />
        <ToggleRow
          label="Auto-title sessions"
          description="Generate a title from the first message"
          checked={settings().behavior.sessionAutoTitle}
          onChange={(v) => updateBehavior({ sessionAutoTitle: v })}
        />
      </SettingsCard>

      <SettingsCard icon={Code2} title="Code Blocks" description="Code rendering preferences">
        <ToggleRow
          label="Line numbers"
          checked={settings().behavior.lineNumbers}
          onChange={(v) => updateBehavior({ lineNumbers: v })}
        />
        <ToggleRow
          label="Word wrap"
          description="Wrap long lines instead of horizontal scroll"
          checked={settings().behavior.wordWrap}
          onChange={(v) => updateBehavior({ wordWrap: v })}
        />
      </SettingsCard>

      <SettingsCard icon={Bell} title="Notifications" description="Desktop and sound alerts">
        <ToggleRow
          label="Desktop notification on completion"
          checked={settings().notifications.notifyOnCompletion}
          onChange={(v) => updateNotifications({ notifyOnCompletion: v })}
        />
        <ToggleRow
          label="Sound on completion"
          checked={settings().notifications.soundOnCompletion}
          onChange={(v) => updateNotifications({ soundOnCompletion: v })}
        />
        <Show when={settings().notifications.soundOnCompletion}>
          <div class="flex items-center justify-between py-1.5 gap-3">
            <span class="text-xs text-[var(--text-secondary)]">Volume</span>
            <div class="flex items-center gap-2 flex-1 justify-end">
              <input
                type="range"
                min={0}
                max={100}
                step={5}
                value={settings().notifications.soundVolume}
                onInput={(e) => updateNotifications({ soundVolume: Number(e.currentTarget.value) })}
                class="w-28 accent-[var(--accent)]"
              />
              <span class="text-[11px] font-mono text-[var(--text-muted)] w-10 text-right">
                {settings().notifications.soundVolume}%
              </span>
            </div>
          </div>
        </Show>
      </SettingsCard>

      <SettingsCard
        icon={SlidersHorizontal}
        title="Context Management"
        description="Auto-compaction settings"
      >
        <div class="flex items-center justify-between py-1.5 gap-3">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Compaction threshold</span>
            <p class="text-[10px] text-[var(--text-muted)]">
              Auto-compact when context reaches this % ({settings().generation.compactionThreshold}
              %)
            </p>
          </div>
          <div class="flex items-center gap-2">
            <input
              type="range"
              min={50}
              max={95}
              step={5}
              value={settings().generation.compactionThreshold}
              onInput={(e) =>
                updateSettings({
                  generation: {
                    ...settings().generation,
                    compactionThreshold: Number(e.currentTarget.value),
                  },
                })
              }
              class="w-24 accent-[var(--accent)]"
            />
            <span class="text-[11px] font-mono text-[var(--text-muted)] w-10 text-right">
              {settings().generation.compactionThreshold}%
            </span>
          </div>
        </div>
      </SettingsCard>

      <SettingsCard
        icon={Eye}
        title="File Watcher"
        description="Detect AI comment patterns in files"
      >
        <ToggleRow
          label="Watch for AI comments"
          description="Detect // AI! and // AI? in project files"
          checked={settings().behavior.fileWatcher}
          onChange={(v) => updateBehavior({ fileWatcher: v })}
        />
      </SettingsCard>

      <SettingsCard icon={Clipboard} title="Clipboard" description="Clipboard monitoring">
        <ToggleRow
          label="Copy-paste mode"
          description="Detect clipboard changes with code or LLM output"
          checked={settings().behavior.clipboardWatcher}
          onChange={(v) => updateBehavior({ clipboardWatcher: v })}
        />
      </SettingsCard>

      <SettingsCard
        icon={GitBranch}
        title="Git Integration"
        description="Auto-commit and version tracking"
      >
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
      </SettingsCard>

      <SettingsCard icon={Tag} title="Model Aliases" description="Short names for model IDs">
        <p class="text-[10px] text-[var(--text-muted)] mb-2">
          Create short names for model IDs (e.g. "fast" → "openai/gpt-4o-mini")
        </p>
        <div class="space-y-1.5">
          <For each={Object.entries(settings().modelAliases)}>
            {([alias, modelId]) => (
              <div class="flex items-center gap-2">
                <input
                  type="text"
                  value={alias}
                  onBlur={(e) => {
                    const newAlias = e.currentTarget.value.trim()
                    if (!newAlias || newAlias === alias) return
                    const aliases = { ...settings().modelAliases }
                    delete aliases[alias]
                    aliases[newAlias] = modelId
                    updateSettings({ modelAliases: aliases })
                  }}
                  class="w-24 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                  placeholder="alias"
                />
                <span class="text-[10px] text-[var(--text-muted)]">→</span>
                <input
                  type="text"
                  value={modelId}
                  onBlur={(e) => {
                    const newModelId = e.currentTarget.value.trim()
                    if (!newModelId) return
                    updateSettings({
                      modelAliases: { ...settings().modelAliases, [alias]: newModelId },
                    })
                  }}
                  class="flex-1 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
                  placeholder="provider/model-id"
                />
                <button
                  type="button"
                  onClick={() => {
                    const aliases = { ...settings().modelAliases }
                    delete aliases[alias]
                    updateSettings({ modelAliases: aliases })
                  }}
                  class="px-1.5 py-1 text-[10px] text-[var(--error)] hover:bg-[var(--alpha-white-05)] rounded-[var(--radius-sm)] transition-colors"
                  title="Remove alias"
                >
                  ✕
                </button>
              </div>
            )}
          </For>
        </div>
        <button
          type="button"
          onClick={() => {
            const aliases = { ...settings().modelAliases }
            let name = 'alias'
            let i = 1
            while (aliases[name]) {
              name = `alias-${i++}`
            }
            aliases[name] = ''
            updateSettings({ modelAliases: aliases })
          }}
          class="mt-2 px-3 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)] transition-colors"
        >
          + Add Alias
        </button>
      </SettingsCard>
    </div>
  )
}
