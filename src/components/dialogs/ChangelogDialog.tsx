/**
 * Changelog Dialog
 * Shows release notes / announcements on first launch after update.
 * Auto-shows when localStorage 'ava-last-seen-version' differs from APP_VERSION.
 */

import { Megaphone } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'

export const APP_VERSION = '2.2.6'

interface ChangelogEntry {
  version: string
  date: string
  items: string[]
}

const CHANGELOG: ChangelogEntry[] = [
  {
    version: '2.2.6',
    date: '2026-03-22',
    items: [
      '21 LLM providers: + Azure OpenAI, AWS Bedrock, xAI, Mistral, Groq, DeepSeek',
      'Mid-stream messaging: Enter = queue, Ctrl+Enter = interrupt, Alt+Enter = post-complete',
      'Message queue UI with reorder, edit, and remove',
      'Shadow git snapshots for file undo/rollback',
      'Crash recovery: per-turn checkpoints, file backups, interrupted tool cleanup',
      'Context overflow auto-compact with retry (12 provider patterns)',
      'Conversation repair before LLM calls',
      '100+ security patterns: credential theft, reverse shells, crypto mining, exfiltration',
      'Collapsible agent activity cards with live stats',
      'Right panel redesign: icon tabs, activity toggle, todos auto-open',
      'Double-Escape abort, send button context menu',
      'Incremental message persistence (crash-safe)',
      'Retry-after header parsing, quota error classification',
    ],
  },
  {
    version: '2.1.0',
    date: '2026-03-08',
    items: [
      '9 default tools: read, write, edit, bash, glob, grep, web_fetch, web_search, git_read',
      '21 Rust crates powering the full agent, CLI, and desktop backend',
      'Thinking + tool interleaving for reasoning-capable models',
      'Multi-agent orchestration via Praxis (Director → Leads → Workers)',
      'MCP protocol support with hot-reload and per-project trust',
      'Persistent memory, session history, and codebase indexing (BM25 + PageRank)',
      '29 built-in themes with live preview and custom TOML theme support',
    ],
  },
]

interface ChangelogDialogProps {
  open: boolean
  onClose: () => void
}

export const ChangelogDialog: Component<ChangelogDialogProps> = (props) => (
  <Show when={props.open}>
    <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
      <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4 max-h-[80vh] overflow-y-auto">
        <div class="flex items-center gap-2">
          <Megaphone class="w-4 h-4 text-[var(--accent)]" />
          <h3 class="text-sm font-semibold text-[var(--text-primary)]">What's New</h3>
          <span class="ml-auto text-[10px] text-[var(--text-muted)] font-mono">v{APP_VERSION}</span>
        </div>

        <For each={CHANGELOG}>
          {(entry) => (
            <div class="space-y-2">
              <div class="flex items-baseline gap-2">
                <span class="text-xs font-medium text-[var(--accent)]">v{entry.version}</span>
                <span class="text-[10px] text-[var(--text-muted)]">{entry.date}</span>
              </div>
              <ul class="space-y-1">
                <For each={entry.items}>
                  {(item) => (
                    <li class="flex gap-2 text-xs text-[var(--text-secondary)]">
                      <span class="text-[var(--accent)] shrink-0 mt-0.5">-</span>
                      <span>{item}</span>
                    </li>
                  )}
                </For>
              </ul>
            </div>
          )}
        </For>

        <div class="flex justify-end pt-2">
          <button
            type="button"
            onClick={props.onClose}
            class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
          >
            Got it
          </button>
        </div>
      </div>
    </div>
  </Show>
)

/** Check if the changelog should auto-show (version mismatch) */
export function shouldShowChangelog(): boolean {
  try {
    const lastSeen = localStorage.getItem('ava-last-seen-version')
    return lastSeen !== APP_VERSION
  } catch {
    return false
  }
}

/** Mark the current version as seen */
export function markChangelogSeen(): void {
  try {
    localStorage.setItem('ava-last-seen-version', APP_VERSION)
  } catch {
    // Ignore storage errors
  }
}
