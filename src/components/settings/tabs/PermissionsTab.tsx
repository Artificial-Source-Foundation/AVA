/**
 * Permissions Settings Tab
 *
 * Global permission mode, quick toggles, per-tool approval rules,
 * and always-approved tool list.
 */

import { type Component, For } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { PermissionMode } from '../../../stores/settings/settings-types'
import { ApprovedToolsSection } from './permissions/ApprovedToolsSection'
import { SectionHeader, segmentedBtn } from './permissions/permissions-helpers'
import { ToolRulesSection } from './permissions/ToolRulesSection'

export const PermissionsTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const rules = () => settings().toolRules
  const approvedTools = () => settings().autoApprovedTools

  const modes: PermissionMode[] = ['ask', 'auto-approve', 'bypass']
  const modeLabels: Record<PermissionMode, string> = {
    ask: 'Ask',
    'auto-approve': 'Auto',
    bypass: 'Bypass',
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
      permissionMode: 'bypass',
      toolRules: [],
    })
  }

  return (
    <div class="space-y-5">
      {/* Global Mode */}
      <div>
        <SectionHeader title="Global Mode" />
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Permission mode</span>
            <p class="text-[10px] text-[var(--text-muted)]">
              Controls how tool executions are approved
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

        <div class="mt-2 flex items-center gap-1.5">
          <button
            type="button"
            class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--warning)]"
            onClick={() => applyPreset('strict')}
          >
            Strict
          </button>
          <button
            type="button"
            class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--accent)]"
            onClick={() => applyPreset('balanced')}
          >
            Balanced
          </button>
          <button
            type="button"
            class="px-2 py-1 text-[10px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] border border-[var(--border-subtle)] hover:border-[var(--error)]"
            onClick={() => applyPreset('yolo')}
          >
            YOLO
          </button>
        </div>
      </div>

      {/* Tool Rules */}
      <ToolRulesSection rules={rules()} onUpdateRules={(r) => updateSettings({ toolRules: r })} />

      {/* Always-Approved Tools */}
      <ApprovedToolsSection
        tools={approvedTools()}
        onUpdateTools={(t) => updateSettings({ autoApprovedTools: t })}
      />
    </div>
  )
}
