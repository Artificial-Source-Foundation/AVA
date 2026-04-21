/**
 * Skills Settings Tab — Tools section
 *
 * Dedicated tab for managing context-aware instruction modules (skills).
 * Skills are markdown files with instructions that get injected into the
 * agent's system prompt based on file glob patterns.
 *
 * Sections:
 * 1. Active Skills — toggleable list of built-in and custom skills
 * 2. Create Skill — inline form for new custom skills
 * 3. Skill Sources — shows directories where skills are loaded from
 */

import { FolderOpen, Plus, Sparkles } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import type { CustomSkill } from '../../../stores/settings/settings-types'
import { SETTINGS_CARD_GAP } from '../settings-constants'
import { RulesAndCommandsContent } from './SkillsTab'
import { SkillForm } from './skills-tab-card'
import { BUILT_IN_SKILLS } from './skills-tab-data'

/** Directories AVA discovers skill files from (mirroring crates/ava-agent/src/instructions.rs). */
const SKILL_SOURCES = [
  {
    path: '$XDG_CONFIG_HOME/ava/skills/',
    scope: 'Global',
    description: 'User-level skills, always loaded',
  },
  { path: '~/.claude/skills/', scope: 'Global', description: 'Claude-compatible global skills' },
  { path: '~/.agents/skills/', scope: 'Global', description: 'Agent-compatible global skills' },
  { path: '.ava/skills/', scope: 'Project', description: 'Project-local skills (requires trust)' },
  {
    path: '.claude/skills/',
    scope: 'Project',
    description: 'Claude-compatible project skills (requires trust)',
  },
  {
    path: '.agents/skills/',
    scope: 'Project',
    description: 'Agent-compatible project skills (requires trust)',
  },
]

// ============================================================================
// Toggle
// ============================================================================

const Toggle: Component<{ checked: boolean; onChange: () => void; ariaLabel: string }> = (
  props
) => (
  <button
    type="button"
    onClick={(e) => {
      e.stopPropagation()
      props.onChange()
    }}
    role="switch"
    aria-checked={props.checked}
    aria-label={props.ariaLabel}
    style={{
      width: '44px',
      height: '24px',
      'border-radius': '12px',
      background: props.checked ? '#0A84FF' : '#2C2C2E',
      border: 'none',
      cursor: 'pointer',
      position: 'relative',
      'flex-shrink': '0',
      transition: 'background 0.15s',
    }}
  >
    <span
      style={{
        position: 'absolute',
        width: '20px',
        height: '20px',
        'border-radius': '50%',
        background: '#FFFFFF',
        top: '2px',
        left: props.checked ? '22px' : '2px',
        transition: 'left 0.15s',
      }}
    />
  </button>
)

// ============================================================================
// Main Tab
// ============================================================================

export const SkillsSettingsTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const enabledSet = createMemo(() => new Set(settings().enabledSkills))
  const hiddenSet = createMemo(() => new Set(settings().hiddenBuiltInSkills ?? []))
  const customMap = createMemo(() => new Map((settings().customSkills ?? []).map((s) => [s.id, s])))
  const activeCount = createMemo(() => enabledSet().size)

  const [showSkillForm, setShowSkillForm] = createSignal(false)
  const [editingSkill, setEditingSkill] = createSignal<CustomSkill | null>(null)
  const [skillFormTitle, setSkillFormTitle] = createSignal('New Custom Skill')

  const visibleBuiltIn = createMemo(() =>
    BUILT_IN_SKILLS.filter((s) => !hiddenSet().has(s.id) && !customMap().has(s.id))
  )
  const overrides = createMemo(() =>
    (settings().customSkills ?? []).filter((s) => BUILT_IN_SKILLS.some((b) => b.id === s.id))
  )
  const pureCustom = createMemo(() =>
    (settings().customSkills ?? []).filter((s) => !BUILT_IN_SKILLS.some((b) => b.id === s.id))
  )
  const hiddenBuiltIns = createMemo(() =>
    BUILT_IN_SKILLS.filter((skill) => hiddenSet().has(skill.id) && !customMap().has(skill.id))
  )

  const allSkills = createMemo(() => [...overrides(), ...visibleBuiltIn(), ...pureCustom()])

  const toggleSkill = (id: string) => {
    const current = settings().enabledSkills
    const next = current.includes(id) ? current.filter((s) => s !== id) : [...current, id]
    updateSettings({ enabledSkills: next })
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
    setEditingSkill(data)
    setSkillFormTitle(`Edit: ${skill.name}`)
    setShowSkillForm(true)
  }

  const handleEditCustom = (skill: CustomSkill) => {
    setEditingSkill(skill)
    setSkillFormTitle(`Edit: ${skill.name}`)
    setShowSkillForm(true)
  }

  const handleCreateSkill = () => {
    setEditingSkill(null)
    setSkillFormTitle('New Custom Skill')
    setShowSkillForm(true)
  }

  const saveSkill = (skill: CustomSkill) => {
    const existing = settings().customSkills ?? []
    const idx = existing.findIndex((s) => s.id === skill.id)
    const updated =
      idx >= 0 ? existing.map((s) => (s.id === skill.id ? skill : s)) : [...existing, skill]
    updateSettings({ customSkills: updated })
    if (idx < 0 && !settings().enabledSkills.includes(skill.id)) {
      updateSettings({ enabledSkills: [...settings().enabledSkills, skill.id] })
    }
    setShowSkillForm(false)
    setEditingSkill(null)
  }

  const deleteSkill = (id: string) => {
    const existing = settings().customSkills ?? []
    updateSettings({ customSkills: existing.filter((s) => s.id !== id) })
    updateSettings({ enabledSkills: settings().enabledSkills.filter((s) => s !== id) })
  }

  const restoreHiddenBuiltIn = (id: string) => {
    updateSettings({
      hiddenBuiltInSkills: (settings().hiddenBuiltInSkills ?? []).filter(
        (hiddenId) => hiddenId !== id
      ),
    })
  }

  const restoreAllHiddenBuiltIns = () => {
    const restoreIds = new Set(hiddenBuiltIns().map((skill) => skill.id))
    updateSettings({
      hiddenBuiltInSkills: (settings().hiddenBuiltInSkills ?? []).filter(
        (hiddenId) => !restoreIds.has(hiddenId)
      ),
    })
  }

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: SETTINGS_CARD_GAP }}>
      {/* Page header */}
      <div>
        <h2
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '22px',
            'font-weight': '600',
            color: '#F5F5F7',
            margin: '0',
          }}
        >
          Skills
        </h2>
        <p
          style={{
            'font-family': 'Geist, sans-serif',
            'font-size': '12px',
            color: '#48484A',
            'margin-top': '4px',
          }}
        >
          Custom instructions that activate based on context
        </p>
      </div>

      {/* ===== Active Skills Card ===== */}
      <div
        style={{
          background: '#111114',
          border: '1px solid #ffffff08',
          'border-radius': '12px',
          padding: '20px',
          display: 'flex',
          'flex-direction': 'column',
          gap: '16px',
        }}
      >
        {/* Header */}
        <div
          style={{
            display: 'flex',
            'align-items': 'center',
            'justify-content': 'space-between',
          }}
        >
          <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
            <Sparkles size={16} style={{ color: '#C8C8CC' }} />
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '14px',
                  'font-weight': '500',
                  color: '#F5F5F7',
                }}
              >
                Active Skills
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                }}
              >
                Domain-specific prompt modules that activate based on file types
              </span>
            </div>
          </div>
          <div style={{ display: 'flex', 'align-items': 'center', gap: '8px' }}>
            <span
              style={{
                padding: '2px 8px',
                'border-radius': '12px',
                background: '#0A84FF18',
                'font-family': 'Geist, sans-serif',
                'font-size': '10px',
                'font-weight': '500',
                color: '#0A84FF',
              }}
            >
              {activeCount()} active
            </span>
            <button
              type="button"
              onClick={handleCreateSkill}
              style={{
                display: 'flex',
                'align-items': 'center',
                gap: '6px',
                padding: '6px 12px',
                background: '#0A84FF',
                'border-radius': '8px',
                border: 'none',
                cursor: 'pointer',
              }}
            >
              <Plus size={12} style={{ color: '#FFFFFF' }} />
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  'font-weight': '500',
                  color: '#FFFFFF',
                }}
              >
                Create Skill
              </span>
            </button>
          </div>
        </div>

        {/* Inline skill form */}
        <Show when={showSkillForm()}>
          <SkillForm
            initial={editingSkill() ?? undefined}
            title={skillFormTitle()}
            onSave={saveSkill}
            onCancel={() => {
              setShowSkillForm(false)
              setEditingSkill(null)
            }}
          />
        </Show>

        <Show when={hiddenBuiltIns().length > 0}>
          <div
            style={{
              padding: '12px',
              'border-radius': '8px',
              background: '#0A84FF08',
              border: '1px solid #0A84FF15',
              display: 'flex',
              'flex-direction': 'column',
              gap: '10px',
            }}
          >
            <div
              style={{
                display: 'flex',
                'align-items': 'center',
                'justify-content': 'space-between',
                gap: '12px',
              }}
            >
              <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
                <span
                  style={{
                    'font-family': 'Geist, sans-serif',
                    'font-size': '12px',
                    'font-weight': '500',
                    color: '#F5F5F7',
                  }}
                >
                  Hidden built-in skills
                </span>
                <span
                  style={{
                    'font-family': 'Geist, sans-serif',
                    'font-size': '11px',
                    color: '#636366',
                  }}
                >
                  Restore any built-ins that were migrated into a hidden state.
                </span>
              </div>
              <button
                type="button"
                onClick={restoreAllHiddenBuiltIns}
                style={{
                  background: 'none',
                  border: 'none',
                  cursor: 'pointer',
                  color: '#0A84FF',
                  'font-family': 'Geist, sans-serif',
                  'font-size': '11px',
                  'font-weight': '500',
                  padding: '0',
                  'flex-shrink': '0',
                }}
              >
                Restore all
              </button>
            </div>

            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '6px' }}>
              <For each={hiddenBuiltIns()}>
                {(skill) => (
                  <div
                    style={{
                      display: 'flex',
                      'align-items': 'center',
                      'justify-content': 'space-between',
                      gap: '12px',
                    }}
                  >
                    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
                      <span
                        style={{
                          'font-family': 'Geist, sans-serif',
                          'font-size': '12px',
                          color: '#F5F5F7',
                        }}
                      >
                        {skill.name}
                      </span>
                      <span
                        style={{
                          'font-family': 'Geist, sans-serif',
                          'font-size': '11px',
                          color: '#636366',
                        }}
                      >
                        {skill.description}
                      </span>
                    </div>
                    <button
                      type="button"
                      onClick={() => restoreHiddenBuiltIn(skill.id)}
                      style={{
                        background: 'none',
                        border: 'none',
                        cursor: 'pointer',
                        color: '#0A84FF',
                        'font-family': 'Geist, sans-serif',
                        'font-size': '11px',
                        'font-weight': '500',
                        padding: '0',
                        'flex-shrink': '0',
                      }}
                    >
                      Restore
                    </button>
                  </div>
                )}
              </For>
            </div>
          </div>
        </Show>

        {/* Skill rows */}
        <Show
          when={allSkills().length > 0}
          fallback={
            <div
              style={{
                display: 'flex',
                'flex-direction': 'column',
                'align-items': 'center',
                'justify-content': 'center',
                padding: '32px 0',
                'text-align': 'center',
              }}
            >
              <Sparkles size={24} style={{ color: '#48484A', 'margin-bottom': '8px' }} />
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                }}
              >
                No skills configured yet
              </span>
            </div>
          }
        >
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
            <For each={allSkills()}>
              {(skill) => {
                const isBuiltIn = () => BUILT_IN_SKILLS.some((b) => b.id === skill.id)
                const isOverride = () => isBuiltIn() && customMap().has(skill.id)
                const isEnabled = () => enabledSet().has(skill.id)
                const builtInData = () => BUILT_IN_SKILLS.find((b) => b.id === skill.id)
                const skillName = () => ('name' in skill ? skill.name : (builtInData()?.name ?? ''))
                const skillDesc = () =>
                  'description' in skill ? skill.description : (builtInData()?.description ?? '')
                const skillGlobs = () =>
                  'fileGlobs' in skill ? skill.fileGlobs : (builtInData()?.fileGlobs ?? [])

                return (
                  // biome-ignore lint/a11y/useKeyWithClickEvents: skill row
                  // biome-ignore lint/a11y/useSemanticElements: card-style row
                  <div
                    role="button"
                    tabIndex={0}
                    onClick={() =>
                      isBuiltIn() && !customMap().has(skill.id)
                        ? handleEditBuiltIn(builtInData()!)
                        : handleEditCustom(skill as CustomSkill)
                    }
                    style={{
                      display: 'flex',
                      'align-items': 'center',
                      'justify-content': 'space-between',
                      padding: '10px 12px',
                      'border-radius': '8px',
                      cursor: 'pointer',
                      transition: 'background 0.15s',
                      background: isEnabled() ? '#ffffff04' : 'transparent',
                    }}
                    onMouseEnter={(e) => {
                      e.currentTarget.style.background = '#ffffff08'
                    }}
                    onMouseLeave={(e) => {
                      e.currentTarget.style.background = isEnabled() ? '#ffffff04' : 'transparent'
                    }}
                  >
                    <div
                      style={{
                        display: 'flex',
                        'flex-direction': 'column',
                        gap: '3px',
                        'min-width': '0',
                        flex: '1',
                      }}
                    >
                      <div
                        style={{
                          display: 'flex',
                          'align-items': 'center',
                          gap: '8px',
                        }}
                      >
                        <span
                          style={{
                            'font-family': 'Geist, sans-serif',
                            'font-size': '13px',
                            color: '#F5F5F7',
                          }}
                        >
                          {skillName()}
                        </span>
                        <Show when={isBuiltIn() && !isOverride()}>
                          <span
                            style={{
                              padding: '1px 6px',
                              'border-radius': '4px',
                              background: '#ffffff08',
                              'font-family': 'Geist, sans-serif',
                              'font-size': '10px',
                              'font-weight': '500',
                              color: '#636366',
                            }}
                          >
                            Built-in
                          </span>
                        </Show>
                        <Show when={isOverride()}>
                          <span
                            style={{
                              padding: '1px 6px',
                              'border-radius': '4px',
                              background: '#0A84FF18',
                              'font-family': 'Geist, sans-serif',
                              'font-size': '10px',
                              'font-weight': '500',
                              color: '#0A84FF',
                            }}
                          >
                            Modified
                          </span>
                        </Show>
                        <Show when={!isBuiltIn()}>
                          <span
                            style={{
                              padding: '1px 6px',
                              'border-radius': '4px',
                              background: '#30D15818',
                              'font-family': 'Geist, sans-serif',
                              'font-size': '10px',
                              'font-weight': '500',
                              color: '#30D158',
                            }}
                          >
                            Custom
                          </span>
                        </Show>
                      </div>
                      <div
                        style={{
                          display: 'flex',
                          'align-items': 'center',
                          gap: '6px',
                        }}
                      >
                        <span
                          style={{
                            'font-family': 'Geist Mono, monospace',
                            'font-size': '11px',
                            color: '#48484A',
                          }}
                        >
                          {skillGlobs().join(', ')}
                        </span>
                      </div>
                      <Show when={skillDesc()}>
                        <span
                          style={{
                            'font-family': 'Geist, sans-serif',
                            'font-size': '11px',
                            color: '#636366',
                          }}
                        >
                          {skillDesc()}
                        </span>
                      </Show>
                    </div>
                    <div style={{ display: 'flex', 'align-items': 'center', gap: '8px' }}>
                      <Show when={!isBuiltIn()}>
                        <button
                          type="button"
                          onClick={(e) => {
                            e.stopPropagation()
                            deleteSkill(skill.id)
                          }}
                          style={{
                            background: 'none',
                            border: 'none',
                            cursor: 'pointer',
                            padding: '4px',
                            'border-radius': '4px',
                            color: '#48484A',
                            'font-family': 'Geist, sans-serif',
                            'font-size': '11px',
                            transition: 'color 0.15s',
                          }}
                          onMouseEnter={(e) => {
                            e.currentTarget.style.color = '#FF453A'
                          }}
                          onMouseLeave={(e) => {
                            e.currentTarget.style.color = '#48484A'
                          }}
                        >
                          Delete
                        </button>
                      </Show>
                      <Toggle
                        checked={isEnabled()}
                        onChange={() => toggleSkill(skill.id)}
                        ariaLabel={`Enable ${skillName()} skill`}
                      />
                    </div>
                  </div>
                )
              }}
            </For>
          </div>
        </Show>
      </div>

      {/* ===== Skill Sources Card ===== */}
      <div
        style={{
          background: '#111114',
          border: '1px solid #ffffff08',
          'border-radius': '12px',
          padding: '20px',
          display: 'flex',
          'flex-direction': 'column',
          gap: '16px',
        }}
      >
        {/* Header */}
        <div style={{ display: 'flex', 'align-items': 'center', gap: '10px' }}>
          <FolderOpen size={16} style={{ color: '#C8C8CC' }} />
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '14px',
                'font-weight': '500',
                color: '#F5F5F7',
              }}
            >
              Skill Sources
            </span>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
              }}
            >
              Directories where AVA discovers SKILL.md files
            </span>
          </div>
        </div>

        {/* Source rows */}
        <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
          <For each={SKILL_SOURCES}>
            {(source) => (
              <div
                style={{
                  display: 'flex',
                  'align-items': 'center',
                  'justify-content': 'space-between',
                  padding: '8px 12px',
                  'border-radius': '8px',
                  background: '#ffffff04',
                }}
              >
                <div
                  style={{
                    display: 'flex',
                    'flex-direction': 'column',
                    gap: '2px',
                    'min-width': '0',
                    flex: '1',
                  }}
                >
                  <span
                    style={{
                      'font-family': 'Geist Mono, monospace',
                      'font-size': '12px',
                      color: '#C8C8CC',
                    }}
                  >
                    {source.path}
                  </span>
                  <span
                    style={{
                      'font-family': 'Geist, sans-serif',
                      'font-size': '11px',
                      color: '#48484A',
                    }}
                  >
                    {source.description}
                  </span>
                </div>
                <span
                  style={{
                    padding: '2px 8px',
                    'border-radius': '4px',
                    background: source.scope === 'Global' ? '#30D15810' : '#FF9F0A10',
                    'font-family': 'Geist, sans-serif',
                    'font-size': '10px',
                    'font-weight': '500',
                    color: source.scope === 'Global' ? '#30D158' : '#FF9F0A',
                    'flex-shrink': '0',
                  }}
                >
                  {source.scope}
                </span>
              </div>
            )}
          </For>
        </div>

        {/* Hint */}
        <div
          style={{
            padding: '10px 12px',
            'border-radius': '8px',
            background: '#0A84FF08',
            border: '1px solid #0A84FF15',
          }}
        >
          <p
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '11px',
              color: '#636366',
              margin: '0',
              'line-height': '1.5',
            }}
          >
            Each skill directory should contain subdirectories with a{' '}
            <span
              style={{
                'font-family': 'Geist Mono, monospace',
                color: '#0A84FF',
                'font-size': '11px',
              }}
            >
              SKILL.md
            </span>{' '}
            file. Project-local skills require the project to be trusted (
            <span
              style={{
                'font-family': 'Geist Mono, monospace',
                color: '#0A84FF',
                'font-size': '11px',
              }}
            >
              ava --trust
            </span>
            ).
          </p>
        </div>
      </div>

      <RulesAndCommandsContent />
    </div>
  )
}
