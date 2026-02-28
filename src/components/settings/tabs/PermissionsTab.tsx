/**
 * Permissions Settings Tab
 *
 * Global permission mode, quick toggles, per-tool approval rules,
 * and always-approved tool list.
 */

import { Plus, Trash2, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { PermissionMode, ToolApprovalRule } from '../../../stores/settings/settings-types'

// ============================================================================
// Shared helpers
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

// ============================================================================
// Main Tab
// ============================================================================

export const PermissionsTab: Component = () => {
  const { settings, updateSettings } = useSettings()
  const [newTool, setNewTool] = createSignal('')
  const [newAction, setNewAction] = createSignal<'allow' | 'ask' | 'deny'>('ask')
  const [newApprovedTool, setNewApprovedTool] = createSignal('')

  const rules = () => settings().toolRules
  const approvedTools = () => settings().autoApprovedTools

  const addRule = () => {
    const tool = newTool().trim()
    if (!tool) return
    const rule: ToolApprovalRule = { tool, action: newAction() }
    updateSettings({ toolRules: [...rules(), rule] })
    setNewTool('')
    setNewAction('ask')
  }

  const removeRule = (index: number) => {
    updateSettings({ toolRules: rules().filter((_, i) => i !== index) })
  }

  const moveRule = (index: number, direction: -1 | 1) => {
    const r = [...rules()]
    const newIdx = index + direction
    if (newIdx < 0 || newIdx >= r.length) return
    ;[r[index], r[newIdx]] = [r[newIdx], r[index]]
    updateSettings({ toolRules: r })
  }

  const addApprovedTool = () => {
    const tool = newApprovedTool().trim()
    if (!tool || approvedTools().includes(tool)) return
    updateSettings({ autoApprovedTools: [...approvedTools(), tool] })
    setNewApprovedTool('')
  }

  const removeApprovedTool = (tool: string) => {
    updateSettings({ autoApprovedTools: approvedTools().filter((t) => t !== tool) })
  }

  const modes: PermissionMode[] = ['ask', 'auto-approve', 'bypass']
  const modeLabels: Record<PermissionMode, string> = {
    ask: 'Ask',
    'auto-approve': 'Auto',
    bypass: 'Bypass',
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
      </div>

      {/* Tool Rules */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Tool Rules (first match wins)" />
        <Show
          when={rules().length > 0}
          fallback={
            <p class="text-[10px] text-[var(--text-muted)] py-2">
              No custom rules. Add rules to override per-tool behavior.
            </p>
          }
        >
          <div class="space-y-1 mb-2">
            <For each={rules()}>
              {(rule, index) => (
                <div class="flex items-center gap-2 py-1 group">
                  <span class="text-xs text-[var(--text-secondary)] font-mono flex-1 truncate">
                    {rule.tool}
                  </span>
                  <span
                    class="text-[10px] px-1.5 py-0.5 rounded"
                    classList={{
                      'bg-[color-mix(in_srgb,var(--success)_15%,transparent)] text-[var(--success)]':
                        rule.action === 'allow',
                      'bg-[color-mix(in_srgb,var(--warning)_15%,transparent)] text-[var(--warning)]':
                        rule.action === 'ask',
                      'bg-[color-mix(in_srgb,var(--error)_15%,transparent)] text-[var(--error)]':
                        rule.action === 'deny',
                    }}
                  >
                    {rule.action}
                  </span>
                  <div class="flex items-center gap-0.5 opacity-0 group-hover:opacity-100 transition-opacity">
                    <Show when={index() > 0}>
                      <button
                        type="button"
                        onClick={() => moveRule(index(), -1)}
                        class="text-[9px] text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
                      >
                        ^
                      </button>
                    </Show>
                    <Show when={index() < rules().length - 1}>
                      <button
                        type="button"
                        onClick={() => moveRule(index(), 1)}
                        class="text-[9px] text-[var(--text-muted)] hover:text-[var(--text-secondary)]"
                      >
                        v
                      </button>
                    </Show>
                    <button
                      type="button"
                      onClick={() => removeRule(index())}
                      class="p-0.5 text-[var(--text-muted)] hover:text-[var(--error)]"
                    >
                      <Trash2 class="w-3 h-3" />
                    </button>
                  </div>
                </div>
              )}
            </For>
          </div>
        </Show>

        {/* Add rule */}
        <div class="flex items-center gap-2 mt-2">
          <input
            type="text"
            value={newTool()}
            onInput={(e) => setNewTool(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') addRule()
            }}
            placeholder="Tool name or glob (e.g. bash, write_*)"
            class="flex-1 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
          />
          <select
            value={newAction()}
            onChange={(e) => setNewAction(e.currentTarget.value as 'allow' | 'ask' | 'deny')}
            class="px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] outline-none"
          >
            <option value="allow">Allow</option>
            <option value="ask">Ask</option>
            <option value="deny">Deny</option>
          </select>
          <button
            type="button"
            onClick={addRule}
            disabled={!newTool().trim()}
            class="p-1 text-[var(--accent)] hover:bg-[var(--accent-subtle)] rounded-[var(--radius-md)] disabled:opacity-50"
          >
            <Plus class="w-3.5 h-3.5" />
          </button>
        </div>
      </div>

      {/* Always-Approved Tools */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Always-Approved Tools" />
        <Show
          when={approvedTools().length > 0}
          fallback={
            <p class="text-[10px] text-[var(--text-muted)] py-2">
              No always-approved tools configured.
            </p>
          }
        >
          <div class="flex flex-wrap gap-1 mb-2">
            <For each={approvedTools()}>
              {(tool) => (
                <span class="inline-flex items-center gap-1 px-2 py-0.5 text-[11px] bg-[var(--surface-raised)] text-[var(--text-secondary)] rounded-[var(--radius-md)] border border-[var(--border-subtle)]">
                  <span class="font-mono">{tool}</span>
                  <button
                    type="button"
                    onClick={() => removeApprovedTool(tool)}
                    class="text-[var(--text-muted)] hover:text-[var(--error)]"
                  >
                    <X class="w-2.5 h-2.5" />
                  </button>
                </span>
              )}
            </For>
          </div>
        </Show>
        <div class="flex items-center gap-2">
          <input
            type="text"
            value={newApprovedTool()}
            onInput={(e) => setNewApprovedTool(e.currentTarget.value)}
            onKeyDown={(e) => {
              if (e.key === 'Enter') addApprovedTool()
            }}
            placeholder="Tool name (e.g. read_file, glob)"
            class="flex-1 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
          />
          <button
            type="button"
            onClick={addApprovedTool}
            disabled={!newApprovedTool().trim()}
            class="p-1 text-[var(--accent)] hover:bg-[var(--accent-subtle)] rounded-[var(--radius-md)] disabled:opacity-50"
          >
            <Plus class="w-3.5 h-3.5" />
          </button>
        </div>
      </div>
    </div>
  )
}
