/**
 * Provider Card
 *
 * Visual card showing provider icon, name, status, description, and toggle.
 * Click to expand for configuration.
 */

import { ChevronRight, Puzzle } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { LLMProviderConfig } from '../../../../config/defaults/provider-defaults'
import { defaultProviders } from '../../../../config/defaults/provider-defaults'
import { checkStoredOAuth } from '../providers-tab-helpers'
import { ProviderCardExpanded } from './provider-card-expanded'
import { ProviderCardStatus } from './provider-card-status'

interface ProviderCardProps {
  provider: LLMProviderConfig
  isExpanded: boolean
  onExpand: () => void
  onToggle?: (enabled: boolean) => void
  onSaveApiKey?: (key: string) => void
  onClearApiKey?: () => void
  onSetDefaultModel?: (modelId: string) => void
  onTestConnection?: () => void
  onUpdateModels?: (models: LLMProviderConfig['models']) => void
}

const BUILTIN_IDS = new Set(defaultProviders.map((p) => p.id))

export const ProviderCard: Component<ProviderCardProps> = (props) => {
  const isPlugin = () => !BUILTIN_IDS.has(props.provider.id)
  const isConnected = () =>
    props.provider.status === 'connected' ||
    !!props.provider.apiKey ||
    checkStoredOAuth(props.provider.id)
  const effectiveStatus = (): 'connected' | 'disconnected' | 'error' =>
    props.provider.status === 'error' ? 'error' : isConnected() ? 'connected' : 'disconnected'

  return (
    <div
      class={`
        rounded-[var(--radius-lg)] border transition-all duration-150
        ${
          props.isExpanded
            ? 'border-[var(--accent)] bg-[var(--surface-raised)]'
            : 'border-[var(--border-subtle)] bg-[var(--surface-raised)] hover:border-[var(--border-default)]'
        }
      `}
    >
      {/* Collapsed header */}
      <div class="flex items-center gap-3 p-3">
        <button
          type="button"
          onClick={props.onExpand}
          class="flex items-center gap-3 flex-1 min-w-0 text-left"
        >
          {/* Icon */}
          <div
            class="w-8 h-8 rounded-[var(--radius-md)] flex items-center justify-center flex-shrink-0"
            style={{ background: 'var(--alpha-white-5)' }}
          >
            <Show when={!isPlugin()} fallback={<Puzzle class="w-4 h-4 text-[var(--accent)]" />}>
              <Dynamic
                component={props.provider.icon}
                class="w-4 h-4 text-[var(--text-secondary)]"
              />
            </Show>
          </div>

          {/* Name + description */}
          <div class="min-w-0 flex-1">
            <div class="flex items-center gap-1.5">
              <ProviderCardStatus status={effectiveStatus()} />
              <span class="text-xs font-medium text-[var(--text-primary)] truncate">
                {props.provider.name}
              </span>
              <Show when={isPlugin()}>
                <span class="px-1.5 py-0.5 text-[8px] font-semibold uppercase tracking-wider text-[var(--accent)] bg-[var(--accent)]/10 rounded-full">
                  Plugin
                </span>
              </Show>
            </div>
            <p class="text-[10px] text-[var(--text-muted)] mt-0.5">{props.provider.description}</p>
          </div>
        </button>

        {/* Toggle + chevron */}
        <div class="flex items-center gap-2 flex-shrink-0">
          <button
            type="button"
            onClick={() => props.onToggle?.(!props.provider.enabled)}
            class={`w-9 h-5 rounded-full transition-colors flex items-center ${
              props.provider.enabled ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'
            }`}
            aria-label={`Toggle ${props.provider.name}`}
          >
            <span
              class={`w-4 h-4 rounded-full bg-white shadow-sm transition-transform duration-150 ${
                props.provider.enabled ? 'translate-x-[18px]' : 'translate-x-[2px]'
              }`}
            />
          </button>
          <ChevronRight
            class={`w-3.5 h-3.5 text-[var(--text-muted)] transition-transform duration-150 ${
              props.isExpanded ? 'rotate-90' : ''
            }`}
          />
        </div>
      </div>

      {/* Expanded configuration */}
      <Show when={props.isExpanded}>
        <ProviderCardExpanded
          provider={props.provider}
          onSaveApiKey={props.onSaveApiKey}
          onClearApiKey={props.onClearApiKey}
          onSetDefaultModel={props.onSetDefaultModel}
          onTestConnection={props.onTestConnection}
          onUpdateModels={props.onUpdateModels}
        />
      </Show>
    </div>
  )
}
