/**
 * Changelog Dialog
 *
 * 480px card-style modal showing release notes with color-coded tags
 * (green "New", blue "Improved", red "Fixed") + title + description,
 * separated by dividers. Auto-shows when localStorage version differs.
 */

import { Sparkles, X } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'

export const APP_VERSION = '2.2.6'

// ============================================================================
// Types
// ============================================================================

type ChangeKind = 'new' | 'improved' | 'fixed'

interface ChangelogItem {
  kind: ChangeKind
  title: string
  description: string
}

interface ChangelogEntry {
  version: string
  items: ChangelogItem[]
}

const KIND_CONFIG: Record<ChangeKind, { label: string; color: string; bg: string }> = {
  new: {
    label: 'New',
    color: 'var(--success)',
    bg: 'rgba(52, 199, 89, 0.125)',
  },
  improved: {
    label: 'Improved',
    color: 'var(--accent)',
    bg: 'rgba(10, 132, 255, 0.08)',
  },
  fixed: {
    label: 'Fixed',
    color: 'var(--error)',
    bg: 'rgba(255, 69, 58, 0.125)',
  },
}

// ============================================================================
// Changelog Data
// ============================================================================

const CHANGELOG: ChangelogEntry[] = [
  {
    version: '2.2.6',
    items: [
      {
        kind: 'new',
        title: 'macOS Luxury Design System',
        description:
          'Complete UI overhaul with new color system, typography, and component library inspired by premium macOS applications.',
      },
      {
        kind: 'improved',
        title: 'Streaming Performance',
        description:
          'Removed filter-based hover effects, converted animations to scaleX transforms, and trimmed transition-all hot paths.',
      },
      {
        kind: 'fixed',
        title: 'Chat Scroll Position',
        description:
          'Fixed contain: layout style breaking flex height calculations in the message list.',
      },
      {
        kind: 'new',
        title: 'Mid-Stream Messaging',
        description:
          'Three-tier message queue: Enter = queue, Ctrl+Enter = interrupt, Alt+Enter = post-complete.',
      },
      {
        kind: 'new',
        title: 'Shadow Git Snapshots',
        description: 'File undo/rollback via automatic shadow git snapshots before every edit.',
      },
      {
        kind: 'improved',
        title: 'Context Overflow Auto-Compact',
        description:
          'Automatic context compaction with retry for 12 different provider overflow patterns.',
      },
      {
        kind: 'fixed',
        title: 'Incremental Persistence',
        description: 'Crash-safe per-turn message persistence with retry-after header parsing.',
      },
    ],
  },
  {
    version: '2.1.0',
    items: [
      {
        kind: 'new',
        title: '21 Rust Crates',
        description:
          '9 default tools, thinking + tool interleaving, multi-agent orchestration via HQ.',
      },
      {
        kind: 'improved',
        title: 'MCP Protocol Support',
        description: 'Hot-reload and per-project trust for MCP servers.',
      },
      {
        kind: 'new',
        title: '29 Built-in Themes',
        description: 'Live preview and custom TOML theme support.',
      },
    ],
  },
]

// ============================================================================
// Component
// ============================================================================

interface ChangelogDialogProps {
  open: boolean
  onClose: () => void
}

export const ChangelogDialog: Component<ChangelogDialogProps> = (props) => (
  <Show when={props.open}>
    {/* Backdrop */}
    {/* biome-ignore lint/a11y/useKeyWithClickEvents: backdrop dismiss */}
    {/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop dismiss */}
    <div
      class="fixed inset-0 z-50 flex items-center justify-center"
      style={{ background: 'rgba(0, 0, 0, 0.6)' }}
      onClick={(e) => {
        if (e.target === e.currentTarget) props.onClose()
      }}
    >
      {/* Dialog card */}
      <div
        style={{
          width: '480px',
          'max-width': 'calc(100% - 32px)',
          'max-height': '80vh',
          'border-radius': '12px',
          background: 'var(--surface)',
          border: '1px solid var(--border-default)',
          'box-shadow': '0 12px 24px rgba(0, 0, 0, 0.4)',
          overflow: 'hidden',
          display: 'flex',
          'flex-direction': 'column',
        }}
      >
        {/* Header */}
        <div
          class="flex items-center justify-between"
          style={{
            height: '48px',
            padding: '0 16px',
            background: 'var(--background-subtle)',
            'flex-shrink': '0',
          }}
        >
          <div class="flex items-center gap-2.5" style={{ height: '100%' }}>
            <Sparkles class="w-4 h-4" style={{ color: 'var(--accent)' }} />
            <span class="text-[14px] font-semibold" style={{ color: 'var(--text-primary)' }}>
              What's New
            </span>
            {/* Version badge */}
            <div
              style={{
                padding: '2px 8px',
                'border-radius': '8px',
                background: 'rgba(10, 132, 255, 0.08)',
              }}
            >
              <span
                style={{
                  color: 'var(--accent)',
                  'font-family': 'var(--font-mono)',
                  'font-size': '10px',
                  'font-weight': '500',
                }}
              >
                v{APP_VERSION}
              </span>
            </div>
          </div>
          <button
            type="button"
            onClick={() => props.onClose()}
            class="flex items-center justify-center transition-colors"
            style={{
              background: 'none',
              border: 'none',
              cursor: 'pointer',
              color: 'var(--text-muted)',
              padding: '4px',
            }}
          >
            <X class="w-4 h-4" />
          </button>
        </div>

        {/* Body — scrollable */}
        <div
          style={{
            padding: '20px',
            'overflow-y': 'auto',
            display: 'flex',
            'flex-direction': 'column',
            gap: '16px',
          }}
        >
          <For each={CHANGELOG}>
            {(entry, entryIdx) => (
              <>
                {/* Version separator for non-first entries */}
                <Show when={entryIdx() > 0}>
                  <div
                    style={{
                      height: '1px',
                      background: 'var(--border-subtle)',
                      margin: '4px 0',
                    }}
                  />
                  <div
                    style={{
                      color: 'var(--text-muted)',
                      'font-size': '11px',
                      'font-weight': '500',
                    }}
                  >
                    v{entry.version}
                  </div>
                </Show>

                <For each={entry.items}>
                  {(item, itemIdx) => {
                    const cfg = KIND_CONFIG[item.kind]
                    return (
                      <>
                        {/* Divider between items (not before first in first entry) */}
                        <Show when={entryIdx() > 0 || itemIdx() > 0}>
                          <div
                            style={{
                              height: '1px',
                              background: 'var(--border-subtle)',
                            }}
                          />
                        </Show>

                        <div
                          style={{
                            display: 'flex',
                            'flex-direction': 'column',
                            gap: '6px',
                          }}
                        >
                          {/* Tag + Title row */}
                          <div class="flex items-center gap-2">
                            {/* Color-coded tag */}
                            <div
                              style={{
                                padding: '2px 8px',
                                'border-radius': '8px',
                                background: cfg.bg,
                              }}
                            >
                              <span
                                style={{
                                  color: cfg.color,
                                  'font-size': '9px',
                                  'font-weight': '600',
                                }}
                              >
                                {cfg.label}
                              </span>
                            </div>
                            <span
                              style={{
                                color: 'var(--text-primary)',
                                'font-size': '13px',
                                'font-weight': '500',
                              }}
                            >
                              {item.title}
                            </span>
                          </div>

                          {/* Description */}
                          <p
                            style={{
                              color: 'var(--text-muted)',
                              'font-size': '12px',
                              'line-height': '1.5',
                              margin: '0',
                            }}
                          >
                            {item.description}
                          </p>
                        </div>
                      </>
                    )
                  }}
                </For>
              </>
            )}
          </For>
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
