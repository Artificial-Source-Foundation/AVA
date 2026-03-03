/**
 * Rules Section — Manage path-targeted coding rules
 *
 * CRUD UI for custom rules with activation modes (always/auto/manual),
 * glob patterns, and toggle switches.
 */

import { Plus, Scale, Trash2, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { CustomRule, RuleActivationMode } from '../../../stores/settings/settings-types'

// ============================================================================
// Activation Badge
// ============================================================================

const ACTIVATION_COLORS: Record<RuleActivationMode, { bg: string; text: string; border: string }> =
  {
    always: {
      bg: 'var(--accent-subtle)',
      text: 'var(--accent)',
      border: 'var(--accent-muted)',
    },
    auto: {
      bg: 'var(--success-subtle, var(--alpha-white-3))',
      text: 'var(--success, var(--text-secondary))',
      border: 'var(--success, var(--border-subtle))',
    },
    manual: {
      bg: 'var(--alpha-white-3)',
      text: 'var(--text-muted)',
      border: 'var(--border-subtle)',
    },
  }

const ActivationBadge: Component<{ mode: RuleActivationMode }> = (props) => {
  const colors = () => ACTIVATION_COLORS[props.mode]
  return (
    <span
      class="px-1 py-0.5 text-[8px] rounded uppercase font-semibold tracking-wider"
      style={{
        background: colors().bg,
        color: colors().text,
        border: `1px solid ${colors().border}`,
      }}
    >
      {props.mode}
    </span>
  )
}

// ============================================================================
// Rule Card
// ============================================================================

const RuleCard: Component<{
  rule: CustomRule
  onToggle: () => void
  onEdit: () => void
  onDelete: () => void
}> = (props) => (
  // biome-ignore lint/a11y/useSemanticElements: card has nested buttons (delete, toggle)
  <div
    role="button"
    tabIndex={0}
    onClick={props.onEdit}
    onKeyDown={(e) => e.key === 'Enter' && props.onEdit()}
    class={`flex items-start gap-3 px-3 py-2.5 rounded-[var(--radius-md)] border transition-colors cursor-pointer ${
      props.rule.enabled
        ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]'
        : 'border-[var(--border-subtle)] bg-[var(--surface)] hover:bg-[var(--alpha-white-3)]'
    }`}
  >
    <Scale
      class={`w-4 h-4 mt-0.5 flex-shrink-0 ${
        props.rule.enabled ? 'text-[var(--accent)]' : 'text-[var(--text-muted)]'
      }`}
    />
    <div class="flex-1 min-w-0">
      <div class="flex items-center gap-2 flex-wrap">
        <span class="text-xs font-medium text-[var(--text-primary)]">{props.rule.name}</span>
        <ActivationBadge mode={props.rule.activation} />
        <For each={props.rule.globs}>
          {(glob) => (
            <span class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-muted)] font-mono">
              {glob}
            </span>
          )}
        </For>
      </div>
      <Show when={props.rule.description}>
        <p class="text-[10px] text-[var(--text-muted)] mt-0.5">{props.rule.description}</p>
      </Show>
    </div>
    <div class="flex items-center gap-1 flex-shrink-0 mt-0.5">
      <button
        type="button"
        onClick={(e) => {
          e.stopPropagation()
          props.onDelete()
        }}
        class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--alpha-white-5)] transition-colors"
        title="Delete rule"
      >
        <Trash2 class="w-3 h-3" />
      </button>
      <button
        type="button"
        onClick={(e) => {
          e.stopPropagation()
          props.onToggle()
        }}
        style={{ width: '28px', height: '16px' }}
        class={`relative rounded-full transition-colors flex-shrink-0 ${
          props.rule.enabled ? 'bg-[var(--accent)]' : 'bg-[var(--alpha-white-10)]'
        }`}
        aria-label={`${props.rule.enabled ? 'Disable' : 'Enable'} ${props.rule.name}`}
      >
        <span
          style={{
            width: '12px',
            height: '12px',
            top: '2px',
            left: props.rule.enabled ? '14px' : '2px',
          }}
          class="absolute rounded-full bg-white shadow-sm transition-[left] duration-150"
        />
      </button>
    </div>
  </div>
)

// ============================================================================
// Rule Form
// ============================================================================

const RuleForm: Component<{
  initial?: CustomRule
  title: string
  onSave: (rule: CustomRule) => void
  onCancel: () => void
}> = (props) => {
  const [name, setName] = createSignal(props.initial?.name ?? '')
  const [description, setDescription] = createSignal(props.initial?.description ?? '')
  const [globInput, setGlobInput] = createSignal(props.initial?.globs.join(', ') ?? '')
  const [activation, setActivation] = createSignal<RuleActivationMode>(
    props.initial?.activation ?? 'auto'
  )
  const [content, setContent] = createSignal(props.initial?.content ?? '')

  const handleSave = () => {
    const trimName = name().trim()
    if (!trimName) return
    const globs = globInput()
      .split(',')
      .map((g) => g.trim())
      .filter(Boolean)
    const id = props.initial?.id ?? `rule-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
    props.onSave({
      id,
      name: trimName,
      description: description().trim(),
      globs,
      activation: activation(),
      content: content().trim(),
      enabled: props.initial?.enabled ?? true,
    })
  }

  const cls =
    'w-full px-2 py-1.5 text-[12px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none'

  return (
    <div class="space-y-3 p-3 rounded-[var(--radius-lg)] border border-[var(--accent-muted)] bg-[var(--accent-subtle)]">
      <div class="flex items-center justify-between">
        <span class="text-xs font-semibold text-[var(--text-primary)]">{props.title}</span>
        <button
          type="button"
          onClick={props.onCancel}
          class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
        >
          <X class="w-3.5 h-3.5" />
        </button>
      </div>
      <div class="space-y-2">
        <input
          type="text"
          value={name()}
          onInput={(e) => setName(e.currentTarget.value)}
          class={cls}
          placeholder="Rule name (e.g. testing-conventions)"
        />
        <input
          type="text"
          value={description()}
          onInput={(e) => setDescription(e.currentTarget.value)}
          class={cls}
          placeholder="Short description"
        />
        <div class="flex gap-2">
          <input
            type="text"
            value={globInput()}
            onInput={(e) => setGlobInput(e.currentTarget.value)}
            class={`${cls} flex-1`}
            placeholder="File globs (comma-separated): **/*.test.ts, **/*.spec.ts"
          />
          <select
            value={activation()}
            onChange={(e) => setActivation(e.currentTarget.value as RuleActivationMode)}
            class={`${cls} w-24`}
          >
            <option value="auto">Auto</option>
            <option value="always">Always</option>
            <option value="manual">Manual</option>
          </select>
        </div>
        <textarea
          value={content()}
          onInput={(e) => setContent(e.currentTarget.value)}
          class={`${cls} resize-none font-mono text-[11px]`}
          rows={8}
          placeholder="Coding instructions injected into the system prompt..."
        />
      </div>
      <div class="flex justify-end gap-2">
        <button
          type="button"
          onClick={props.onCancel}
          class="px-3 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)] transition-colors"
        >
          Cancel
        </button>
        <button
          type="button"
          onClick={handleSave}
          disabled={!name().trim()}
          class="px-3 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:brightness-110 disabled:opacity-40 transition-colors"
        >
          Save
        </button>
      </div>
    </div>
  )
}

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
