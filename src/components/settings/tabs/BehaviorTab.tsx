/**
 * Behavior Settings Tab
 *
 * Pencil design: 4 cards (Input, Chat, Code Blocks, Notifications).
 * Each card: #111114 surface, #ffffff08 border, rounded-12, 20px padding, 16px gap.
 * Toggle ON: 44x24 #0A84FF white knob. OFF: #2C2C2E #86868B knob.
 * Segmented: rounded-8 #ffffff06 bg, 2px padding, active #ffffff10.
 */

import { Bell, Code2, Download, Keyboard, MessageCircle, Wrench } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import type { SendKey, ToolResponseStyle } from '../../../stores/settings'
import { useSettings } from '../../../stores/settings'
import { ToggleRow } from '../../ui/ToggleRow'
import { SettingsCard } from '../SettingsCard'

// ============================================================================
// Main Tab
// ============================================================================

export const BehaviorTab: Component = () => {
  const { settings, updateBehavior, updateNotifications, updateUI } = useSettings()

  return (
    <div class="flex flex-col" style={{ gap: '24px' }}>
      {/* Page title */}
      <h2
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
          margin: '0',
        }}
      >
        Behavior
      </h2>

      {/* Input Card */}
      <SettingsCard icon={Keyboard} title="Input" description="Send key and input behavior">
        <div class="flex items-center justify-between">
          <div class="flex flex-col" style={{ gap: '2px' }}>
            <span
              style={{ 'font-family': 'Geist, sans-serif', 'font-size': '13px', color: '#C8C8CC' }}
            >
              Send message with
            </span>
            <span
              style={{ 'font-family': 'Geist, sans-serif', 'font-size': '12px', color: '#48484A' }}
            >
              {settings().behavior.sendKey === 'enter'
                ? 'Shift+Enter for newline'
                : 'Enter for newline'}
            </span>
          </div>
          <div
            style={{
              display: 'inline-flex',
              'border-radius': '8px',
              background: '#ffffff06',
              padding: '2px',
              gap: '2px',
            }}
          >
            <button
              type="button"
              onClick={() => updateBehavior({ sendKey: 'enter' as SendKey })}
              style={{
                'border-radius': '6px',
                padding: '6px 12px',
                background: settings().behavior.sendKey === 'enter' ? '#ffffff10' : 'transparent',
                color: settings().behavior.sendKey === 'enter' ? '#F5F5F7' : '#48484A',
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                'font-weight': settings().behavior.sendKey === 'enter' ? '500' : '400',
                border: 'none',
                cursor: 'pointer',
                transition: 'background 150ms, color 150ms',
              }}
            >
              Enter
            </button>
            <button
              type="button"
              onClick={() => updateBehavior({ sendKey: 'ctrl+enter' as SendKey })}
              style={{
                'border-radius': '6px',
                padding: '6px 12px',
                background:
                  settings().behavior.sendKey === 'ctrl+enter' ? '#ffffff10' : 'transparent',
                color: settings().behavior.sendKey === 'ctrl+enter' ? '#F5F5F7' : '#48484A',
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                'font-weight': settings().behavior.sendKey === 'ctrl+enter' ? '500' : '400',
                border: 'none',
                cursor: 'pointer',
                transition: 'background 150ms, color 150ms',
              }}
            >
              Ctrl+Enter
            </button>
          </div>
        </div>
      </SettingsCard>

      {/* Chat Card */}
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

      {/* Code Blocks Card */}
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

      {/* Notifications Card */}
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
          <div class="flex items-center justify-between gap-3">
            <span
              style={{ 'font-family': 'Geist, sans-serif', 'font-size': '13px', color: '#C8C8CC' }}
            >
              Volume
            </span>
            <div class="flex items-center gap-2 flex-1 justify-end">
              <input
                type="range"
                min={0}
                max={100}
                step={5}
                value={settings().notifications.soundVolume}
                onInput={(e) => updateNotifications({ soundVolume: Number(e.currentTarget.value) })}
                class="w-32 accent-[#0A84FF]"
              />
              <span
                class="w-10 text-right"
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '12px',
                  color: '#48484A',
                }}
              >
                {settings().notifications.soundVolume}%
              </span>
            </div>
          </div>
        </Show>
      </SettingsCard>

      {/* Tool Results Card (not in Pencil design but keeping functionality) */}
      <SettingsCard
        icon={Wrench}
        title="Tool Results"
        description="How tool output is displayed in chat"
      >
        <div class="flex items-center justify-between">
          <div class="flex flex-col" style={{ gap: '2px' }}>
            <span
              style={{ 'font-family': 'Geist, sans-serif', 'font-size': '13px', color: '#C8C8CC' }}
            >
              Tool response style
            </span>
            <span
              style={{ 'font-family': 'Geist, sans-serif', 'font-size': '12px', color: '#48484A' }}
            >
              {settings().ui.toolResponseStyle === 'concise'
                ? 'Results collapsed by default — click to expand'
                : 'Results expanded with full output'}
            </span>
          </div>
          <div
            style={{
              display: 'inline-flex',
              'border-radius': '8px',
              background: '#ffffff06',
              padding: '2px',
              gap: '2px',
            }}
          >
            <button
              type="button"
              onClick={() => updateUI({ toolResponseStyle: 'concise' as ToolResponseStyle })}
              style={{
                'border-radius': '6px',
                padding: '6px 12px',
                background:
                  settings().ui.toolResponseStyle === 'concise' ? '#ffffff10' : 'transparent',
                color: settings().ui.toolResponseStyle === 'concise' ? '#F5F5F7' : '#48484A',
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                'font-weight': settings().ui.toolResponseStyle === 'concise' ? '500' : '400',
                border: 'none',
                cursor: 'pointer',
                transition: 'background 150ms, color 150ms',
              }}
            >
              Concise
            </button>
            <button
              type="button"
              onClick={() => updateUI({ toolResponseStyle: 'detailed' as ToolResponseStyle })}
              style={{
                'border-radius': '6px',
                padding: '6px 12px',
                background:
                  settings().ui.toolResponseStyle === 'detailed' ? '#ffffff10' : 'transparent',
                color: settings().ui.toolResponseStyle === 'detailed' ? '#F5F5F7' : '#48484A',
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                'font-weight': settings().ui.toolResponseStyle === 'detailed' ? '500' : '400',
                border: 'none',
                cursor: 'pointer',
                transition: 'background 150ms, color 150ms',
              }}
            >
              Detailed
            </button>
          </div>
        </div>
      </SettingsCard>

      {/* Updates Card */}
      <SettingsCard icon={Download} title="Updates" description="Automatic update behavior">
        <ToggleRow
          label="Auto-update"
          description="Check for updates automatically on startup"
          checked={settings().behavior.updateCheck !== false}
          onChange={(v) => updateBehavior({ updateCheck: v })}
        />
      </SettingsCard>
    </div>
  )
}
