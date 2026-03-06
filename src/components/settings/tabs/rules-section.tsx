/**
 * Rules Section — Manage path-targeted coding rules
 *
 * CRUD UI for custom rules with activation modes (always/auto/manual),
 * glob patterns, and toggle switches.
 */

import { Plus } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { CustomRule } from '../../../stores/settings/settings-types'
import { RuleCard } from './rules/RuleCard'
import { RuleForm } from './rules/RuleForm'

// ============================================================================
// Rules Section (exported)
// ============================================================================

export const RulesSection: Component = () => {
  const { settings, updateSettings } = useSettings()

  const rules = () => settings().customRules ?? []
  const activeCount = () => rules().filter((r) => r.enabled).length

  const [showForm, setShowForm] = createSignal(false)
  const [editingRule, setEditingRule] = createSignal<CustomRule | null>(null)
  const [formTitle, setFormTitle] = createSignal('New Rule')

  const toggleRule = (id: string) => {
    updateSettings({
      customRules: rules().map((r) => (r.id === id ? { ...r, enabled: !r.enabled } : r)),
    })
  }

  const handleEdit = (rule: CustomRule) => {
    setEditingRule(rule)
    setFormTitle(`Edit: ${rule.name}`)
    setShowForm(true)
  }

  const handleCreateNew = () => {
    setEditingRule(null)
    setFormTitle('New Rule')
    setShowForm(true)
  }

  const saveRule = (rule: CustomRule) => {
    const existing = rules()
    const idx = existing.findIndex((r) => r.id === rule.id)
    const updated =
      idx >= 0 ? existing.map((r) => (r.id === rule.id ? rule : r)) : [...existing, rule]
    updateSettings({ customRules: updated })
    setShowForm(false)
    setEditingRule(null)
  }

  const deleteRule = (id: string) => {
    updateSettings({ customRules: rules().filter((r) => r.id !== id) })
  }

  return (
    <div class="space-y-3">
      {/* Header */}
      <div class="flex items-center justify-between">
        <div>
          <h3 class="text-sm font-semibold text-[var(--text-primary)]">Rules</h3>
          <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
            Path-targeted coding instructions that inject into the system prompt.
          </p>
        </div>
        <div class="flex items-center gap-2">
          <span class="px-2 py-0.5 text-[10px] rounded-full bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent-muted)]">
            {activeCount()} active
          </span>
          <button
            type="button"
            onClick={handleCreateNew}
            class="inline-flex items-center gap-1 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:brightness-110 transition-colors"
          >
            <Plus class="w-3 h-3" />
            Create Rule
          </button>
        </div>
      </div>

      {/* Inline form */}
      <Show when={showForm()}>
        <RuleForm
          initial={editingRule() ?? undefined}
          title={formTitle()}
          onSave={saveRule}
          onCancel={() => {
            setShowForm(false)
            setEditingRule(null)
          }}
        />
      </Show>

      {/* Rule cards */}
      <Show
        when={rules().length > 0}
        fallback={
          <p class="text-[10px] text-[var(--text-muted)] italic py-2">
            No rules yet. Create a rule or add .md files to .ava/rules/ in your project.
          </p>
        }
      >
        <div class="space-y-1.5">
          <For each={rules()}>
            {(rule) => (
              <RuleCard
                rule={rule}
                onToggle={() => toggleRule(rule.id)}
                onEdit={() => handleEdit(rule)}
                onDelete={() => deleteRule(rule.id)}
              />
            )}
          </For>
        </div>
      </Show>
    </div>
  )
}
