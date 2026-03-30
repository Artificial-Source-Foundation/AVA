import { RotateCcw } from 'lucide-solid'
import { type Accessor, type Component, createMemo, createSignal, Show } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { ModelBrowserDialog } from './model-browser-dialog'
import {
  aggregateModels,
  formatModelSelectionLabel,
  matchesModelSelection,
} from './model-browser-helpers'

interface ModelPickerFieldProps {
  value: Accessor<string>
  providers: Accessor<LLMProviderConfig[]>
  onSelect: (modelId: string, providerId: string) => void
  onClear?: () => void
  selectedProvider?: Accessor<string | null>
  autoLabel?: string
  fallbackValue?: Accessor<string | null | undefined>
  includeProvider?: boolean
  buttonClass?: string
  buttonStyle?: Record<string, string>
  resetLabel?: string
}

export const ModelPickerField: Component<ModelPickerFieldProps> = (props) => {
  const [open, setOpen] = createSignal(false)

  const enabledProviders = createMemo(() =>
    props.providers().filter((provider) => provider.enabled || provider.status === 'connected')
  )

  const allModels = createMemo(() => aggregateModels(enabledProviders()))

  const displayValue = createMemo(() =>
    formatModelSelectionLabel(allModels(), props.value() || props.fallbackValue?.() || '', {
      autoLabel: props.autoLabel,
      includeProvider: props.includeProvider,
      providerId: props.selectedProvider?.() ?? null,
    })
  )

  const hasSelection = createMemo(() => {
    const value = props.value()
    if (!value) return false
    return allModels().some((model) => matchesModelSelection(value, model.id, model.providerId))
  })

  return (
    <>
      <div class="flex items-center gap-2 min-w-0">
        <button
          type="button"
          class={
            props.buttonClass ??
            'flex-1 min-w-0 h-9 rounded-md border border-[var(--border-subtle)] bg-[var(--input-background)] px-3 text-left text-xs text-[var(--text-primary)] transition-colors hover:border-[var(--accent-border)]'
          }
          style={props.buttonStyle}
          onClick={() => setOpen(true)}
        >
          <span class="block truncate">{displayValue()}</span>
        </button>
        <Show when={props.onClear && (props.value() || hasSelection())}>
          <button
            type="button"
            class="inline-flex h-9 items-center gap-1 rounded-md border border-[var(--border-subtle)] px-2.5 text-[11px] font-medium text-[var(--text-secondary)] transition-colors hover:border-[var(--accent-border)] hover:text-[var(--text-primary)]"
            onClick={() => props.onClear?.()}
          >
            <RotateCcw class="h-3.5 w-3.5" />
            {props.resetLabel ?? 'Auto'}
          </button>
        </Show>
      </div>

      <ModelBrowserDialog
        open={open}
        onOpenChange={setOpen}
        selectedModel={props.value}
        selectedProvider={props.selectedProvider}
        onSelect={props.onSelect}
        enabledProviders={enabledProviders}
      />
    </>
  )
}
