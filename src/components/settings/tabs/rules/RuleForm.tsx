/**
 * Rule Form
 *
 * Inline form for creating/editing custom coding rules.
 */

import { X } from 'lucide-solid'
import { type Component, createSignal } from 'solid-js'
import type { CustomRule, RuleActivationMode } from '../../../../stores/settings/settings-types'
import { SETTINGS_FORM_INPUT_CLASS } from '../../settings-constants'

export const RuleForm: Component<{
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

  const cls = SETTINGS_FORM_INPUT_CLASS

  return (
    <div class="space-y-3 p-3 rounded-[var(--radius-lg)] border border-[var(--accent-muted)] bg-[var(--accent-subtle)]">
      <div class="flex items-center justify-between">
        <span class="text-xs font-semibold text-[var(--text-primary)]">{props.title}</span>
        <button
          type="button"
          onClick={() => props.onCancel()}
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
          class={`${cls} resize-none font-mono text-[var(--settings-text-button)]`}
          rows={8}
          placeholder="Coding instructions injected into the system prompt..."
        />
      </div>
      <div class="flex justify-end gap-2">
        <button
          type="button"
          onClick={() => props.onCancel()}
          class="px-3 py-1 text-[var(--settings-text-button)] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-secondary)] hover:bg-[var(--alpha-white-8)] transition-colors"
        >
          Cancel
        </button>
        <button
          type="button"
          onClick={handleSave}
          disabled={!name().trim()}
          class="px-3 py-1 text-[var(--settings-text-button)] rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:bg-[var(--accent-hover)] disabled:opacity-40 transition-colors"
        >
          Save
        </button>
      </div>
    </div>
  )
}
