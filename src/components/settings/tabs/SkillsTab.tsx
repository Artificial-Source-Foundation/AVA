/**
 * Skills Tab — Manage domain-specific prompt modules
 *
 * All skills (including built-in) are clickable to edit or delete.
 * Editing a built-in skill creates a custom override with the same ID.
 * Deleting a built-in skill hides it (restorable).
 */

import { Plus, RotateCcw } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { CustomSkill } from '../../../stores/settings/settings-types'
import { RulesSection } from './rules-section'
import { SkillCard, SkillForm } from './skills-tab-card'
import { BUILT_IN_SKILLS } from './skills-tab-data'

export const SkillsTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const enabledSet = createMemo(() => new Set(settings().enabledSkills))
  const hiddenSet = createMemo(() => new Set(settings().hiddenBuiltInSkills ?? []))
  const customMap = createMemo(() => new Map((settings().customSkills ?? []).map((s) => [s.id, s])))
  const activeCount = createMemo(() => enabledSet().size)

  const [showForm, setShowForm] = createSignal(false)
  const [editingSkill, setEditingSkill] = createSignal<CustomSkill | null>(null)
  const [formTitle, setFormTitle] = createSignal('New Custom Skill')

  // Built-in skills not hidden and not overridden by custom
  const visibleBuiltIn = createMemo(() =>
    BUILT_IN_SKILLS.filter((s) => !hiddenSet().has(s.id) && !customMap().has(s.id))
  )

  // Custom overrides of built-in skills (shown with "Modified" badge)
  const overrides = createMemo(() =>
    (settings().customSkills ?? []).filter((s) => BUILT_IN_SKILLS.some((b) => b.id === s.id))
  )

  // Pure custom skills (not overrides of built-ins)
  const pureCustom = createMemo(() =>
    (settings().customSkills ?? []).filter((s) => !BUILT_IN_SKILLS.some((b) => b.id === s.id))
  )

  const toggleSkill = (id: string) => {
    const current = settings().enabledSkills
    const next = current.includes(id) ? current.filter((s) => s !== id) : [...current, id]
    updateSettings({ enabledSkills: next })
  }

  const openEditForm = (skill: CustomSkill, title: string) => {
    setEditingSkill(skill)
    setFormTitle(title)
    setShowForm(true)
  }

  const handleEditBuiltIn = (skill: (typeof BUILT_IN_SKILLS)[number]) => {
    const override = customMap().get(skill.id)
    const data: CustomSkill = override ?? {
      id: skill.id,
      name: skill.name,
      description: skill.description,
      fileGlobs: [...skill.fileGlobs],
      instructions: skill.instructions,
    }
    openEditForm(data, `Edit: ${skill.name}`)
  }

  const handleEditCustom = (skill: CustomSkill) => {
    openEditForm(skill, `Edit: ${skill.name}`)
  }

  const handleCreateNew = () => {
    setEditingSkill(null)
    setFormTitle('New Custom Skill')
    setShowForm(true)
  }

  const saveSkill = (skill: CustomSkill) => {
    const existing = settings().customSkills ?? []
    const idx = existing.findIndex((s) => s.id === skill.id)
    const updated =
      idx >= 0 ? existing.map((s) => (s.id === skill.id ? skill : s)) : [...existing, skill]
    updateSettings({ customSkills: updated })
    // Auto-enable new skills
    if (idx < 0 && !settings().enabledSkills.includes(skill.id)) {
      updateSettings({ enabledSkills: [...settings().enabledSkills, skill.id] })
    }
    setShowForm(false)
    setEditingSkill(null)
  }

  const deleteCustomSkill = (id: string) => {
    updateSettings({
      customSkills: (settings().customSkills ?? []).filter((s) => s.id !== id),
      enabledSkills: settings().enabledSkills.filter((s) => s !== id),
    })
  }

  const hideBuiltInSkill = (id: string) => {
    const hidden = settings().hiddenBuiltInSkills ?? []
    if (!hidden.includes(id)) {
      updateSettings({
        hiddenBuiltInSkills: [...hidden, id],
        enabledSkills: settings().enabledSkills.filter((s) => s !== id),
      })
    }
  }

  const restoreAllBuiltIn = () => updateSettings({ hiddenBuiltInSkills: [] })

  return (
    <div class="space-y-4">
      {/* Rules section (above skills) */}
      <RulesSection />

      {/* Divider */}
      <div class="border-t border-[var(--border-subtle)]" />

      {/* Skills Header */}
      <div class="flex items-center justify-between">
        <div>
          <h3 class="text-sm font-semibold text-[var(--text-primary)]">Skills</h3>
          <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
            Domain-specific prompt modules that activate based on file types. Click any skill to
            edit.
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
            Create Skill
          </button>
        </div>
      </div>

      {/* Inline edit form */}
      <Show when={showForm()}>
        <SkillForm
          initial={editingSkill() ?? undefined}
          title={formTitle()}
          onSave={saveSkill}
          onCancel={() => {
            setShowForm(false)
            setEditingSkill(null)
          }}
        />
      </Show>

      {/* Custom skills */}
      <Show
        when={pureCustom().length > 0}
        fallback={
          <div class="flex flex-col items-center justify-center py-6 text-center border border-dashed border-[var(--border-subtle)] rounded-[var(--radius-lg)]">
            <p class="text-xs text-[var(--text-secondary)] mb-0.5">No custom skills</p>
            <p class="text-[10px] text-[var(--text-muted)] max-w-xs mb-2">
              Skills inject domain-specific guidance into the agent when matching files are detected
            </p>
            <button
              type="button"
              onClick={handleCreateNew}
              class="inline-flex items-center gap-1.5 px-3 py-1.5 text-[11px] font-medium rounded-[var(--radius-md)] bg-[var(--accent)] text-white hover:brightness-110 transition-colors"
            >
              Create Skill
            </button>
          </div>
        }
      >
        <div class="space-y-1.5">
          <h4 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
            Custom Skills
          </h4>
          <For each={pureCustom()}>
            {(skill) => (
              <SkillCard
                name={skill.name}
                description={skill.description}
                fileGlobs={skill.fileGlobs}
                isEnabled={enabledSet().has(skill.id)}
                isCustom
                onToggle={() => toggleSkill(skill.id)}
                onClick={() => handleEditCustom(skill)}
                onDelete={() => deleteCustomSkill(skill.id)}
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
        <For each={overrides()}>
          {(skill) => (
            <SkillCard
              name={skill.name}
              description={skill.description}
              fileGlobs={skill.fileGlobs}
              isEnabled={enabledSet().has(skill.id)}
              isOverride
              onToggle={() => toggleSkill(skill.id)}
              onClick={() => handleEditCustom(skill)}
              onDelete={() => deleteCustomSkill(skill.id)}
            />
          )}
        </For>
        <For each={visibleBuiltIn()}>
          {(skill) => (
            <SkillCard
              name={skill.name}
              description={skill.description}
              fileGlobs={skill.fileGlobs}
              isEnabled={enabledSet().has(skill.id)}
              onToggle={() => toggleSkill(skill.id)}
              onClick={() => handleEditBuiltIn(skill)}
              onDelete={() => hideBuiltInSkill(skill.id)}
            />
          )}
        </For>
      </div>

      {/* Restore hidden built-in skills */}
      <Show when={hiddenSet().size > 0}>
        <button
          type="button"
          onClick={restoreAllBuiltIn}
          class="inline-flex items-center gap-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
        >
          <RotateCcw class="w-3 h-3" />
          Restore {hiddenSet().size} hidden built-in skill{hiddenSet().size > 1 ? 's' : ''}
        </button>
      </Show>

      <p class="text-[10px] text-[var(--text-muted)]">
        Enabled skills inject domain-specific guidance into the system prompt when matching files
        are detected in your project.
      </p>
    </div>
  )
}
