/**
 * Behavior Settings Tab
 *
 * Input behavior (sendKey), chat (autoScroll, autoTitle),
 * code blocks (lineNumbers, wordWrap), and notifications.
 */

import { Bell, Code2, Keyboard, MessageCircle, Wrench } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import type { SendKey, ToolResponseStyle } from '../../../stores/settings'
import { useSettings } from '../../../stores/settings'
import { segmentedBtnClass } from '../../ui/SegmentedControl'
import { ToggleRow } from '../../ui/ToggleRow'
import { SettingsCard } from '../SettingsCard'

// ============================================================================
// Main Tab
// ============================================================================

export const BehaviorTab: Component = () => {
  const { settings, updateBehavior, updateNotifications, updateUI } = useSettings()

  return (
    <div class="grid grid-cols-1" style={{ gap: '28px' }}>
      <SettingsCard icon={Keyboard} title="Input" description="Send key and input behavior">
        <div class="flex items-center justify-between py-2">
          <div>
            <span class="text-[14px] text-[var(--text-secondary)]">Send message with</span>
            <p class="text-[13px] text-[var(--text-muted)]">
              {settings().behavior.sendKey === 'enter'
                ? 'Shift+Enter for newline'
                : 'Enter for newline'}
            </p>
          </div>
          <div class="flex gap-1">
            <button
              type="button"
              onClick={() => updateBehavior({ sendKey: 'enter' as SendKey })}
              class={segmentedBtnClass(settings().behavior.sendKey === 'enter')}
            >
              Enter
            </button>
            <button
              type="button"
              onClick={() => updateBehavior({ sendKey: 'ctrl+enter' as SendKey })}
              class={segmentedBtnClass(settings().behavior.sendKey === 'ctrl+enter')}
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

      <SettingsCard
        icon={Wrench}
        title="Tool Results"
        description="How tool output is displayed in chat"
      >
        <div class="flex items-center justify-between py-2">
          <div>
            <span class="text-[14px] text-[var(--text-secondary)]">Tool response style</span>
            <p class="text-[13px] text-[var(--text-muted)]">
              {settings().ui.toolResponseStyle === 'concise'
                ? 'Results collapsed by default — click to expand'
                : 'Results expanded with full output'}
            </p>
          </div>
          <div class="flex gap-1">
            <button
              type="button"
              onClick={() => updateUI({ toolResponseStyle: 'concise' as ToolResponseStyle })}
              class={segmentedBtnClass(settings().ui.toolResponseStyle === 'concise')}
            >
              Concise
            </button>
            <button
              type="button"
              onClick={() => updateUI({ toolResponseStyle: 'detailed' as ToolResponseStyle })}
              class={segmentedBtnClass(settings().ui.toolResponseStyle === 'detailed')}
            >
              Detailed
            </button>
          </div>
        </div>
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
          <div class="flex items-center justify-between py-2 gap-3">
            <span class="text-[14px] text-[var(--text-secondary)]">Volume</span>
            <div class="flex items-center gap-2 flex-1 justify-end">
              <input
                type="range"
                min={0}
                max={100}
                step={5}
                value={settings().notifications.soundVolume}
                onInput={(e) => updateNotifications({ soundVolume: Number(e.currentTarget.value) })}
                class="w-32 accent-[var(--accent)]"
              />
              <span class="text-[13px] font-mono text-[var(--text-muted)] w-10 text-right">
                {settings().notifications.soundVolume}%
              </span>
            </div>
          </div>
        </Show>
      </SettingsCard>
    </div>
  )
}
