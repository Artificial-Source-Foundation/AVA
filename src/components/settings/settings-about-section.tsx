import { ExternalLink, RefreshCw } from 'lucide-solid'
import { type Component, createResource, For } from 'solid-js'
import { apiInvoke } from '../../lib/api-client'

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
    return 'unknown'
  }
}

export const AboutSection: Component = () => {
  const [version, { refetch }] = createResource(fetchVersion)

  const info: [string, string][] = [
    ['Runtime', 'Tauri v2 + SolidJS'],
    ['Backend', 'Rust (22 crates)'],
    ['License', 'MIT'],
    ['Platform', 'Linux / macOS / Windows'],
  ]

  return (
    <div class="space-y-4">
      <div>
        <h3 class="text-sm font-semibold text-[var(--text-primary)]">AVA</h3>
        <p class="text-xs text-[var(--text-muted)] mt-1">
          Solo-first coding agent with a plugin-first architecture.
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
    </div>
  )
}
