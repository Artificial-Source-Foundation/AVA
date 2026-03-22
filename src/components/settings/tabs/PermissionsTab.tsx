/**
 * Permissions Settings Tab
 *
 * Global permission mode, quick toggles, per-tool approval rules,
 * and always-approved tool list.
 */

import { ListChecks, Shield, ShieldCheck } from 'lucide-solid'
import { type Component, For } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { PermissionMode } from '../../../stores/settings/settings-types'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { ApprovedToolsSection } from './permissions/ApprovedToolsSection'
import { segmentedBtn } from './permissions/permissions-helpers'
import { ToolRulesSection } from './permissions/ToolRulesSection'

export const PermissionsTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const rules = () => settings().toolRules
  const approvedTools = () => settings().autoApprovedTools

  const modes: PermissionMode[] = ['ask', 'auto-approve']
  const modeLabels: Record<PermissionMode, string> = {
    ask: 'Ask',
    'auto-approve': 'Auto',
    bypass: 'Auto', // 'bypass' is legacy — treated as 'auto-approve'
  }

  const applyPreset = (preset: 'strict' | 'balanced' | 'yolo') => {
    if (preset === 'strict') {
      updateSettings({
        permissionMode: 'ask',
        toolRules: [
          { tool: 'bash', action: 'ask' },
          { tool: 'write_*', action: 'ask' },
          { tool: 'delete_file', action: 'deny' },
        ],
      })
      return
    }

    if (preset === 'balanced') {
      updateSettings({
        permissionMode: 'ask',
        toolRules: [
          { tool: 'read_*', action: 'allow' },
          { tool: 'glob', action: 'allow' },
          { tool: 'grep', action: 'allow' },
          { tool: 'bash', action: 'ask' },
        ],
      })
      return
    }

    updateSettings({
      permissionMode: 'auto-approve',
      toolRules: [],
    })
  }

  return (
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      {/* Global Mode */}
      <SettingsCard
        icon={Shield}
        title="Global Mode"
        description="Controls how tool executions are approved"
      >
        <div class="flex items-center justify-between py-2">
          <div>
            <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)]">
              Permission mode
            </span>
            <p class="text-[var(--settings-text-description)] text-[var(--text-muted)]">
              Choose between manual approval or automatic execution
            </p>
          </div>
          <div class="flex gap-1">
            <For each={modes}>
              {(mode) => (
                <button
                  type="button"
                  onClick={() => updateSettings({ permissionMode: mode })}
                  class={segmentedBtn(settings().permissionMode === mode)}
                >
                  {modeLabels[mode]}
                </button>
              )}
            </For>
          </div>
        </div>

        <div class="mt-3 flex items-center gap-2">
          <button
            type="button"
            class="px-3 py-1.5 text-[var(--settings-text-description)] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--warning)]"
            onClick={() => applyPreset('strict')}
          >
            Strict
          </button>
          <button
            type="button"
            class="px-3 py-1.5 text-[var(--settings-text-description)] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent)]"
            onClick={() => applyPreset('balanced')}
          >
            Balanced
          </button>
          <button
            type="button"
            class="px-3 py-1.5 text-[var(--settings-text-description)] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--error)]"
            onClick={() => applyPreset('yolo')}
          >
            YOLO
          </button>
        </div>
      </SettingsCard>

      {/* Tool Rules */}
      <SettingsCard
        icon={ListChecks}
        title="Tool Rules"
        description="Per-tool approval rules — first match wins"
      >
        <ToolRulesSection rules={rules()} onUpdateRules={(r) => updateSettings({ toolRules: r })} />
      </SettingsCard>

      {/* Always-Approved Tools */}
      <SettingsCard
        icon={ShieldCheck}
        title="Always-Approved Tools"
        description="Tools that skip approval regardless of permission mode"
      >
        <ApprovedToolsSection
          tools={approvedTools()}
          onUpdateTools={(t) => updateSettings({ autoApprovedTools: t })}
        />
      </SettingsCard>
    </div>
  )
}
