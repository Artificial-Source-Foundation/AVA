/**
 * Provider Card
 *
 * Pencil macOS-inspired card: rounded-12, fill #111114, border #ffffff08.
 * Icon 28x28 rounded-7 with provider color bg, status dot + label, toggle.
 */

import { Puzzle } from 'lucide-solid'
import { type Component, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import type { LLMProviderConfig } from '../../../../config/defaults/provider-defaults'
import { defaultProviders } from '../../../../config/defaults/provider-defaults'
import { getProviderLogo } from '../../../icons/provider-logo-map'
import { Toggle } from '../../../ui/Toggle'
import { checkStoredOAuth } from '../providers-tab-helpers'
import { ProviderCardExpanded } from './provider-card-expanded'

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
  onSaveBaseUrl?: (url: string) => void
}

const BUILTIN_IDS = new Set(defaultProviders.map((p) => p.id))

/** Provider brand colors for icon backgrounds */
const PROVIDER_COLORS: Record<string, { bg: string; text: string }> = {
  anthropic: { bg: '#F5762315', text: '#F57623' },
  openai: { bg: '#74AA9C15', text: '#74AA9C' },
  google: { bg: '#4285F415', text: '#4285F4' },
  gemini: { bg: '#4285F415', text: '#4285F4' },
  openrouter: { bg: '#6366F115', text: '#6366F1' },
  ollama: { bg: '#ffffff10', text: '#C8C8CC' },
  copilot: { bg: '#ffffff10', text: '#C8C8CC' },
  inception: { bg: '#FF6B3515', text: '#FF6B35' },
  alibaba: { bg: '#FF6A0015', text: '#FF6A00' },
  zai: { bg: '#0EA5E915', text: '#0EA5E9' },
  kimi: { bg: '#7C3AED15', text: '#7C3AED' },
  minimax: { bg: '#F59E0B15', text: '#F59E0B' },
}

export const ProviderCard: Component<ProviderCardProps> = (props) => {
  const isPlugin = () => !BUILTIN_IDS.has(props.provider.id)
  const isConnected = () =>
    props.provider.status === 'connected' ||
    !!props.provider.apiKey ||
    checkStoredOAuth(props.provider.id)
  const effectiveStatus = (): 'connected' | 'disconnected' | 'error' =>
    props.provider.status === 'error' ? 'error' : isConnected() ? 'connected' : 'disconnected'

  const colors = () => PROVIDER_COLORS[props.provider.id] ?? { bg: '#ffffff10', text: '#C8C8CC' }

  return (
    <div
      style={{
        'border-radius': '12px',
        background: '#111114',
        border: `1px solid ${props.isExpanded ? '#0A84FF40' : '#ffffff08'}`,
        display: 'flex',
        'flex-direction': 'column',
        gap: '12px',
        padding: '16px',
        transition: 'border-color 150ms',
        contain: 'layout style paint',
      }}
    >
      {/* Card header */}
      <div class="flex items-center justify-between" style={{ width: '100%' }}>
        <button
          type="button"
          onClick={() => props.onExpand()}
          class="flex items-center flex-1 min-w-0 text-left"
          style={{
            gap: '10px',
            background: 'transparent',
            border: 'none',
            cursor: 'pointer',
            padding: '0',
          }}
        >
          {/* Provider icon */}
          <div
            class="flex items-center justify-center flex-shrink-0"
            style={{
              width: '28px',
              height: '28px',
              'border-radius': '7px',
              background: colors().bg,
            }}
          >
            <Show
              when={!isPlugin()}
              fallback={<Puzzle style={{ width: '14px', height: '14px', color: '#0A84FF' }} />}
            >
              <Dynamic component={getProviderLogo(props.provider.id)} class="w-4 h-4" />
            </Show>
          </div>

          {/* Name + model */}
          <div class="min-w-0 flex-1">
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                'font-weight': '500',
                color: '#F5F5F7',
                display: 'block',
              }}
            >
              {props.provider.name}
            </span>
            <span
              style={{
                'font-family': 'Geist Mono, monospace',
                'font-size': '11px',
                color: '#48484A',
                display: 'block',
                'margin-top': '1px',
              }}
            >
              {props.provider.defaultModel || 'Not configured'}
            </span>
          </div>
        </button>

        {/* Status */}
        <div class="flex items-center" style={{ gap: '6px' }}>
          <div
            style={{
              width: '6px',
              height: '6px',
              'border-radius': '50%',
              background:
                effectiveStatus() === 'connected'
                  ? '#34C759'
                  : effectiveStatus() === 'error'
                    ? '#FF453A'
                    : '#48484A',
            }}
          />
          <span
            style={{
              'font-family': 'Geist, sans-serif',
              'font-size': '11px',
              color:
                effectiveStatus() === 'connected'
                  ? '#34C759'
                  : effectiveStatus() === 'error'
                    ? '#FF453A'
                    : '#48484A',
            }}
          >
            {effectiveStatus() === 'connected'
              ? 'Connected'
              : effectiveStatus() === 'error'
                ? 'Error'
                : 'Disconnected'}
          </span>
        </div>
      </div>

      {/* Toggle */}
      <Toggle checked={props.provider.enabled} onChange={(v) => props.onToggle?.(v)} />

      {/* Expanded configuration */}
      <div class="tool-card-body-grid" data-expanded={props.isExpanded ? 'true' : 'false'}>
        <div class="tool-card-body-inner">
          <Show when={props.isExpanded}>
            <ProviderCardExpanded
              provider={props.provider}
              onSaveApiKey={props.onSaveApiKey}
              onClearApiKey={props.onClearApiKey}
              onSetDefaultModel={props.onSetDefaultModel}
              onTestConnection={props.onTestConnection}
              onUpdateModels={props.onUpdateModels}
              onSaveBaseUrl={props.onSaveBaseUrl}
            />
          </Show>
        </div>
      </div>
    </div>
  )
}
