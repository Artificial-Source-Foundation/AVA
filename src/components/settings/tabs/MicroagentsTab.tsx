/**
 * Microagents Tab — Manage domain-specific prompt modules (skills)
 *
 * Lists built-in skills with file glob triggers, toggle enable/disable,
 * and shows active skill badges for the current session.
 */

import { Brain } from 'lucide-solid'
import { type Component, createMemo, For } from 'solid-js'
import { useSettings } from '../../../stores/settings'

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

export const MicroagentsTab: Component = () => {
  const { settings, updateSettings } = useSettings()

  const enabledSet = createMemo(() => new Set(settings().enabledMicroagents))

  const activeCount = createMemo(() => enabledSet().size)

  const toggleSkill = (id: string) => {
    const current = settings().enabledMicroagents
    const next = current.includes(id) ? current.filter((s) => s !== id) : [...current, id]
    updateSettings({ enabledMicroagents: next })
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
        <span class="px-2 py-0.5 text-[10px] rounded-full bg-[var(--accent-subtle)] text-[var(--accent)] border border-[var(--accent-muted)]">
          {activeCount()} active
        </span>
      </div>

      <div class="space-y-1.5">
        <For each={BUILT_IN_SKILLS}>
          {(skill) => {
            const isEnabled = () => enabledSet().has(skill.id)
            return (
              <div
                class={`flex items-start gap-3 px-3 py-2.5 rounded-[var(--radius-md)] border transition-colors ${
                  isEnabled()
                    ? 'border-[var(--accent-muted)] bg-[var(--accent-subtle)]'
                    : 'border-[var(--border-subtle)] bg-[var(--surface)]'
                }`}
              >
                <Brain
                  class={`w-4 h-4 mt-0.5 flex-shrink-0 ${
                    isEnabled() ? 'text-[var(--accent)]' : 'text-[var(--text-muted)]'
                  }`}
                />
                <div class="flex-1 min-w-0">
                  <div class="flex items-center gap-2">
                    <span class="text-xs font-medium text-[var(--text-primary)]">{skill.name}</span>
                    <For each={skill.fileGlobs}>
                      {(glob) => (
                        <span class="px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-muted)] font-mono">
                          {glob}
                        </span>
                      )}
                    </For>
                  </div>
                  <p class="text-[10px] text-[var(--text-muted)] mt-0.5">{skill.description}</p>
                </div>
                <button
                  type="button"
                  onClick={() => toggleSkill(skill.id)}
                  class={`relative w-8 h-[18px] rounded-full transition-colors flex-shrink-0 mt-0.5 ${
                    isEnabled() ? 'bg-[var(--accent)]' : 'bg-[var(--alpha-white-10)]'
                  }`}
                  aria-label={`${isEnabled() ? 'Disable' : 'Enable'} ${skill.name}`}
                >
                  <span
                    class={`absolute top-[2px] w-[14px] h-[14px] rounded-full bg-white shadow-sm transition-transform ${
                      isEnabled() ? 'translate-x-[16px]' : 'translate-x-[2px]'
                    }`}
                  />
                </button>
              </div>
            )
          }}
        </For>
      </div>

      <p class="text-[10px] text-[var(--text-muted)]">
        Enabled skills inject domain-specific guidance into the system prompt when matching files
        are detected in your project.
      </p>
    </div>
  )
}
