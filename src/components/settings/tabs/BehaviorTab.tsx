/**
 * Behavior Settings Tab
 *
 * Input behavior (sendKey), chat (autoScroll, autoTitle),
 * code blocks (lineNumbers, wordWrap), and notifications.
 */

import { type Component, Show } from 'solid-js'
import type { SendKey } from '../../../stores/settings'
import { useSettings } from '../../../stores/settings'

// ============================================================================
// Shared helpers (same patterns as AppearanceTab)
// ============================================================================

const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
    {props.title}
  </h3>
)

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
  const { settings, updateBehavior, updateNotifications, updateGit } = useSettings()

  return (
    <div class="space-y-5">
      {/* Input */}
      <div>
        <SectionHeader title="Input" />
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
      </div>

      {/* Chat */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Chat" />
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
      </div>

      {/* Code Blocks */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Code Blocks" />
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
      </div>

      {/* Notifications */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Notifications" />
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
      </div>

      {/* File Watcher */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="File Watcher" />
        <ToggleRow
          label="Watch for AI comments"
          description="Detect // AI! and // AI? in project files"
          checked={settings().behavior.fileWatcher}
          onChange={(v) => updateBehavior({ fileWatcher: v })}
        />
      </div>

      {/* Git Integration */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Git Integration" />
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
                placeholder="[estela]"
              />
            </div>
          </Show>
        </Show>
      </div>
    </div>
  )
}
