import { ExternalLink, RefreshCw } from 'lucide-solid'
import { type Component, createResource, For, Show } from 'solid-js'
import { apiInvoke } from '../../lib/api-client'
import { useSettings } from '../../stores/settings'

interface HealthResponse {
  cwd: string
  status: string
  version: string
}

async function fetchVersion(): Promise<string> {
  try {
    const health = await apiInvoke<HealthResponse>('health')
    return health.version
  } catch {
    return '2.1.0'
  }
}

export const AboutSection: Component = () => {
  const { settings, updateSettings } = useSettings()
  const [version, { refetch }] = createResource(fetchVersion)

  const info: [string, string][] = [
    ['Runtime', 'Tauri v2 + SolidJS'],
    ['Backend', 'Rust (21 crates)'],
    ['License', 'MIT'],
    ['Platform', 'Linux / macOS / Windows'],
  ]

  return (
    <div class="space-y-4">
      <div>
        <h3 class="text-sm font-semibold text-[var(--text-primary)]">AVA</h3>
        <p class="text-xs text-[var(--text-muted)] mt-1">
          AI dev team — lean by default, infinitely extensible
        </p>
        <div class="flex items-center gap-2 mt-2">
          <span class="inline-block px-2 py-0.5 text-[var(--settings-text-badge)] font-mono text-[var(--accent)] bg-[var(--accent-subtle)] rounded-[var(--radius-sm)]">
            v{version() ?? '...'}
          </span>
          <button
            type="button"
            onClick={() => refetch()}
            class="p-1 text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors rounded"
            title="Refresh version"
          >
            <RefreshCw class="w-3 h-3" />
          </button>
        </div>
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
        href="https://github.com/ASF-GROUP/AVA"
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
