/**
 * Rules Section — Pencil design revamp
 *
 * Simple list of rule files with "active" badge per the Pencil design.
 * CRUD still available via edit/create buttons.
 */

import { Plus } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { CustomRule } from '../../../stores/settings/settings-types'
import { RuleForm } from './rules/RuleForm'

export const RulesSection: Component = () => {
  const { settings, updateSettings } = useSettings()

  const rules = () => settings().customRules ?? []

  const [showForm, setShowForm] = createSignal(false)
  const [editingRule, setEditingRule] = createSignal<CustomRule | null>(null)
  const [formTitle, setFormTitle] = createSignal('New Rule')

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

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '12px' }}>
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

      {/* Rule file rows */}
      <Show
        when={rules().length > 0}
        fallback={
          <div
            style={{
              display: 'flex',
              'align-items': 'center',
              'justify-content': 'space-between',
            }}
          >
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
                'font-style': 'italic',
              }}
            >
              No rules yet. Add .md files to .ava/rules/ or create one.
            </span>
            <button
              type="button"
              onClick={handleCreateNew}
              style={{
                display: 'flex',
                'align-items': 'center',
                gap: '4px',
                padding: '4px 10px',
                background: '#0A84FF',
                'border-radius': '6px',
                border: 'none',
                cursor: 'pointer',
                'font-family': 'Geist, sans-serif',
                'font-size': '10px',
                'font-weight': '500',
                color: '#FFFFFF',
              }}
            >
              <Plus size={10} />
              Create Rule
            </button>
          </div>
        }
      >
        <For each={rules()}>
          {(rule) => (
            // biome-ignore lint/a11y/useKeyWithClickEvents: rule row selection
            // biome-ignore lint/a11y/useSemanticElements: card-style row
            <div
              role="button"
              tabIndex={0}
              onClick={() => handleEdit(rule)}
              style={{
                display: 'flex',
                'align-items': 'center',
                'justify-content': 'space-between',
                padding: '8px 12px',
                background: '#ffffff04',
                border: '1px solid #ffffff0a',
                'border-radius': '8px',
                cursor: 'pointer',
                transition: 'border-color 0.15s',
              }}
            >
              <span
                style={{
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '12px',
                  color: '#F5F5F7',
                }}
              >
                {rule.name}
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '10px',
                  'font-weight': '500',
                  color: rule.enabled ? '#34C759' : '#48484A',
                }}
              >
                {rule.enabled ? 'active' : 'inactive'}
              </span>
            </div>
          )}
        </For>
      </Show>
    </div>
  )
}
