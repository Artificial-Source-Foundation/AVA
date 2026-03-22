import { ExternalLink } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import { useSettings } from '../../stores/settings'

export const AboutSection: Component = () => {
  const { settings, updateSettings } = useSettings()

  const info: [string, string][] = [
    ['Runtime', 'Tauri v2 + SolidJS'],
    ['Language', 'TypeScript (strict)'],
    ['License', 'MIT'],
    ['Platform', 'Linux / macOS / Windows'],
  ]

  return (
    <div class="space-y-4">
      <div>
        <h3 class="text-sm font-semibold text-[var(--text-primary)]">AVA</h3>
        <p class="text-xs text-[var(--text-muted)] mt-1">
          Desktop AI coding app with a virtual dev team and community plugins.
        </p>
        <span class="inline-block mt-2 px-2 py-0.5 text-[var(--settings-text-badge)] font-mono text-[var(--accent)] bg-[var(--accent-subtle)] rounded-[var(--radius-sm)]">
          v0.1.0-alpha
        </span>
      </div>

      <div class="space-y-0.5">
        <For each={info}>
          {([label, value]) => (
            <div class="flex items-center justify-between py-1.5">
              <span class="text-xs text-[var(--text-muted)]">{label}</span>
              <span class="text-xs text-[var(--text-primary)] font-mono">{value}</span>
            </div>
          )}
        </For>
      </div>

      <a
        href="https://github.com/ava-ai/ava"
        target="_blank"
        rel="noopener noreferrer"
        class="inline-flex items-center gap-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
      >
        Source code <ExternalLink class="w-3 h-3" />
      </a>

      {/* Developer Mode toggle */}
      <div class="border-t border-[var(--border-subtle)] pt-4">
        <div class="flex items-center justify-between">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">Developer Mode</span>
            <p class="text-[var(--settings-text-badge)] text-[var(--text-muted)]">
              <Show
                when={settings().devMode}
                fallback="Enable to show the Developer tab with console logs and debug tools."
              >
                Developer tab is visible in the sidebar footer.
              </Show>
            </p>
          </div>
          <button
            type="button"
            onClick={() => updateSettings({ devMode: !(settings().devMode ?? false) })}
            class="relative inline-flex h-5 w-9 items-center rounded-full transition-colors"
            style={{
              background: settings().devMode ? 'var(--accent)' : 'var(--gray-5)',
            }}
          >
            <span
              class="inline-block h-3.5 w-3.5 rounded-full bg-white transition-transform"
              style={{
                transform: settings().devMode ? 'translateX(18px)' : 'translateX(3px)',
              }}
            />
          </button>
        </div>
      </div>
    </div>
  )
}
