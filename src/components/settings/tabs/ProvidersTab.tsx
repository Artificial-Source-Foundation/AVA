/**
 * Providers Settings Tab
 *
 * Configure LLM provider settings, model preferences, and API configuration.
 */

import {
  AlertTriangle,
  Bot,
  Check,
  ChevronDown,
  ChevronUp,
  Cpu,
  ExternalLink,
  Eye,
  EyeOff,
  Globe,
  Key,
  RefreshCw,
  Sparkles,
  Trash2,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import { Button } from '../../ui/Button'
import { Toggle } from '../../ui/Toggle'

// ============================================================================
// Types
// ============================================================================

type IconComponent = Component<{ class?: string }>

export interface LLMProviderConfig {
  id: string
  name: string
  icon: IconComponent
  description: string
  enabled: boolean
  apiKey?: string
  baseUrl?: string
  defaultModel?: string
  models: ProviderModel[]
  status: 'connected' | 'disconnected' | 'error'
  error?: string
}

export interface ProviderModel {
  id: string
  name: string
  contextWindow: number
  isDefault?: boolean
}

export interface ProvidersTabProps {
  providers: LLMProviderConfig[]
  onToggle?: (id: string, enabled: boolean) => void
  onSaveApiKey?: (id: string, key: string) => void
  onClearApiKey?: (id: string) => void
  onSetDefaultModel?: (providerId: string, modelId: string) => void
  onTestConnection?: (id: string) => void
}

// ============================================================================
// Provider Icons
// ============================================================================

const providerIcons: Record<string, IconComponent> = {
  anthropic: Sparkles as IconComponent,
  openai: Cpu as IconComponent,
  openrouter: Zap as IconComponent,
  ollama: Bot as IconComponent,
  custom: Globe as IconComponent,
}

// ============================================================================
// Providers Tab Component
// ============================================================================

export const ProvidersTab: Component<ProvidersTabProps> = (props) => {
  const connectedCount = () => props.providers.filter((p) => p.status === 'connected').length

  return (
    <div class="space-y-6">
      {/* Header */}
      <div class="flex items-center justify-between">
        <div>
          <h3 class="text-sm font-medium text-[var(--text-primary)]">LLM Providers</h3>
          <p class="text-xs text-[var(--text-muted)] mt-0.5">
            {connectedCount()} of {props.providers.length} providers connected
          </p>
        </div>
      </div>

      {/* Provider List */}
      <div class="space-y-3">
        <For each={props.providers}>
          {(provider) => (
            <ProviderCard
              provider={provider}
              onToggle={(enabled) => props.onToggle?.(provider.id, enabled)}
              onSaveApiKey={(key) => props.onSaveApiKey?.(provider.id, key)}
              onClearApiKey={() => props.onClearApiKey?.(provider.id)}
              onSetDefaultModel={(modelId) => props.onSetDefaultModel?.(provider.id, modelId)}
              onTestConnection={() => props.onTestConnection?.(provider.id)}
            />
          )}
        </For>
      </div>

      {/* Info */}
      <div class="flex items-start gap-3 p-3 bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-lg)]">
        <Key class="w-5 h-5 text-[var(--info)] flex-shrink-0 mt-0.5" />
        <div class="text-sm text-[var(--text-secondary)]">
          <p>
            API keys are stored securely on your device and never sent to any server except the
            respective provider.
          </p>
        </div>
      </div>
    </div>
  )
}

// ============================================================================
// Provider Card Component
// ============================================================================

interface ProviderCardProps {
  provider: LLMProviderConfig
  onToggle?: (enabled: boolean) => void
  onSaveApiKey?: (key: string) => void
  onClearApiKey?: () => void
  onSetDefaultModel?: (modelId: string) => void
  onTestConnection?: () => void
}

const ProviderCard: Component<ProviderCardProps> = (props) => {
  const [expanded, setExpanded] = createSignal(false)
  const [apiKey, setApiKey] = createSignal(props.provider.apiKey ? '••••••••••••' : '')
  const [showKey, setShowKey] = createSignal(false)
  const [isEditing, setIsEditing] = createSignal(false)

  const icon = () => providerIcons[props.provider.id] || providerIcons.custom

  const statusConfig = {
    connected: {
      label: 'Connected',
      color: 'var(--success)',
      bg: 'var(--success-subtle)',
      icon: Check,
    },
    disconnected: {
      label: 'Not configured',
      color: 'var(--text-muted)',
      bg: 'var(--surface-sunken)',
      icon: Key,
    },
    error: {
      label: 'Error',
      color: 'var(--error)',
      bg: 'var(--error-subtle)',
      icon: AlertTriangle,
    },
  }

  const status = () => statusConfig[props.provider.status]

  const handleSaveKey = () => {
    if (apiKey() && !apiKey().includes('••••')) {
      props.onSaveApiKey?.(apiKey())
      setIsEditing(false)
    }
  }

  const handleClearKey = () => {
    props.onClearApiKey?.()
    setApiKey('')
    setIsEditing(false)
  }

  return (
    <div class="border border-[var(--border-subtle)] rounded-[var(--radius-lg)] overflow-hidden">
      {/* Header */}
      <button
        type="button"
        class={`
          w-full text-left
          flex items-center gap-3 p-3 cursor-pointer
          hover:bg-[var(--surface-raised)]
          transition-colors duration-[var(--duration-fast)]
          ${props.provider.enabled ? 'bg-[var(--surface-raised)]' : ''}
        `}
        onClick={() => setExpanded(!expanded())}
      >
        {/* Icon */}
        <div
          class={`
            p-2 rounded-[var(--radius-md)]
            ${
              props.provider.enabled
                ? 'bg-[var(--accent)] text-white'
                : 'bg-[var(--surface-sunken)] text-[var(--text-muted)]'
            }
          `}
        >
          <Dynamic component={icon()} class="w-4 h-4" />
        </div>

        {/* Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-2">
            <span class="text-sm font-medium text-[var(--text-primary)]">
              {props.provider.name}
            </span>
            <span
              class="flex items-center gap-1 px-1.5 py-0.5 text-[10px] font-medium rounded-full"
              style={{ background: status().bg, color: status().color }}
            >
              <Dynamic component={status().icon} class="w-2.5 h-2.5" />
              {status().label}
            </span>
          </div>
          <div class="text-xs text-[var(--text-muted)]">{props.provider.description}</div>
        </div>

        {/* Toggle & Expand */}
        {/* biome-ignore lint/a11y/useKeyWithClickEvents: stopPropagation wrapper for nested controls */}
        {/* biome-ignore lint/a11y/noStaticElementInteractions: container element with nested interactive controls */}
        <div class="flex items-center gap-2" onClick={(e) => e.stopPropagation()}>
          <Toggle
            checked={props.provider.enabled}
            onChange={(checked) => props.onToggle?.(checked)}
            size="sm"
          />
          <button
            type="button"
            class="p-1 text-[var(--text-muted)]"
            onClick={(e) => {
              e.stopPropagation()
              setExpanded(!expanded())
            }}
          >
            <Show when={expanded()} fallback={<ChevronDown class="w-4 h-4" />}>
              <ChevronUp class="w-4 h-4" />
            </Show>
          </button>
        </div>
      </button>

      {/* Expanded Content */}
      <Show when={expanded()}>
        <div class="border-t border-[var(--border-subtle)] p-4 space-y-4">
          {/* API Key */}
          <div>
            <label
              for={`provider-api-key-${props.provider.id}`}
              class="block text-xs font-medium text-[var(--text-secondary)] mb-2"
            >
              API Key
            </label>
            <div class="flex gap-2">
              <div class="flex-1 relative">
                <input
                  id={`provider-api-key-${props.provider.id}`}
                  type={showKey() ? 'text' : 'password'}
                  value={apiKey()}
                  onInput={(e) => {
                    setApiKey(e.currentTarget.value)
                    setIsEditing(true)
                  }}
                  onFocus={() => {
                    if (apiKey().includes('••••')) {
                      setApiKey('')
                    }
                  }}
                  placeholder={`Enter ${props.provider.name} API key`}
                  class="
                    w-full px-3 py-2 pr-10
                    bg-[var(--input-background)]
                    text-[var(--text-primary)]
                    placeholder-[var(--text-muted)]
                    border border-[var(--input-border)]
                    rounded-[var(--radius-md)]
                    text-sm font-mono
                    focus:outline-none focus:border-[var(--accent)]
                    transition-colors duration-[var(--duration-fast)]
                  "
                />
                <button
                  type="button"
                  onClick={() => setShowKey(!showKey())}
                  class="
                    absolute right-2.5 top-1/2 -translate-y-1/2
                    text-[var(--text-tertiary)] hover:text-[var(--text-primary)]
                  "
                >
                  <Show when={showKey()} fallback={<Eye class="w-4 h-4" />}>
                    <EyeOff class="w-4 h-4" />
                  </Show>
                </button>
              </div>
              <Show when={isEditing()}>
                <Button variant="primary" size="sm" onClick={handleSaveKey}>
                  Save
                </Button>
              </Show>
              <Show when={props.provider.apiKey}>
                <Button variant="ghost" size="sm" onClick={handleClearKey}>
                  <Trash2 class="w-4 h-4 text-[var(--error)]" />
                </Button>
              </Show>
            </div>
          </div>

          {/* Default Model */}
          <Show when={props.provider.models.length > 0}>
            <div>
              <label
                for={`provider-model-${props.provider.id}`}
                class="block text-xs font-medium text-[var(--text-secondary)] mb-2"
              >
                Default Model
              </label>
              <select
                id={`provider-model-${props.provider.id}`}
                value={props.provider.defaultModel || ''}
                onChange={(e) => props.onSetDefaultModel?.(e.currentTarget.value)}
                class="
                  w-full px-3 py-2
                  bg-[var(--input-background)]
                  border border-[var(--input-border)]
                  rounded-[var(--radius-md)]
                  text-sm text-[var(--text-primary)]
                  focus:outline-none focus:border-[var(--accent)]
                  transition-colors duration-[var(--duration-fast)]
                "
              >
                <For each={props.provider.models}>
                  {(model) => (
                    <option value={model.id}>
                      {model.name} ({formatContextWindow(model.contextWindow)})
                    </option>
                  )}
                </For>
              </select>
            </div>
          </Show>

          {/* Base URL (for custom/ollama) */}
          <Show when={props.provider.id === 'ollama' || props.provider.id === 'custom'}>
            <div>
              <label
                for={`provider-baseurl-${props.provider.id}`}
                class="block text-xs font-medium text-[var(--text-secondary)] mb-2"
              >
                Base URL
              </label>
              <input
                id={`provider-baseurl-${props.provider.id}`}
                type="url"
                value={props.provider.baseUrl || ''}
                placeholder="http://localhost:11434"
                class="
                  w-full px-3 py-2
                  bg-[var(--input-background)]
                  border border-[var(--input-border)]
                  rounded-[var(--radius-md)]
                  text-sm text-[var(--text-primary)]
                  placeholder-[var(--text-muted)]
                  focus:outline-none focus:border-[var(--accent)]
                  transition-colors duration-[var(--duration-fast)]
                "
              />
            </div>
          </Show>

          {/* Test Connection */}
          <Show when={props.provider.apiKey}>
            <div class="flex items-center justify-between pt-2">
              <Button variant="ghost" size="sm" onClick={() => props.onTestConnection?.()}>
                <RefreshCw class="w-4 h-4 mr-1" />
                Test Connection
              </Button>

              {/* Provider Docs Link */}
              <a
                href={getProviderDocsUrl(props.provider.id)}
                target="_blank"
                rel="noopener noreferrer"
                class="
                  flex items-center gap-1 text-xs
                  text-[var(--text-muted)] hover:text-[var(--accent)]
                  transition-colors duration-[var(--duration-fast)]
                "
              >
                API Documentation
                <ExternalLink class="w-3 h-3" />
              </a>
            </div>
          </Show>

          {/* Error Message */}
          <Show when={props.provider.status === 'error' && props.provider.error}>
            <div class="flex items-start gap-2 p-2 bg-[var(--error-subtle)] rounded-[var(--radius-md)]">
              <AlertTriangle class="w-4 h-4 text-[var(--error)] flex-shrink-0 mt-0.5" />
              <p class="text-xs text-[var(--error)]">{props.provider.error}</p>
            </div>
          </Show>
        </div>
      </Show>
    </div>
  )
}

// ============================================================================
// Helpers
// ============================================================================

const formatContextWindow = (tokens: number): string => {
  if (tokens >= 1000000) return `${(tokens / 1000000).toFixed(1)}M`
  if (tokens >= 1000) return `${(tokens / 1000).toFixed(0)}K`
  return tokens.toString()
}

const getProviderDocsUrl = (providerId: string): string => {
  const urls: Record<string, string> = {
    anthropic: 'https://docs.anthropic.com/en/api',
    openai: 'https://platform.openai.com/docs/api-reference',
    openrouter: 'https://openrouter.ai/docs',
    ollama: 'https://ollama.ai/docs',
  }
  return urls[providerId] || '#'
}

// ============================================================================
// Default Provider Configurations
// ============================================================================

export const defaultProviders: LLMProviderConfig[] = [
  {
    id: 'anthropic',
    name: 'Anthropic',
    icon: Sparkles as IconComponent,
    description: 'Claude models with advanced reasoning',
    enabled: true,
    status: 'disconnected',
    models: [
      {
        id: 'claude-3-5-sonnet-20241022',
        name: 'Claude 3.5 Sonnet',
        contextWindow: 200000,
        isDefault: true,
      },
      { id: 'claude-3-opus-20240229', name: 'Claude 3 Opus', contextWindow: 200000 },
      { id: 'claude-3-haiku-20240307', name: 'Claude 3 Haiku', contextWindow: 200000 },
    ],
    defaultModel: 'claude-3-5-sonnet-20241022',
  },
  {
    id: 'openai',
    name: 'OpenAI',
    icon: Cpu as IconComponent,
    description: 'GPT models and embeddings',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'gpt-4-turbo', name: 'GPT-4 Turbo', contextWindow: 128000, isDefault: true },
      { id: 'gpt-4o', name: 'GPT-4o', contextWindow: 128000 },
      { id: 'gpt-3.5-turbo', name: 'GPT-3.5 Turbo', contextWindow: 16000 },
    ],
    defaultModel: 'gpt-4-turbo',
  },
  {
    id: 'openrouter',
    name: 'OpenRouter',
    icon: Zap as IconComponent,
    description: 'Access to 100+ models via single API',
    enabled: false,
    status: 'disconnected',
    models: [
      {
        id: 'anthropic/claude-3.5-sonnet',
        name: 'Claude 3.5 Sonnet',
        contextWindow: 200000,
        isDefault: true,
      },
      { id: 'openai/gpt-4-turbo', name: 'GPT-4 Turbo', contextWindow: 128000 },
      { id: 'google/gemini-pro', name: 'Gemini Pro', contextWindow: 32000 },
    ],
    defaultModel: 'anthropic/claude-3.5-sonnet',
  },
  {
    id: 'ollama',
    name: 'Ollama',
    icon: Bot as IconComponent,
    description: 'Run local models on your machine',
    enabled: false,
    status: 'disconnected',
    baseUrl: 'http://localhost:11434',
    models: [
      { id: 'llama2', name: 'Llama 2', contextWindow: 4096 },
      { id: 'codellama', name: 'Code Llama', contextWindow: 16000 },
      { id: 'mistral', name: 'Mistral', contextWindow: 8000 },
    ],
  },
]
