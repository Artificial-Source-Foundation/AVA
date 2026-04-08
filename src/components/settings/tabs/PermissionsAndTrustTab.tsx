/**
 * Permissions & Trust Tab
 *
 * Unified security surface for approval mode, tool rules, and trusted folders.
 */

import { ShieldCheck, ShieldX } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { PermissionMode } from '../../../stores/settings/settings-types'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { ToolRulesSection } from './permissions/ToolRulesSection'
import { TrustedFoldersSection } from './permissions/TrustedFoldersSection'

export const PermissionsAndTrustTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const rules = () => settings().toolRules
  const permissionMode = () => settings().permissionMode

  const modes: PermissionMode[] = ['ask', 'auto-approve', 'bypass']
  const modeLabels: Record<PermissionMode, string> = {
    ask: 'Ask',
    'auto-approve': 'Auto',
    bypass: 'YOLO',
  }

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: SETTINGS_CARD_GAP }}>
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: 'var(--text-primary)',
        }}
      >
        Permissions & Trust
      </h1>

      <SettingsCard
        icon={ShieldCheck}
        title="Global Mode"
        description="Control how AVA asks for permission before running tools."
      >
        <div
          class="flex items-center"
          style={{
            'border-radius': '8px',
            background: 'var(--surface-raised)',
            border: '1px solid var(--border-subtle)',
            padding: '3px',
            gap: '2px',
            width: 'fit-content',
          }}
        >
          <For each={modes}>
            {(mode) => {
              const isActive = () => permissionMode() === mode
              return (
                <button
                  type="button"
                  onClick={() => updateSettings({ permissionMode: mode })}
                  class="flex items-center justify-center"
                  style={{
                    'border-radius': '6px',
                    height: '28px',
                    padding: '0 20px',
                    background: isActive() ? 'var(--accent)' : 'transparent',
                    color: isActive() ? 'var(--text-on-accent)' : 'var(--text-muted)',
                    'font-family': 'Geist, sans-serif',
                    'font-size': '13px',
                    'font-weight': isActive() ? '500' : '400',
                    border: 'none',
                    cursor: 'pointer',
                    transition: 'background 150ms, color 150ms',
                  }}
                >
                  {modeLabels[mode]}
                </button>
              )
            }}
          </For>
        </div>
        <Show when={!modes.includes(permissionMode())}>
          <p class="text-[12px] text-[var(--text-muted)]">
            Current mode from shared config: {permissionMode()}
          </p>
        </Show>
      </SettingsCard>

      <SettingsCard
        title="Tool Rules"
        description="Override the global mode for specific tools or patterns."
      >
        <ToolRulesSection rules={rules()} onUpdateRules={(r) => updateSettings({ toolRules: r })} />
      </SettingsCard>

      <SettingsCard
        icon={ShieldX}
        title="Trusted Folders"
        description="Allow or deny folder access explicitly for desktop safety boundaries."
      >
        <TrustedFoldersSection
          trustedFolders={settings().trustedFolders}
          onUpdate={(trustedFolders) => updateSettings({ trustedFolders })}
        />
      </SettingsCard>
    </div>
  )
}
