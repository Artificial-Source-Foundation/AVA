import { Eye, EyeOff, Trash2 } from 'lucide-solid'
import { type Accessor, type Component, Show } from 'solid-js'

interface ProviderRowApiKeyInputProps {
  providerName: string
  hasStoredApiKey: boolean
  apiKey: Accessor<string>
  showKey: Accessor<boolean>
  onInput: (value: string) => void
  onToggleVisibility: () => void
  onClearClick: () => void
  onBlur: () => void
}

export const ProviderRowApiKeyInput: Component<ProviderRowApiKeyInputProps> = (props) => (
  <div class="relative">
    <input
      type={props.showKey() ? 'text' : 'password'}
      value={props.apiKey()}
      onInput={(e) => props.onInput(e.currentTarget.value)}
      onFocus={() => props.apiKey().includes('••••') && props.onInput('')}
      onBlur={props.onBlur}
      placeholder={`${props.providerName} API key`}
      class="w-full px-3 py-2 pr-16 bg-[var(--input-background)] text-xs text-[var(--text-primary)] font-mono placeholder:text-[var(--input-placeholder)] border border-[var(--input-border)] rounded-[var(--radius-md)] focus:outline-none focus:border-[var(--input-border-focus)] transition-colors"
    />
    <div class="absolute right-2 top-1/2 -translate-y-1/2 flex items-center gap-1">
      <button
        type="button"
        onClick={props.onToggleVisibility}
        class="p-1 text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
      >
        <Show when={props.showKey()} fallback={<Eye class="w-3 h-3" />}>
          <EyeOff class="w-3 h-3" />
        </Show>
      </button>
      <Show when={props.hasStoredApiKey}>
        <button
          type="button"
          onClick={props.onClearClick}
          class="p-1 text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
          title="Clear API key"
        >
          <Trash2 class="w-3 h-3" />
        </button>
      </Show>
    </div>
  </div>
)
