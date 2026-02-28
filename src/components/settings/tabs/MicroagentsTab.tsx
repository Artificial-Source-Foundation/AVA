/**
 * Microagents Tab — Manage domain-specific prompt modules (skills)
 *
 * Lists built-in skills with file glob triggers, toggle enable/disable,
 * and allows creating/editing/deleting custom skills.
 */

import { Brain, Pencil, Plus, Trash2, X } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { CustomMicroagent } from '../../../stores/settings/settings-types'

interface MicroagentSkill {
  id: string
  name: string
  description: string
  fileGlobs: string[]
}

const BUILT_IN_SKILLS: MicroagentSkill[] = [
  {
    id: 'react-patterns',
    name: 'React Patterns',
    description: 'Component composition, hooks best practices, and React 19 patterns.',
    fileGlobs: ['**/*.tsx', '**/*.jsx'],
  },
  {
    id: 'python-best-practices',
    name: 'Python Best Practices',
    description: 'PEP 8 style, type hints, async patterns, and Pythonic idioms.',
    fileGlobs: ['**/*.py'],
  },
  {
    id: 'rust-safety',
    name: 'Rust Safety',
    description: 'Ownership rules, lifetime annotations, unsafe blocks, and error handling.',
    fileGlobs: ['**/*.rs'],
  },
  {
    id: 'go-conventions',
    name: 'Go Conventions',
    description: 'Go idioms, error handling, goroutine patterns, and module layout.',
    fileGlobs: ['**/*.go'],
  },
  {
    id: 'typescript-strict',
    name: 'TypeScript Strict',
    description: 'Strict mode patterns, utility types, generics, and type narrowing.',
    fileGlobs: ['**/*.ts'],
  },
  {
    id: 'css-architecture',
    name: 'CSS Architecture',
    description: 'BEM methodology, CSS custom properties, responsive design, and specificity.',
    fileGlobs: ['**/*.css', '**/*.scss'],
  },
  {
    id: 'docker-best-practices',
    name: 'Docker Best Practices',
    description: 'Multi-stage builds, layer caching, security scanning, and compose patterns.',
    fileGlobs: ['**/Dockerfile', '**/Dockerfile.*', '**/docker-compose*.yml'],
  },
  {
    id: 'sql-optimization',
    name: 'SQL Optimization',
    description: 'Query optimization, indexing strategies, joins, and schema design.',
    fileGlobs: ['**/*.sql'],
  },
]

// ============================================================================
// Skill Card (shared for built-in and custom)
// ============================================================================

const SkillCard: Component<{
  name: string
  description: string
  fileGlobs: string[]
  isEnabled: boolean
  onToggle: () => void
  onEdit?: () => void
  onDelete?: () => void
  isCustom?: boolean
}> = (props) => (
  <div
    class={`flex items-start gap-3 px-3 py-2.5 rounded-[var(--radius-md)] border transition-colors ${
      props.isEnabled
        ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]'
        : 'border-[var(--border-subtle)] bg-[var(--surface)]'
    }`}
  >
    <Brain
      class={`w-4 h-4 mt-0.5 flex-shrink-0 ${
        props.isEnabled ? 'text-[var(--accent)]' : 'text-[var(--text-muted)]'
      }`}
    />
    <div class="flex-1 min-w-0">
      <div class="flex items-center gap-2">
        <span class="text-xs font-medium text-[var(--text-primary)]">{props.name}</span>
        <Show when={props.isCustom}>
          <span class="px-1 py-0.5 text-[8px] rounded bg-[var(--warning-subtle)] text-[var(--warning)] border border-[var(--warning)]/20">
            Custom
          </span>
        </Show>
        <For each={props.fileGlobs}>
          {(glob) => (
            <span class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-muted)] font-mono">
              {glob}
            </span>
          )}
        </For>
      </div>
      <p class="text-[10px] text-[var(--text-muted)] mt-0.5">{props.description}</p>
    </div>
    <div class="flex items-center gap-1 flex-shrink-0 mt-0.5">
      <Show when={props.onEdit}>
        <button
          type="button"
          onClick={props.onEdit}
          class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-05)] transition-colors"
          title="Edit skill"
        >
          <Pencil class="w-3 h-3" />
        </button>
      </Show>
      <Show when={props.onDelete}>
        <button
          type="button"
          onClick={props.onDelete}
          class="p-1 rounded-[var(--radius-sm)] text-[var(--text-muted)] hover:text-[var(--error)] hover:bg-[var(--alpha-white-05)] transition-colors"
          title="Delete skill"
        >
          <Trash2 class="w-3 h-3" />
        </button>
      </Show>
      <button
        type="button"
        onClick={props.onToggle}
        class={`relative w-8 h-[18px] rounded-full transition-colors ${
          props.isEnabled ? 'bg-[var(--accent)]' : 'bg-[var(--alpha-white-10)]'
        }`}
        aria-label={`${props.isEnabled ? 'Disable' : 'Enable'} ${props.name}`}
      >
        <span
          class={`absolute top-[2px] w-[14px] h-[14px] rounded-full bg-white shadow-sm transition-transform ${
            props.isEnabled ? 'translate-x-[16px]' : 'translate-x-[2px]'
          }`}
        />
      </button>
    </div>
  </div>
)

// ============================================================================
// Custom Skill Form
// ============================================================================

const CustomSkillForm: Component<{
  initial?: CustomMicroagent
  onSave: (skill: CustomMicroagent) => void
  onCancel: () => void
}> = (props) => {
  const [name, setName] = createSignal(props.initial?.name ?? '')
  const [description, setDescription] = createSignal(props.initial?.description ?? '')
  const [globInput, setGlobInput] = createSignal(props.initial?.fileGlobs.join(', ') ?? '')
  const [instructions, setInstructions] = createSignal(props.initial?.instructions ?? '')

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
    })
  }

  const inputClass =
    'w-full px-2 py-1.5 text-[12px] rounded-[var(--radius-md)] bg-[var(--surface-raised)] text-[var(--text-primary)] border border-[var(--border-subtle)] focus:border-[var(--accent)] outline-none'

  return (
    <div class="space-y-3 p-3 rounded-[var(--radius-lg)] border border-[var(--accent-muted)] bg-[var(--accent-subtle)]">
      <div class="flex items-center justify-between">
        <span class="text-xs font-semibold text-[var(--text-primary)]">
          {props.initial ? 'Edit Skill' : 'New Custom Skill'}
        </span>
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
          class={inputClass}
          placeholder="Skill name"
        />
        <input
          type="text"
          value={description()}
          onInput={(e) => setDescription(e.currentTarget.value)}
          class={inputClass}
          placeholder="Short description"
        />
        <input
          type="text"
          value={globInput()}
          onInput={(e) => setGlobInput(e.currentTarget.value)}
          class={inputClass}
          placeholder="File globs (comma-separated): **/*.tsx, **/*.jsx"
        />
        <textarea
          value={instructions()}
          onInput={(e) => setInstructions(e.currentTarget.value)}
          class={`${inputClass} resize-none`}
          rows={4}
          placeholder="Instructions injected into the system prompt when matching files are detected..."
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
          class="px-3 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:brightness-110 disabled:opacity-40 disabled:cursor-not-allowed transition-colors"
        >
          {props.initial ? 'Save' : 'Create'}
        </button>
      </div>
    </div>
  )
}

// ============================================================================
// Main Tab
// ============================================================================

export const MicroagentsTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const enabledSet = createMemo(() => new Set(settings().enabledMicroagents))
  const activeCount = createMemo(() => enabledSet().size)

  const [showForm, setShowForm] = createSignal(false)
  const [editingSkill, setEditingSkill] = createSignal<CustomMicroagent | null>(null)

  const toggleSkill = (id: string) => {
    const current = settings().enabledMicroagents
    const next = current.includes(id) ? current.filter((s) => s !== id) : [...current, id]
    updateSettings({ enabledMicroagents: next })
  }

  const saveCustomSkill = (skill: CustomMicroagent) => {
    const existing = settings().customMicroagents ?? []
    const idx = existing.findIndex((s) => s.id === skill.id)
    const updated =
      idx >= 0 ? existing.map((s) => (s.id === skill.id ? skill : s)) : [...existing, skill]
    updateSettings({ customMicroagents: updated })
    // Auto-enable new skills
    if (idx < 0) {
      const enabled = settings().enabledMicroagents
      if (!enabled.includes(skill.id)) {
        updateSettings({ enabledMicroagents: [...enabled, skill.id] })
      }
    }
    setShowForm(false)
    setEditingSkill(null)
  }

  const deleteCustomSkill = (id: string) => {
    const existing = settings().customMicroagents ?? []
    updateSettings({
      customMicroagents: existing.filter((s) => s.id !== id),
      enabledMicroagents: settings().enabledMicroagents.filter((s) => s !== id),
    })
  }

  return (
    <div class="space-y-4">
      <div class="flex items-center justify-between">
        <div>
          <h3 class="text-sm font-semibold text-[var(--text-primary)]">Microagents</h3>
          <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
            Domain-specific prompt modules that activate based on file types.
          </p>
        </div>
        <div class="flex items-center gap-2">
          <span class="px-2 py-0.5 text-[10px] rounded-full bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent-muted)]">
            {activeCount()} active
          </span>
          <button
            type="button"
            onClick={() => {
              setEditingSkill(null)
              setShowForm(true)
            }}
            class="inline-flex items-center gap-1 px-2 py-1 text-[11px] rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:brightness-110 transition-colors"
          >
            <Plus class="w-3 h-3" />
            Create Skill
          </button>
        </div>
      </div>

      <Show when={showForm()}>
        <CustomSkillForm
          initial={editingSkill() ?? undefined}
          onSave={saveCustomSkill}
          onCancel={() => {
            setShowForm(false)
            setEditingSkill(null)
          }}
        />
      </Show>

      {/* Custom skills */}
      <Show when={(settings().customMicroagents ?? []).length > 0}>
        <div class="space-y-1.5">
          <h4 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
            Custom Skills
          </h4>
          <For each={settings().customMicroagents ?? []}>
            {(skill) => (
              <SkillCard
                name={skill.name}
                description={skill.description}
                fileGlobs={skill.fileGlobs}
                isEnabled={enabledSet().has(skill.id)}
                onToggle={() => toggleSkill(skill.id)}
                onEdit={() => {
                  setEditingSkill(skill)
                  setShowForm(true)
                }}
                onDelete={() => deleteCustomSkill(skill.id)}
                isCustom
              />
            )}
          </For>
        </div>
      </Show>

      {/* Built-in skills */}
      <div class="space-y-1.5">
        <h4 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
          Built-in Skills
        </h4>
        <For each={BUILT_IN_SKILLS}>
          {(skill) => (
            <SkillCard
              name={skill.name}
              description={skill.description}
              fileGlobs={skill.fileGlobs}
              isEnabled={enabledSet().has(skill.id)}
              onToggle={() => toggleSkill(skill.id)}
            />
          )}
        </For>
      </div>

      <p class="text-[10px] text-[var(--text-muted)]">
        Enabled skills inject domain-specific guidance into the system prompt when matching files
        are detected in your project.
      </p>
    </div>
  )
}
