import { Mic } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'

export interface MicButtonProps {
  isRecording: Accessor<boolean>
  onToggle: () => void
  supported: Accessor<boolean>
}

export const MicButton: Component<MicButtonProps> = (props) => (
  <Show when={props.supported()}>
    <button
      type="button"
      onClick={() => props.onToggle()}
      class={`
        flex items-center gap-1 px-1.5 py-1
        text-[var(--text-xs)] font-medium rounded-[var(--radius-md)]
        transition-all duration-200
        ${
          props.isRecording()
            ? 'text-[var(--error)] bg-[color-mix(in_srgb,var(--error)_10%,transparent)]'
            : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)] bg-transparent hover:bg-[var(--surface-raised)]'
        }
      `}
      title={props.isRecording() ? 'Voice dictation (recording)' : 'Voice dictation'}
    >
      <Mic class="w-3.5 h-3.5" />
      <Show when={props.isRecording()}>
        <span class="w-1.5 h-1.5 rounded-full bg-[var(--error)] animate-pulse" />
      </Show>
    </button>
  </Show>
)
