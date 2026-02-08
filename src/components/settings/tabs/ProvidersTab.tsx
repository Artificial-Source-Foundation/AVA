/**
 * Providers Settings Tab
 *
 * Modern, minimal provider configuration inspired by Cursor/Windsurf/Zed.
 * Features: OAuth-first flow, clean cards, inline editing.
 */

import {
  AlertCircle,
  Bot,
  Braces,
  ChevronRight,
  CircleDot,
  Cloud,
  Cpu,
  ExternalLink,
  Eye,
  EyeOff,
  Flame,
  Globe,
  Loader2,
  LogIn,
  Monitor,
  RefreshCw,
  Shield,
  Sparkles,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { Dynamic } from 'solid-js/web'
import {
  type DeviceCodeResponse,
  isOAuthSupported,
  startOAuthFlow,
} from '../../../services/auth/oauth'
import { fetchModels, supportsDynamicFetch } from '../../../services/providers/model-fetcher'
import type { LLMProvider } from '../../../types/llm'
import { DeviceCodeDialog } from '../DeviceCodeDialog'

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
  onUpdateModels?: (providerId: string, models: ProviderModel[]) => void
}

// ============================================================================
// Provider Icons & Colors (uses CSS variables from tokens.css)
// ============================================================================

const providerConfig: Record<
  string,
  { icon: IconComponent; colorVar: string; subtleVar: string; borderVar: string }
> = {
  anthropic: {
    icon: Sparkles as IconComponent,
    colorVar: '--provider-anthropic',
    subtleVar: '--provider-anthropic-subtle',
    borderVar: '--provider-anthropic-border',
  },
  openai: {
    icon: Cpu as IconComponent,
    colorVar: '--provider-openai',
    subtleVar: '--provider-openai-subtle',
    borderVar: '--provider-openai-subtle',
  },
  google: {
    icon: Globe as IconComponent,
    colorVar: '--provider-google',
    subtleVar: '--provider-google-subtle',
    borderVar: '--provider-google-subtle',
  },
  copilot: {
    icon: Monitor as IconComponent,
    colorVar: '--provider-copilot',
    subtleVar: '--provider-copilot-subtle',
    borderVar: '--provider-copilot-subtle',
  },
  openrouter: {
    icon: Zap as IconComponent,
    colorVar: '--provider-openrouter',
    subtleVar: '--provider-openrouter-subtle',
    borderVar: '--provider-openrouter-subtle',
  },
  xai: {
    icon: Flame as IconComponent,
    colorVar: '--provider-xai',
    subtleVar: '--provider-xai-subtle',
    borderVar: '--provider-xai-subtle',
  },
  mistral: {
    icon: Cloud as IconComponent,
    colorVar: '--provider-mistral',
    subtleVar: '--provider-mistral-subtle',
    borderVar: '--provider-mistral-subtle',
  },
  groq: {
    icon: Zap as IconComponent,
    colorVar: '--provider-groq',
    subtleVar: '--provider-groq-subtle',
    borderVar: '--provider-groq-subtle',
  },
  deepseek: {
    icon: Braces as IconComponent,
    colorVar: '--provider-deepseek',
    subtleVar: '--provider-deepseek-subtle',
    borderVar: '--provider-deepseek-subtle',
  },
  cohere: {
    icon: Shield as IconComponent,
    colorVar: '--provider-cohere',
    subtleVar: '--provider-cohere-subtle',
    borderVar: '--provider-cohere-subtle',
  },
  together: {
    icon: Cloud as IconComponent,
    colorVar: '--provider-together',
    subtleVar: '--provider-together-subtle',
    borderVar: '--provider-together-subtle',
  },
  kimi: {
    icon: Bot as IconComponent,
    colorVar: '--provider-kimi',
    subtleVar: '--provider-kimi-subtle',
    borderVar: '--provider-kimi-subtle',
  },
  glm: {
    icon: Cpu as IconComponent,
    colorVar: '--provider-glm',
    subtleVar: '--provider-glm-subtle',
    borderVar: '--provider-glm-subtle',
  },
  ollama: {
    icon: Bot as IconComponent,
    colorVar: '--provider-ollama',
    subtleVar: '--provider-ollama-subtle',
    borderVar: '--provider-ollama-subtle',
  },
  custom: {
    icon: Globe as IconComponent,
    colorVar: '--text-muted',
    subtleVar: '--alpha-white-5',
    borderVar: '--border-subtle',
  },
}

// ============================================================================
// Providers Tab Component
// ============================================================================

export const ProvidersTab: Component<ProvidersTabProps> = (props) => {
  const [expandedId, setExpandedId] = createSignal<string | null>(null)

  const connectedCount = () => props.providers.filter((p) => p.status === 'connected').length

  return (
    <div class="space-y-[var(--space-4)]">
      {/* Header - Minimal */}
      <div class="flex items-center justify-between mb-[var(--space-6)]">
        <div>
          <h3 class="text-[var(--text-base)] font-medium text-[var(--text-primary)]">Providers</h3>
          <p class="text-[var(--text-xs)] text-[var(--text-muted)] mt-[var(--space-0_5)]">
            {connectedCount() > 0 ? (
              <span class="text-[var(--success)]">{connectedCount()} connected</span>
            ) : (
              'Configure your AI providers'
            )}
          </p>
        </div>
      </div>

      {/* Provider Grid */}
      <div class="space-y-[var(--space-2)]">
        <For each={props.providers}>
          {(provider) => (
            <ProviderRow
              provider={provider}
              isExpanded={expandedId() === provider.id}
              onExpand={() => setExpandedId(expandedId() === provider.id ? null : provider.id)}
              onToggle={(enabled) => props.onToggle?.(provider.id, enabled)}
              onSaveApiKey={(key) => props.onSaveApiKey?.(provider.id, key)}
              onClearApiKey={() => props.onClearApiKey?.(provider.id)}
              onSetDefaultModel={(modelId) => props.onSetDefaultModel?.(provider.id, modelId)}
              onTestConnection={() => props.onTestConnection?.(provider.id)}
              onUpdateModels={(models) => props.onUpdateModels?.(provider.id, models)}
            />
          )}
        </For>
      </div>

      {/* Footer hint */}
      <p class="text-[var(--text-xs)] text-[var(--text-muted)] text-center pt-[var(--space-4)]">
        Keys stored locally · Never sent to Estela servers
      </p>
    </div>
  )
}

// ============================================================================
// Provider Row Component
// ============================================================================

interface ProviderRowProps {
  provider: LLMProviderConfig
  isExpanded: boolean
  onExpand: () => void
  onToggle?: (enabled: boolean) => void
  onSaveApiKey?: (key: string) => void
  onClearApiKey?: () => void
  onSetDefaultModel?: (modelId: string) => void
  onTestConnection?: () => void
  onUpdateModels?: (models: ProviderModel[]) => void
}

const ProviderRow: Component<ProviderRowProps> = (props) => {
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [apiKey, setApiKey] = createSignal(props.provider.apiKey ? '••••••••••••' : '')
  const [showKey, setShowKey] = createSignal(false)
  const [isLoadingModels, setIsLoadingModels] = createSignal(false)
  const [modelError, setModelError] = createSignal<string | null>(null)
  const [deviceCode, setDeviceCode] = createSignal<DeviceCodeResponse | null>(null)

  const config = () => providerConfig[props.provider.id] || providerConfig.custom

  const handleOAuthClick = async () => {
    const result = await startOAuthFlow(props.provider.id as LLMProvider)
    // Device code flow returns a DeviceCodeResponse
    if (result && 'userCode' in result) {
      setDeviceCode(result)
    }
  }

  const handleRefreshModels = async () => {
    setIsLoadingModels(true)
    setModelError(null)
    try {
      const key = apiKey().includes('••••') ? props.provider.apiKey : apiKey()
      const fetched = await fetchModels(props.provider.id as LLMProvider, {
        apiKey: key,
        baseUrl: props.provider.baseUrl,
      })
      if (fetched.length > 0) {
        const models: ProviderModel[] = fetched.map((m, idx) => ({
          id: m.id,
          name: m.name,
          contextWindow: m.contextWindow,
          isDefault: idx === 0,
        }))
        props.onUpdateModels?.(models)
      }
    } catch (err) {
      console.error('Failed to fetch models:', err)
      setModelError(err instanceof Error ? err.message : 'Failed to fetch models')
    } finally {
      setIsLoadingModels(false)
    }
  }

  const handleSaveKey = () => {
    if (apiKey() && !apiKey().includes('••••')) {
      props.onSaveApiKey?.(apiKey())
    }
  }

  return (
    <div
      class={`
        rounded-[var(--radius-xl)] border transition-colors duration-[var(--duration-fast)]
        ${
          props.isExpanded
            ? 'border-[var(--border-default)] bg-[var(--surface-raised)]'
            : 'border-transparent hover:border-[var(--border-subtle)] hover:bg-[var(--alpha-white-5)]'
        }
      `}
    >
      {/* Collapsed Row — div (not button) to avoid nested button with toggle */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button (toggle inside) which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        onClick={() => props.onExpand()}
        onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && props.onExpand()}
        class="w-full flex items-center gap-[var(--space-3)] p-[var(--space-3)] text-left cursor-pointer bg-transparent border-none"
      >
        {/* Icon with gradient background */}
        <div
          class="relative w-9 h-9 rounded-[var(--radius-lg)] flex items-center justify-center"
          style={{ background: `var(${config().subtleVar})` }}
        >
          <span style={{ color: `var(${config().colorVar})` }}>
            <Dynamic component={config().icon} class="w-4 h-4" />
          </span>
          {/* Status dot */}
          <div
            class={`
              absolute -bottom-[var(--space-0_5)] -right-[var(--space-0_5)] w-2.5 h-2.5 rounded-full border-2 border-[var(--surface)]
              ${
                props.provider.status === 'connected'
                  ? 'bg-[var(--success)]'
                  : props.provider.status === 'error'
                    ? 'bg-[var(--error)]'
                    : 'bg-[var(--text-muted)]'
              }
            `}
          />
        </div>

        {/* Info */}
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-[var(--space-2)]">
            <span class="text-[var(--text-sm)] font-medium text-[var(--text-primary)]">
              {props.provider.name}
            </span>
            <Show when={props.provider.status === 'connected'}>
              <span class="text-[var(--text-xs)] text-[var(--text-muted)]">
                {props.provider.defaultModel?.split('/').pop()?.split('-').slice(0, 2).join(' ')}
              </span>
            </Show>
          </div>
          <p class="text-[var(--text-xs)] text-[var(--text-muted)] truncate">
            {props.provider.description}
          </p>
        </div>

        {/* Right side controls */}
        {/* biome-ignore lint/a11y/noStaticElementInteractions: stopPropagation wrapper for nested controls */}
        {/* biome-ignore lint/a11y/useKeyWithClickEvents: mouse-only stopPropagation wrapper */}
        <div class="flex items-center gap-[var(--space-2)]" onClick={(e) => e.stopPropagation()}>
          {/* Quick toggle */}
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation()
              props.onToggle?.(!props.provider.enabled)
            }}
            class={`
              relative w-9 h-5 rounded-full transition-colors duration-[var(--duration-fast)]
              ${props.provider.enabled ? 'bg-[var(--accent)]' : 'bg-[var(--surface-sunken)]'}
            `}
          >
            <div
              class={`
                absolute top-[var(--space-0_5)] w-4 h-4 rounded-full bg-white shadow-sm
                transition-transform duration-[var(--duration-fast)]
                ${props.provider.enabled ? 'left-[18px]' : 'left-[var(--space-0_5)]'}
              `}
            />
          </button>

          {/* Expand chevron */}
          <ChevronRight
            class={`
              w-4 h-4 text-[var(--text-muted)] transition-transform duration-[var(--duration-fast)]
              ${props.isExpanded ? 'rotate-90' : ''}
            `}
          />
        </div>
      </div>

      {/* Expanded Content */}
      <Show when={props.isExpanded}>
        <div class="px-[var(--space-3)] pb-[var(--space-3)] space-y-[var(--space-3)] animate-slide-up">
          <div class="h-px bg-[var(--border-subtle)] mx-[var(--space-1)]" />

          {/* Device Code Dialog (Copilot) */}
          <Show when={deviceCode()}>
            <DeviceCodeDialog
              provider={props.provider.id as LLMProvider}
              deviceCode={deviceCode()!}
              onClose={() => setDeviceCode(null)}
              onSuccess={() => {
                setDeviceCode(null)
                props.onToggle?.(true)
              }}
            />
          </Show>

          {/* OAuth Section - Primary Action */}
          <Show when={isOAuthSupported(props.provider.id as LLMProvider)}>
            <button
              type="button"
              onClick={handleOAuthClick}
              class="
                w-full flex items-center gap-[var(--space-3)] p-[var(--space-3)] rounded-[var(--radius-lg)]
                border border-[var(--border-subtle)]
                hover:border-[var(--accent)] hover:shadow-sm
                transition-colors duration-[var(--duration-fast)] group
              "
              style={{ background: `var(${config().subtleVar})` }}
            >
              <div
                class="w-8 h-8 rounded-[var(--radius-md)] flex items-center justify-center"
                style={{ background: `var(${config().colorVar})` }}
              >
                <LogIn class="w-4 h-4 text-white" />
              </div>
              <div class="flex-1 text-left">
                <p class="text-[var(--text-sm)] font-medium text-[var(--text-primary)]">
                  {oauthButtonText(props.provider.id).label}
                </p>
                <p class="text-[var(--text-xs)] text-[var(--text-muted)]">
                  {oauthButtonText(props.provider.id).description}
                </p>
              </div>
              <ChevronRight class="w-4 h-4 text-[var(--text-muted)] group-hover:text-[var(--accent)] transition-colors duration-[var(--duration-fast)]" />
            </button>

            <div class="flex items-center gap-[var(--space-3)] px-[var(--space-2)]">
              <div class="flex-1 h-px bg-[var(--border-subtle)]" />
              <span class="text-[var(--text-xs)] text-[var(--text-muted)] uppercase tracking-wider">
                or API key
              </span>
              <div class="flex-1 h-px bg-[var(--border-subtle)]" />
            </div>
          </Show>

          {/* API Key Input */}
          <div class="space-y-[var(--space-2)]">
            <div class="relative">
              <input
                type={showKey() ? 'text' : 'password'}
                value={apiKey()}
                onInput={(e) => setApiKey(e.currentTarget.value)}
                onFocus={() => apiKey().includes('••••') && setApiKey('')}
                onBlur={handleSaveKey}
                placeholder={`${props.provider.name} API key`}
                class="
                  w-full h-[var(--space-10)] px-[var(--space-3)] pr-[80px]
                  bg-[var(--input-background)]
                  text-[var(--text-primary)] text-[var(--text-sm)] font-[var(--font-mono)]
                  placeholder:text-[var(--input-placeholder)]
                  border border-[var(--input-border)]
                  rounded-[var(--radius-lg)]
                  focus:outline-none focus:border-[var(--input-border-focus)] focus:shadow-[0_0_0_3px_var(--input-focus-ring)]
                  transition-colors duration-[var(--duration-fast)]
                "
              />
              <div class="absolute right-[var(--space-2)] top-1/2 -translate-y-1/2 flex items-center gap-[var(--space-1)]">
                <button
                  type="button"
                  onClick={() => setShowKey(!showKey())}
                  class="p-[var(--space-1_5)] text-[var(--text-muted)] hover:text-[var(--text-primary)] rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)]"
                >
                  <Show when={showKey()} fallback={<Eye class="w-3.5 h-3.5" />}>
                    <EyeOff class="w-3.5 h-3.5" />
                  </Show>
                </button>
                <Show when={props.provider.apiKey}>
                  <button
                    type="button"
                    onClick={() => props.onClearApiKey?.()}
                    class="p-[var(--space-1_5)] text-[var(--text-muted)] hover:text-[var(--error)] rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)]"
                    title="Clear API key"
                  >
                    <AlertCircle class="w-3.5 h-3.5" />
                  </button>
                </Show>
              </div>
            </div>
          </div>

          {/* Model Selector */}
          <Show when={props.provider.models.length > 0}>
            <div class="flex items-center gap-[var(--space-2)]">
              <select
                value={props.provider.defaultModel || ''}
                onChange={(e) => props.onSetDefaultModel?.(e.currentTarget.value)}
                class="
                  flex-1 h-9 px-[var(--space-3)]
                  bg-[var(--input-background)]
                  text-[var(--text-primary)] text-[var(--text-sm)]
                  border border-[var(--input-border)]
                  rounded-[var(--radius-lg)]
                  focus:outline-none focus:border-[var(--input-border-focus)]
                  transition-colors duration-[var(--duration-fast)]
                  appearance-none cursor-pointer
                "
                style={{
                  'background-image': `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%2352525b' stroke-width='2'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E")`,
                  'background-repeat': 'no-repeat',
                  'background-position': 'right 12px center',
                }}
              >
                <For each={props.provider.models}>
                  {(model) => (
                    <option value={model.id}>
                      {model.name} · {formatContextWindow(model.contextWindow)}
                    </option>
                  )}
                </For>
              </select>

              <Show when={supportsDynamicFetch(props.provider.id as LLMProvider)}>
                <button
                  type="button"
                  onClick={handleRefreshModels}
                  disabled={isLoadingModels()}
                  class="
                    h-9 px-[var(--space-3)] flex items-center gap-[var(--space-1_5)]
                    text-[var(--text-xs)] text-[var(--text-muted)]
                    bg-[var(--input-background)]
                    border border-[var(--input-border)]
                    rounded-[var(--radius-lg)]
                    hover:text-[var(--text-primary)] hover:border-[var(--border-default)]
                    disabled:opacity-50
                    transition-colors duration-[var(--duration-fast)]
                  "
                >
                  <Show
                    when={!isLoadingModels()}
                    fallback={<Loader2 class="w-3 h-3 animate-spin" />}
                  >
                    <RefreshCw class="w-3 h-3" />
                  </Show>
                  Sync
                </button>
              </Show>
            </div>
            <Show when={modelError()}>
              <p class="text-[var(--text-xs)] text-[var(--error)] px-[var(--space-1)]">
                {modelError()}
              </p>
            </Show>
          </Show>

          {/* Base URL for Ollama/Custom */}
          <Show when={props.provider.id === 'ollama' || props.provider.id === 'custom'}>
            <input
              type="url"
              value={props.provider.baseUrl || ''}
              placeholder="http://localhost:11434"
              class="
                w-full h-9 px-[var(--space-3)]
                bg-[var(--input-background)]
                text-[var(--text-primary)] text-[var(--text-sm)]
                placeholder:text-[var(--input-placeholder)]
                border border-[var(--input-border)]
                rounded-[var(--radius-lg)]
                focus:outline-none focus:border-[var(--input-border-focus)]
                transition-colors duration-[var(--duration-fast)]
              "
            />
          </Show>

          {/* Footer Actions */}
          <div class="flex items-center justify-between pt-[var(--space-1)]">
            <Show when={props.provider.apiKey}>
              <button
                type="button"
                onClick={() => props.onTestConnection?.()}
                class="
                  flex items-center gap-[var(--space-1_5)] px-[var(--space-2)] py-[var(--space-1)]
                  text-[var(--text-xs)] text-[var(--text-muted)]
                  hover:text-[var(--accent)]
                  transition-colors duration-[var(--duration-fast)]
                "
              >
                <CircleDot class="w-3 h-3" />
                Test
              </button>
            </Show>
            <div class="flex-1" />
            <a
              href={getProviderDocsUrl(props.provider.id)}
              target="_blank"
              rel="noopener noreferrer"
              class="
                flex items-center gap-[var(--space-1)] px-[var(--space-2)] py-[var(--space-1)]
                text-[var(--text-xs)] text-[var(--text-muted)]
                hover:text-[var(--accent)]
                transition-colors duration-[var(--duration-fast)]
              "
            >
              Docs
              <ExternalLink class="w-2.5 h-2.5" />
            </a>
          </div>

          {/* Error Display */}
          <Show when={props.provider.status === 'error' && props.provider.error}>
            <div class="flex items-center gap-2 px-[var(--space-3)] py-[var(--space-2)] bg-[var(--error-subtle)] border border-[var(--error-border)] rounded-[var(--radius-lg)]">
              <AlertCircle class="w-3.5 h-3.5 text-[var(--error)]" />
              <p class="text-[var(--text-xs)] text-[var(--error)]">{props.provider.error}</p>
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
  if (tokens >= 1000) return `${Math.round(tokens / 1000)}K`
  return tokens.toString()
}

const getProviderDocsUrl = (providerId: string): string => {
  const urls: Record<string, string> = {
    anthropic: 'https://docs.anthropic.com/en/api',
    openai: 'https://platform.openai.com/docs/api-reference',
    google: 'https://ai.google.dev/docs',
    copilot: 'https://docs.github.com/en/copilot',
    openrouter: 'https://openrouter.ai/docs',
    xai: 'https://docs.x.ai/api',
    mistral: 'https://docs.mistral.ai/api/',
    groq: 'https://console.groq.com/docs',
    deepseek: 'https://platform.deepseek.com/api-docs',
    cohere: 'https://docs.cohere.com/',
    together: 'https://docs.together.ai/',
    kimi: 'https://platform.moonshot.cn/docs',
    glm: 'https://open.bigmodel.cn/dev/api',
    ollama: 'https://ollama.ai/docs',
  }
  return urls[providerId] || '#'
}

const oauthButtonText = (providerId: string): { label: string; description: string } => {
  switch (providerId) {
    case 'anthropic':
      return { label: 'Sign in with Claude', description: 'Use your Max/Pro subscription' }
    case 'openai':
      return { label: 'Sign in with ChatGPT', description: 'Use your Plus/Pro subscription' }
    case 'google':
      return { label: 'Sign in with Google', description: 'Use your Gemini API access' }
    case 'copilot':
      return { label: 'Sign in with GitHub Copilot', description: 'Use your Copilot subscription' }
    default:
      return { label: `Sign in with ${providerId}`, description: 'OAuth authentication' }
  }
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
        id: 'claude-sonnet-4-5-20250514',
        name: 'Claude Sonnet 4.5',
        contextWindow: 200000,
        isDefault: true,
      },
      { id: 'claude-opus-4-5-20251124', name: 'Claude Opus 4.5', contextWindow: 200000 },
      { id: 'claude-haiku-4-5-20251022', name: 'Claude Haiku 4.5', contextWindow: 200000 },
      { id: 'claude-opus-4-1-20250801', name: 'Claude Opus 4.1', contextWindow: 200000 },
      { id: 'claude-opus-4-20250514', name: 'Claude Opus 4', contextWindow: 200000 },
      { id: 'claude-sonnet-4-20250514', name: 'Claude Sonnet 4', contextWindow: 200000 },
      { id: 'claude-3-7-sonnet-20250219', name: 'Claude 3.7 Sonnet', contextWindow: 200000 },
      { id: 'claude-3-5-sonnet-20241022', name: 'Claude 3.5 Sonnet v2', contextWindow: 200000 },
      { id: 'claude-3-5-haiku-20241022', name: 'Claude 3.5 Haiku', contextWindow: 200000 },
    ],
    defaultModel: 'claude-sonnet-4-5-20250514',
  },
  {
    id: 'openai',
    name: 'OpenAI',
    icon: Cpu as IconComponent,
    description: 'GPT and reasoning models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'gpt-5.2', name: 'GPT-5.2', contextWindow: 196000, isDefault: true },
      { id: 'gpt-5.2-mini', name: 'GPT-5.2 Mini', contextWindow: 128000 },
      { id: 'gpt-5.1-codex-max', name: 'GPT-5.1 Codex Max', contextWindow: 196000 },
      { id: 'gpt-4.1', name: 'GPT-4.1', contextWindow: 1000000 },
      { id: 'gpt-4.1-mini', name: 'GPT-4.1 Mini', contextWindow: 1000000 },
      { id: 'gpt-4.1-nano', name: 'GPT-4.1 Nano', contextWindow: 1000000 },
      { id: 'o3', name: 'o3', contextWindow: 200000 },
      { id: 'o3-pro', name: 'o3 Pro', contextWindow: 200000 },
      { id: 'o4-mini', name: 'o4 Mini', contextWindow: 200000 },
      { id: 'gpt-4o', name: 'GPT-4o', contextWindow: 128000 },
      { id: 'gpt-4o-mini', name: 'GPT-4o Mini', contextWindow: 128000 },
    ],
    defaultModel: 'gpt-5.2',
  },
  {
    id: 'google',
    name: 'Google',
    icon: Globe as IconComponent,
    description: 'Gemini models with large context',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'gemini-2.5-pro', name: 'Gemini 2.5 Pro', contextWindow: 1000000, isDefault: true },
      { id: 'gemini-2.5-flash', name: 'Gemini 2.5 Flash', contextWindow: 1000000 },
      { id: 'gemini-2.0-flash', name: 'Gemini 2.0 Flash', contextWindow: 1000000 },
      { id: 'gemini-1.5-pro', name: 'Gemini 1.5 Pro', contextWindow: 2000000 },
    ],
    defaultModel: 'gemini-2.5-pro',
  },
  {
    id: 'copilot',
    name: 'GitHub Copilot',
    icon: Monitor as IconComponent,
    description: 'Copilot subscription models via device code',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'copilot-gpt-4o', name: 'GPT-4o (Copilot)', contextWindow: 128000, isDefault: true },
      { id: 'copilot-claude-sonnet', name: 'Claude Sonnet (Copilot)', contextWindow: 200000 },
    ],
    defaultModel: 'copilot-gpt-4o',
  },
  {
    id: 'openrouter',
    name: 'OpenRouter',
    icon: Zap as IconComponent,
    description: 'Access 300+ models via single API',
    enabled: false,
    status: 'disconnected',
    models: [
      {
        id: 'anthropic/claude-sonnet-4.5',
        name: 'Claude Sonnet 4.5',
        contextWindow: 200000,
        isDefault: true,
      },
      { id: 'anthropic/claude-opus-4.5', name: 'Claude Opus 4.5', contextWindow: 200000 },
      { id: 'openai/gpt-5.2', name: 'GPT-5.2', contextWindow: 196000 },
      { id: 'openai/o3', name: 'o3', contextWindow: 200000 },
      { id: 'google/gemini-3-pro-preview', name: 'Gemini 3 Pro', contextWindow: 2000000 },
      { id: 'google/gemini-2.5-pro', name: 'Gemini 2.5 Pro', contextWindow: 1000000 },
      { id: 'deepseek/deepseek-r1', name: 'DeepSeek R1', contextWindow: 64000 },
      { id: 'meta-llama/llama-4-405b', name: 'Llama 4 405B', contextWindow: 256000 },
      { id: 'mistralai/codestral-2501', name: 'Codestral', contextWindow: 256000 },
      { id: 'x-ai/grok-3', name: 'Grok 3', contextWindow: 131072 },
    ],
    defaultModel: 'anthropic/claude-sonnet-4.5',
  },
  {
    id: 'xai',
    name: 'xAI',
    icon: Flame as IconComponent,
    description: 'Grok models for reasoning and code',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'grok-3', name: 'Grok 3', contextWindow: 131072, isDefault: true },
      { id: 'grok-3-mini', name: 'Grok 3 Mini', contextWindow: 131072 },
    ],
    defaultModel: 'grok-3',
  },
  {
    id: 'mistral',
    name: 'Mistral',
    icon: Cloud as IconComponent,
    description: 'European AI with code-specialized models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'mistral-large-latest', name: 'Mistral Large', contextWindow: 128000, isDefault: true },
      { id: 'codestral-latest', name: 'Codestral', contextWindow: 256000 },
      { id: 'mistral-small-latest', name: 'Mistral Small', contextWindow: 128000 },
    ],
    defaultModel: 'mistral-large-latest',
  },
  {
    id: 'groq',
    name: 'Groq',
    icon: Zap as IconComponent,
    description: 'Ultra-fast inference on open models',
    enabled: false,
    status: 'disconnected',
    models: [
      {
        id: 'llama-3.3-70b-versatile',
        name: 'Llama 3.3 70B',
        contextWindow: 128000,
        isDefault: true,
      },
      { id: 'mixtral-8x7b-32768', name: 'Mixtral 8x7B', contextWindow: 32768 },
      { id: 'gemma2-9b-it', name: 'Gemma 2 9B', contextWindow: 8192 },
    ],
    defaultModel: 'llama-3.3-70b-versatile',
  },
  {
    id: 'deepseek',
    name: 'DeepSeek',
    icon: Braces as IconComponent,
    description: 'Open-weight reasoning and coding models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'deepseek-chat', name: 'DeepSeek V3', contextWindow: 64000, isDefault: true },
      { id: 'deepseek-reasoner', name: 'DeepSeek R1', contextWindow: 64000 },
    ],
    defaultModel: 'deepseek-chat',
  },
  {
    id: 'cohere',
    name: 'Cohere',
    icon: Shield as IconComponent,
    description: 'Enterprise RAG and command models',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'command-r-plus', name: 'Command R+', contextWindow: 128000, isDefault: true },
      { id: 'command-r', name: 'Command R', contextWindow: 128000 },
    ],
    defaultModel: 'command-r-plus',
  },
  {
    id: 'together',
    name: 'Together',
    icon: Cloud as IconComponent,
    description: 'Open-source models with fast inference',
    enabled: false,
    status: 'disconnected',
    models: [
      {
        id: 'meta-llama/Llama-3.3-70B-Instruct-Turbo',
        name: 'Llama 3.3 70B',
        contextWindow: 128000,
        isDefault: true,
      },
      { id: 'mistralai/Mixtral-8x7B-Instruct-v0.1', name: 'Mixtral 8x7B', contextWindow: 32768 },
      { id: 'Qwen/Qwen2.5-Coder-32B-Instruct', name: 'Qwen 2.5 Coder 32B', contextWindow: 32768 },
    ],
    defaultModel: 'meta-llama/Llama-3.3-70B-Instruct-Turbo',
  },
  {
    id: 'kimi',
    name: 'Kimi',
    icon: Bot as IconComponent,
    description: 'Moonshot AI models with long context',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'moonshot-v1-128k', name: 'Kimi v1 128K', contextWindow: 128000, isDefault: true },
    ],
    defaultModel: 'moonshot-v1-128k',
  },
  {
    id: 'glm',
    name: 'Zhipu (GLM)',
    icon: Cpu as IconComponent,
    description: 'Chinese AI with bilingual capabilities',
    enabled: false,
    status: 'disconnected',
    models: [
      { id: 'glm-4-plus', name: 'GLM-4 Plus', contextWindow: 128000, isDefault: true },
      { id: 'glm-4-flash', name: 'GLM-4 Flash', contextWindow: 128000 },
    ],
    defaultModel: 'glm-4-plus',
  },
  {
    id: 'ollama',
    name: 'Ollama',
    icon: Bot as IconComponent,
    description: 'Run models locally',
    enabled: false,
    status: 'disconnected',
    baseUrl: 'http://localhost:11434',
    models: [
      { id: 'llama3.3:latest', name: 'Llama 3.3', contextWindow: 128000 },
      { id: 'deepseek-r1:latest', name: 'DeepSeek R1', contextWindow: 64000 },
      { id: 'qwen2.5-coder:latest', name: 'Qwen 2.5 Coder', contextWindow: 32000 },
      { id: 'codestral:latest', name: 'Codestral', contextWindow: 32000 },
      { id: 'mistral:latest', name: 'Mistral', contextWindow: 32000 },
      { id: 'phi4:latest', name: 'Phi-4', contextWindow: 16000 },
    ],
  },
]
