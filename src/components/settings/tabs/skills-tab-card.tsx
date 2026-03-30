/**
 * Skills Tab — Shared Components
 *
 * SkillCard (clickable card with toggle/edit/delete) and SkillForm (inline editor).
 */

import { Brain, Pencil, Trash2, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { CustomSkill, SkillActivationMode } from '../../../stores/settings/settings-types'
import { SETTINGS_FORM_INPUT_CLASS } from '../settings-constants'

// ============================================================================
// Skill Card
// ============================================================================

export const SkillCard: Component<{
  name: string
  description: string
  fileGlobs: string[]
  isEnabled: boolean
  isCustom?: boolean
  isOverride?: boolean
  onToggle: () => void
  onClick: () => void
  onDelete: () => void
}> = (props) => (
  // biome-ignore lint/a11y/useSemanticElements: card has nested buttons (delete, toggle)
  <div
    role="button"
    tabIndex={0}
    onClick={() => props.onClick()}
    onKeyDown={(e) => e.key === 'Enter' && props.onClick()}
    class={`flex items-start gap-3 px-3 py-2.5 rounded-[var(--radius-md)] border transition-colors cursor-pointer ${
      props.isEnabled
        ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]'
        : 'border-[var(--border-subtle)] bg-[var(--surface)] hover:bg-[var(--alpha-white-3)]'
    }`}
  >
    <Brain
      class={`w-4 h-4 mt-0.5 flex-shrink-0 ${
        props.isEnabled ? 'text-[var(--accent)]' : 'text-[var(--text-muted)]'
      }`}
    />
    <div class="flex-1 min-w-0">
      <div class="flex items-center gap-2 flex-wrap">
        <span class="text-xs font-medium text-[var(--text-primary)]">{props.name}</span>
        <Show when={props.isCustom}>
          <span class="px-1 py-0.5 text-[var(--settings-text-caption)] rounded bg-[var(--warning-subtle)] text-[var(--warning)] border border-[var(--warning)]/20">
            Custom
          </span>
        </Show>
        <Show when={props.isOverride}>
          <span class="px-1 py-0.5 text-[var(--settings-text-caption)] rounded bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent)]/20">
            Modified
          </span>
        </Show>
        <For each={props.fileGlobs}>
          {(glob) => (
            <span class="px-1.5 py-0.5 text-[var(--settings-text-caption)] rounded-[var(--radius-sm)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-muted)] font-mono">
              {glob}
            </span>
          )}
        </For>
      </div>
      <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)] mt-0.5">
        {props.description}
      </p>
    </div>
    <div class="flex items-center gap-1 flex-shrink-0 mt-0.5">
      <button
        type="button"
        onClick={(e) => {
          e.stopPropagation()
          props.onClick()
        }}
        class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)] transition-colors"
        title="Edit skill"
      >
        <Pencil class="w-3 h-3" />
      </button>
      <button
        type="button"
        onClick={(e) => {
          e.stopPropagation()
          props.onDelete()
        }}
        class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--alpha-white-5)] transition-colors"
        title="Delete skill"
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
          props.isEnabled ? 'bg-[var(--accent)]' : 'bg-[var(--alpha-white-10)]'
        }`}
        aria-label={`${props.isEnabled ? 'Disable' : 'Enable'} ${props.name}`}
      >
        <span
          style={{
            width: '12px',
            height: '12px',
            top: '2px',
            left: props.isEnabled ? '14px' : '2px',
          }}
          class="absolute rounded-full bg-white shadow-sm transition-[left] duration-150"
        />
      </button>
    </div>
  </div>
)

// ============================================================================
// Skill Form
// ============================================================================

export const SkillForm: Component<{
  initial?: CustomSkill
  title: string
  onSave: (skill: CustomSkill) => void
  onCancel: () => void
}> = (props) => {
  const [name, setName] = createSignal(props.initial?.name ?? '')
  const [description, setDescription] = createSignal(props.initial?.description ?? '')
  const [globInput, setGlobInput] = createSignal(props.initial?.fileGlobs.join(', ') ?? '')
  const [instructions, setInstructions] = createSignal(props.initial?.instructions ?? '')
  const [activation, setActivation] = createSignal<SkillActivationMode>(
    props.initial?.activation ?? 'auto'
  )

  const handleSave = () => {
    const trimName = name().trim()
    if (!trimName) return
    const globs = globInput()
      .split(',')
      .map((g) => g.trim())
      .filter(Boolean)
    const id = props.initial?.id ?? `custom-${Date.now()}-${Math.random().toString(36).slice(2, 6)}`
    props.onSave({
      id,
      name: trimName,
      description: description().trim(),
      fileGlobs: globs,
      instructions: instructions().trim(),
      activation: activation(),
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
          placeholder="Skill name"
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
            placeholder="File globs (comma-separated): **/*.tsx, **/*.jsx"
          />
          <select
            value={activation()}
            onChange={(e) => setActivation(e.currentTarget.value as SkillActivationMode)}
            class={`${cls} w-28`}
          >
            <option value="auto">Auto</option>
            <option value="agent">Agent</option>
            <option value="always">Always</option>
            <option value="manual">Manual</option>
          </select>
        </div>
        <textarea
          value={instructions()}
          onInput={(e) => setInstructions(e.currentTarget.value)}
          class={`${cls} resize-none font-mono text-[var(--settings-text-button)]`}
          rows={6}
          placeholder="Instructions injected into the system prompt when matching files are detected..."
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
