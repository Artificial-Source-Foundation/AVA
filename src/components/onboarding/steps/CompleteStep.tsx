/**
 * Step 5: You're All Set
 *
 * Green checkmark, keyboard shortcuts table, and "Start Coding" CTA.
 */

import { Check } from 'lucide-solid'
import { type Component, For } from 'solid-js'

// ---------------------------------------------------------------------------
// Shortcut data
// ---------------------------------------------------------------------------

const SHORTCUTS: { label: string; keys: string }[] = [
  { label: 'New session', keys: 'Ctrl+N' },
  { label: 'Command palette', keys: 'Ctrl+/' },
  { label: 'Switch model', keys: 'Ctrl+M' },
  { label: 'Toggle thinking', keys: 'Ctrl+T' },
  { label: 'Toggle sidebar', keys: 'Ctrl+S' },
]

// ---------------------------------------------------------------------------
// Props
// ---------------------------------------------------------------------------

export interface CompleteStepProps {
  onComplete: () => void
}

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const CompleteStep: Component<CompleteStepProps> = (props) => (
  <div class="flex flex-col items-center">
    {/* Green checkmark */}
    <div
      class="w-16 h-16 rounded-full flex items-center justify-center mb-6"
      style={{ background: 'var(--success-subtle)' }}
    >
      <Check class="w-8 h-8 text-[var(--success)]" />
    </div>

    {/* Title */}
    <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-8">
      You're All Set
    </h2>

    {/* Shortcuts */}
    <div class="w-full max-w-[400px] flex flex-col gap-2 mb-10">
      <For each={SHORTCUTS}>
        {(shortcut) => (
          <div class="flex items-center justify-between py-1.5">
            <span class="text-sm text-[var(--text-primary)]">{shortcut.label}</span>
            <kbd
              class="px-2 py-1 text-xs rounded-md"
              style={{
                background: 'var(--surface-raised)',
                color: 'var(--text-muted)',
                'font-family': '"JetBrains Mono", monospace',
              }}
            >
              {shortcut.keys}
            </kbd>
          </div>
        )}
      </For>
    </div>

    {/* Start Coding button */}
    <button
      type="button"
      onClick={props.onComplete}
      class="px-10 py-3 bg-[var(--accent)] hover:bg-[var(--violet-8)] text-white text-sm font-semibold rounded-xl transition-colors"
    >
      Start Coding
    </button>
  </div>
)
