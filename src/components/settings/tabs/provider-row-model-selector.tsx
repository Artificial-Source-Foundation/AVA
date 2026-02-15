import { Loader2, RefreshCw } from 'lucide-solid'
import { type Accessor, type Component, For, Show } from 'solid-js'
import type { ProviderModel } from '../../../config/defaults/provider-defaults'
import { supportsDynamicFetch } from '../../../services/providers/model-fetcher'
import type { LLMProvider } from '../../../types/llm'
import { formatContextWindow } from './providers-tab-utils'

interface ProviderRowModelSelectorProps {
  providerId: string
  models: ProviderModel[]
  defaultModel?: string
  modelError: Accessor<string | null>
  isLoadingModels: Accessor<boolean>
  onSetDefaultModel: (modelId: string) => void
  onRefreshModels: () => void
}

export const ProviderRowModelSelector: Component<ProviderRowModelSelectorProps> = (props) => (
  <Show when={props.models.length > 0}>
    <div class="flex items-center gap-2">
      <select
        value={props.defaultModel || ''}
        onChange={(e) => props.onSetDefaultModel(e.currentTarget.value)}
        class="flex-1 px-3 py-2 bg-[var(--input-background)] text-xs text-[var(--text-primary)] border border-[var(--input-border)] rounded-[var(--radius-md)] focus:outline-none focus:border-[var(--input-border-focus)] transition-colors appearance-none cursor-pointer"
        style={{
          'background-image': `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%2352525b' stroke-width='2'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E")`,
          'background-repeat': 'no-repeat',
          'background-position': 'right 10px center',
        }}
      >
        <For each={props.models}>
          {(model) => (
            <option value={model.id}>
              {model.name} · {formatContextWindow(model.contextWindow)}
            </option>
          )}
        </For>
      </select>
      <Show when={supportsDynamicFetch(props.providerId as LLMProvider)}>
        <button
          type="button"
          onClick={props.onRefreshModels}
          disabled={props.isLoadingModels()}
          class="px-2 py-2 text-[var(--text-muted)] hover:text-[var(--text-primary)] disabled:opacity-50 transition-colors"
        >
          <Show when={!props.isLoadingModels()} fallback={<Loader2 class="w-3 h-3 animate-spin" />}>
            <RefreshCw class="w-3 h-3" />
          </Show>
        </button>
      </Show>
    </div>
    <Show when={props.modelError()}>
      <p class="text-[10px] text-[var(--error)] px-1">{props.modelError()}</p>
    </Show>
  </Show>
)
