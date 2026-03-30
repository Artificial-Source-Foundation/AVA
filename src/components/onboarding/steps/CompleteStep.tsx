/**
 * Step 5: You're All Set
 *
 * Green checkmark circle (56px), title, shortcuts table in dark card,
 * "Start Coding" CTA button.
 */

import { Check, Rocket } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'

// ---------------------------------------------------------------------------
// Shortcut data
// ---------------------------------------------------------------------------

const SHORTCUTS: { label: string; mod: string; key: string }[] = [
  { label: 'New session', mod: 'Ctrl', key: 'N' },
  { label: 'Command palette', mod: 'Ctrl', key: 'K' },
  { label: 'Switch model', mod: 'Ctrl', key: 'M' },
  { label: 'Settings', mod: 'Ctrl', key: ',' },
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
    {/* Green checkmark circle - 56px */}
    <div
      class="w-14 h-14 rounded-full flex items-center justify-center mb-6"
      style={{ background: 'rgba(34, 197, 94, 0.15)' }}
    >
      <Check class="w-7 h-7" style={{ color: '#22C55E' }} />
    </div>

    {/* Title */}
    <h2 class="text-[22px] font-bold text-[var(--text-primary)] tracking-tight mb-2">
      You're All Set
    </h2>

    {/* Subtitle */}
    <p class="text-sm text-[var(--text-muted)] mb-8">Here are a few shortcuts to get you started</p>

    {/* Shortcuts card - dark surface, rounded-8, subtle border */}
    <div
      class="w-full max-w-[380px] rounded-lg overflow-hidden mb-10"
      style={{
        background: 'var(--surface)',
        border: '1px solid var(--border-subtle)',
      }}
    >
      <For each={SHORTCUTS}>
        {(shortcut, i) => (
          <>
            <Show when={i() > 0}>
              <div style={{ 'border-top': '1px solid var(--border-subtle)' }} />
            </Show>
            <div class="flex items-center justify-between px-4" style={{ height: '36px' }}>
              <span class="text-sm text-[var(--text-primary)]">{shortcut.label}</span>
              <div class="flex items-center gap-1">
                <kbd
                  class="px-1.5 py-0.5 text-[10px] rounded"
                  style={{
                    background: 'rgba(255, 255, 255, 0.03)',
                    color: 'var(--text-muted)',
                    'font-family': '"JetBrains Mono", monospace',
                  }}
                >
                  {shortcut.mod}
                </kbd>
                <kbd
                  class="px-1.5 py-0.5 text-[10px] rounded"
                  style={{
                    background: 'rgba(255, 255, 255, 0.03)',
                    color: 'var(--text-muted)',
                    'font-family': '"JetBrains Mono", monospace',
                  }}
                >
                  {shortcut.key}
                </kbd>
              </div>
            </div>
          </>
        )}
      </For>
    </div>

    {/* Start Coding button - blue filled, rounded-10, with rocket */}
    <button
      type="button"
      onClick={() => props.onComplete()}
      class="px-8 py-3 bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white text-sm font-semibold rounded-[10px] transition-colors flex items-center gap-2"
    >
      <Rocket class="w-4 h-4" />
      Start Coding
    </button>
  </div>
)
