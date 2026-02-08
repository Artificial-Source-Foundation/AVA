/**
 * Sidebar Plugins View
 *
 * Placeholder panel for the plugin ecosystem (Phase 2).
 * Shows built-in skills and disabled browse/create buttons.
 */

import { ExternalLink, Package, Plus, Puzzle, Search } from 'lucide-solid'
import { type Component, For } from 'solid-js'

// Built-in skills that ship with Estela
const builtInSkills = [
  {
    name: 'Code Navigation',
    description: 'Jump to definitions, references, and symbols',
    active: true,
  },
  { name: 'Git Integration', description: 'Stage, commit, and diff from chat', active: true },
  { name: 'Test Runner', description: 'Run and debug tests inline', active: true },
  { name: 'Web Search', description: 'Search the web for documentation', active: true },
  { name: 'Browser Automation', description: 'Puppeteer-based web interaction', active: false },
]

export const SidebarPlugins: Component = () => {
  return (
    <div class="flex flex-col h-full">
      {/* Header */}
      <div class="flex items-center justify-between px-3 h-10 flex-shrink-0 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-semibold text-[var(--text-secondary)] uppercase tracking-wider">
          Plugins
        </span>
        <div class="flex items-center gap-1">
          <Puzzle class="w-3.5 h-3.5 text-[var(--text-muted)]" />
        </div>
      </div>

      {/* Coming Soon Banner */}
      <div class="px-3 py-3 border-b border-[var(--border-subtle)]">
        <div class="px-3 py-2 rounded-[var(--radius-lg)] bg-[var(--accent-subtle)] border border-[var(--accent-border)]">
          <p class="text-[11px] font-medium text-[var(--accent)]">Plugin Ecosystem</p>
          <p class="text-[10px] text-[var(--text-muted)] mt-0.5">
            Obsidian-style plugins coming in Phase 2. Create, discover, and install extensions.
          </p>
        </div>
      </div>

      {/* Action Buttons (disabled) */}
      <div class="px-3 py-2 space-y-1.5 border-b border-[var(--border-subtle)]">
        <button
          type="button"
          disabled
          class="
            w-full flex items-center gap-2 px-3 py-2
            rounded-[var(--radius-md)]
            bg-[var(--surface-sunken)]
            border border-[var(--border-subtle)]
            text-[var(--text-muted)]
            opacity-50 cursor-not-allowed
            text-xs
          "
          title="Coming in Phase 2"
        >
          <Search class="w-3.5 h-3.5" />
          Browse Plugins
          <span class="ml-auto text-[9px] text-[var(--text-muted)]">Soon</span>
        </button>

        <button
          type="button"
          disabled
          class="
            w-full flex items-center gap-2 px-3 py-2
            rounded-[var(--radius-md)]
            bg-[var(--surface-sunken)]
            border border-[var(--border-subtle)]
            text-[var(--text-muted)]
            opacity-50 cursor-not-allowed
            text-xs
          "
          title="Coming in Phase 2"
        >
          <Plus class="w-3.5 h-3.5" />
          Create Plugin
          <span class="ml-auto text-[9px] text-[var(--text-muted)]">Soon</span>
        </button>
      </div>

      {/* Built-in Skills */}
      <div class="flex-1 overflow-y-auto scrollbar-none">
        <div class="px-3 py-2">
          <p class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
            Built-in Skills
          </p>
          <div class="space-y-1">
            <For each={builtInSkills}>
              {(skill) => (
                <div
                  class={`
                    flex items-center gap-2.5 px-2.5 py-2
                    rounded-[var(--radius-md)]
                    border border-[var(--border-subtle)]
                    ${skill.active ? 'bg-[var(--surface)]' : 'bg-[var(--surface-sunken)] opacity-60'}
                  `}
                >
                  <Package class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]" />
                  <div class="flex-1 min-w-0">
                    <p class="text-[11px] text-[var(--text-primary)] truncate">{skill.name}</p>
                    <p class="text-[9px] text-[var(--text-muted)] truncate">{skill.description}</p>
                  </div>
                  <span
                    class={`
                      w-1.5 h-1.5 rounded-full flex-shrink-0
                      ${skill.active ? 'bg-[var(--success)]' : 'bg-[var(--gray-6)]'}
                    `}
                  />
                </div>
              )}
            </For>
          </div>
        </div>
      </div>

      {/* Footer */}
      <div class="px-3 py-2 border-t border-[var(--border-subtle)]">
        <a
          href="https://github.com/estela-ai/estela"
          target="_blank"
          rel="noopener noreferrer"
          class="
            flex items-center gap-1.5
            text-[10px] text-[var(--text-muted)]
            hover:text-[var(--accent)]
            transition-colors
          "
        >
          <ExternalLink class="w-3 h-3" />
          Plugin Development Guide
        </a>
      </div>
    </div>
  )
}
