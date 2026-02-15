import { ExternalLink } from 'lucide-solid'
import { type Component, For } from 'solid-js'

export const AboutSection: Component = () => {
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
        <span class="inline-block mt-2 px-2 py-0.5 text-[10px] font-mono text-[var(--accent)] bg-[var(--accent-subtle)] rounded-[var(--radius-sm)]">
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
    </div>
  )
}
