/**
 * Models List Section — compact model tag display with refresh
 *
 * Shows model names as chips with a refresh button to re-fetch from API.
 */

import { Loader2, RefreshCw } from 'lucide-solid'
import { type Component, For, Show } from 'solid-js'
import type { ProviderModel } from '../../../../config/defaults/provider-defaults'
import { SettingsSelect } from '../../shared-settings-components'
import { formatContextWindow } from '../providers-tab-utils'

interface ModelsListSectionProps {
  models: ProviderModel[]
  defaultModel?: string
  isLoading: boolean
  error: string | null
  onRefresh: () => void
  onSelectDefault?: (modelId: string) => void
}

export const ModelsListSection: Component<ModelsListSectionProps> = (props) => {
  return (
    <div class="space-y-1">
      <div class="flex items-center justify-between">
        <span class="text-[var(--settings-text-badge)] font-medium text-[var(--text-muted)]">
          {props.models.length} models
        </span>
        <button
          type="button"
          onClick={() => props.onRefresh()}
          disabled={props.isLoading}
          class="flex items-center gap-1 text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--accent)] disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
          title="Refresh models from API"
        >
          <Show when={props.isLoading} fallback={<RefreshCw class="w-2.5 h-2.5" />}>
            <Loader2 class="w-2.5 h-2.5 animate-spin" />
          </Show>
          Refresh
        </button>
      </div>
      <div class="flex flex-wrap gap-1">
        <For each={props.models.slice(0, 6)}>
          {(model) => (
            <span
              class={`px-1.5 py-0.5 text-[var(--settings-text-caption)] rounded-[var(--radius-sm)] border cursor-default ${
                model.id === props.defaultModel
                  ? 'border-[var(--accent)] text-[var(--accent)] bg-[var(--accent)]/5'
                  : 'border-[var(--border-subtle)] text-[var(--text-muted)]'
              }`}
              title={`${model.name} · ${formatContextWindow(model.contextWindow)}`}
            >
              {model.name}
            </span>
          )}
        </For>
        <Show when={props.models.length > 6}>
          <span class="px-1.5 py-0.5 text-[var(--settings-text-caption)] text-[var(--text-muted)]">
            +{props.models.length - 6} more
          </span>
        </Show>
      </div>
      <Show when={props.models.length > 0 && props.onSelectDefault}>
        <SettingsSelect
          value={props.defaultModel || props.models[0]?.id || ''}
          onChange={(value) => props.onSelectDefault?.(value)}
          options={props.models.map((model) => ({ value: model.id, label: model.name }))}
          label="Default model"
        />
      </Show>
      <Show when={props.error}>
        <p class="text-[var(--settings-text-badge)] text-[var(--error)] px-1">{props.error}</p>
      </Show>
    </div>
  )
}
