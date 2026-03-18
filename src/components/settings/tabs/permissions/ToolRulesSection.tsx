/**
 * Tool Rules Section
 *
 * Per-tool approval rules with add/remove/reorder support.
 */

import { Plus, Trash2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { ToolApprovalRule } from '../../../../stores/settings/settings-types'
import { SectionHeader } from './permissions-helpers'

export interface ToolRulesSectionProps {
  rules: ToolApprovalRule[]
  onUpdateRules: (rules: ToolApprovalRule[]) => void
}

export const ToolRulesSection: Component<ToolRulesSectionProps> = (props) => {
  const [newTool, setNewTool] = createSignal('')
  const [newAction, setNewAction] = createSignal<'allow' | 'ask' | 'deny'>('ask')

  const addRule = () => {
    const tool = newTool().trim()
    if (!tool) return
    const rule: ToolApprovalRule = { tool, action: newAction() }
    props.onUpdateRules([...props.rules, rule])
    setNewTool('')
    setNewAction('ask')
  }

  const removeRule = (index: number) => {
    props.onUpdateRules(props.rules.filter((_, i) => i !== index))
  }

  const moveRule = (index: number, direction: -1 | 1) => {
    const r = [...props.rules]
    const newIdx = index + direction
    if (newIdx < 0 || newIdx >= r.length) return
    ;[r[index], r[newIdx]] = [r[newIdx], r[index]]
    props.onUpdateRules(r)
  }

  return (
    <div class="pt-2 border-t border-[var(--border-subtle)]">
      <SectionHeader title="Tool Rules (first match wins)" />
      <Show
        when={props.rules.length > 0}
        fallback={
          <p class="text-[13px] text-[var(--text-muted)] py-2">
            No custom rules. Add rules to override per-tool behavior.
          </p>
        }
      >
        <div class="space-y-1 mb-2">
          <For each={props.rules}>
            {(rule, index) => (
              <div class="flex items-center gap-2 py-1 group">
                <span class="text-[14px] text-[var(--text-secondary)] font-mono flex-1 truncate">
                  {rule.tool}
                </span>
                <span
                  class="text-[12px] px-2 py-0.5 rounded"
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
                  <Show when={index() < props.rules.length - 1}>
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
          class="flex-1 px-3 py-2 text-[14px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none"
        />
        <select
          value={newAction()}
          onChange={(e) => setNewAction(e.currentTarget.value as 'allow' | 'ask' | 'deny')}
          class="px-3 py-2 text-[14px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] outline-none"
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
          <Plus class="w-4 h-4" />
        </button>
      </div>
    </div>
  )
}
