/**
 * Providers Settings Tab
 *
 * Flat, minimal design matching GeneralSection.
 * Expand a provider to configure API key, model, etc.
 */

import {
  AlertCircle,
  Bot,
  Braces,
  ChevronRight,
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
import {
  type DeviceCodeResponse,
  isOAuthSupported,
  type OAuthTokens,
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
// Providers Tab Component
// ============================================================================

export const ProvidersTab: Component<ProvidersTabProps> = (props) => {
  const [expandedId, setExpandedId] = createSignal<string | null>(null)

  const connectedCount = () => props.providers.filter((p) => p.status === 'connected').length

  return (
    <div class="space-y-4">
      <p class="text-[10px] text-[var(--text-muted)]">
        {connectedCount() > 0 ? (
          <span class="text-[var(--success)]">{connectedCount()} connected</span>
        ) : (
          'Configure your AI providers'
        )}
      </p>

      <div class="space-y-0.5">
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

      <p class="text-[10px] text-[var(--text-muted)] text-center">
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
  const [isOAuthLoading, setIsOAuthLoading] = createSignal(false)
  const [modelError, setModelError] = createSignal<string | null>(null)
  const [oauthError, setOauthError] = createSignal<string | null>(null)
  const [deviceCode, setDeviceCode] = createSignal<DeviceCodeResponse | null>(null)

  const statusColor = () => {
    if (props.provider.status === 'connected') return 'var(--success)'
    if (props.provider.status === 'error') return 'var(--error)'
    return 'var(--text-muted)'
  }

  const handleOAuthClick = async () => {
    setOauthError(null)
    setIsOAuthLoading(true)
    try {
      const result = await startOAuthFlow(props.provider.id as LLMProvider)
      if ('userCode' in result) {
        // Device code flow — show dialog for user to enter code
        setDeviceCode(result as DeviceCodeResponse)
      } else {
        // PKCE flow completed — tokens returned, already stored + synced
        const tokens = result as OAuthTokens
        props.onSaveApiKey?.(tokens.accessToken)
      }
    } catch (err) {
      console.error('OAuth flow failed:', err)
      setOauthError(err instanceof Error ? err.message : 'OAuth flow failed')
    } finally {
      setIsOAuthLoading(false)
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
    <div>
      {/* Collapsed row */}
      {/* biome-ignore lint/a11y/useSemanticElements: div+role=button avoids nested button which crashes WebKitGTK */}
      <div
        role="button"
        tabIndex={0}
        onClick={() => props.onExpand()}
        onKeyDown={(e) => (e.key === 'Enter' || e.key === ' ') && props.onExpand()}
        class="flex items-center justify-between py-2 cursor-pointer group"
      >
        <div class="flex-1 min-w-0">
          <div class="flex items-center gap-1.5">
            <span class="text-xs text-[var(--text-secondary)]">{props.provider.name}</span>
            <span class="w-1.5 h-1.5 rounded-full" style={{ background: statusColor() }} />
          </div>
          <p class="text-[10px] text-[var(--text-muted)] truncate">{props.provider.description}</p>
        </div>
        <div class="flex items-center gap-1.5">
          {/* biome-ignore lint/a11y/noStaticElementInteractions: stopPropagation wrapper */}
          {/* biome-ignore lint/a11y/useKeyWithClickEvents: mouse-only wrapper */}
          <div class="flex items-center gap-1.5" onClick={(e) => e.stopPropagation()}>
            <button
              type="button"
              onClick={(e) => {
                e.stopPropagation()
                props.onToggle?.(!props.provider.enabled)
              }}
              class={`
                w-9 h-5 rounded-full transition-colors flex-shrink-0 flex items-center
                ${props.provider.enabled ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'}
              `}
            >
              <span
                class={`
                  w-4 h-4 rounded-full bg-white shadow-sm transition-transform duration-150
                  ${props.provider.enabled ? 'translate-x-[18px]' : 'translate-x-[2px]'}
                `}
              />
            </button>
          </div>
          <ChevronRight
            class={`w-3.5 h-3.5 text-[var(--text-muted)] transition-transform duration-150 ${props.isExpanded ? 'rotate-90' : ''}`}
          />
        </div>
      </div>

      {/* Expanded detail */}
      <Show when={props.isExpanded}>
        <div class="pl-2 pb-3 space-y-3 border-l border-[var(--border-subtle)] ml-1 mb-2">
          {/* Device Code Dialog */}
          <Show when={deviceCode()}>
            <DeviceCodeDialog
              provider={props.provider.id as LLMProvider}
              deviceCode={deviceCode()!}
              onClose={() => setDeviceCode(null)}
              onSuccess={(accessToken) => {
                setDeviceCode(null)
                props.onSaveApiKey?.(accessToken)
              }}
            />
          </Show>

          {/* OAuth button */}
          <Show when={isOAuthSupported(props.provider.id as LLMProvider)}>
            <button
              type="button"
              onClick={handleOAuthClick}
              disabled={isOAuthLoading()}
              class="flex items-center gap-2 px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] hover:text-[var(--accent)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors w-full disabled:opacity-50"
            >
              <Show when={isOAuthLoading()} fallback={<LogIn class="w-3 h-3" />}>
                <Loader2 class="w-3 h-3 animate-spin" />
              </Show>
              <span>
                {isOAuthLoading()
                  ? 'Waiting for authorization...'
                  : oauthButtonText(props.provider.id).label}
              </span>
            </button>
            <Show when={oauthError()}>
              <p class="text-[10px] text-[var(--error)] px-1">{oauthError()}</p>
            </Show>
            <div class="flex items-center gap-2 px-1">
              <div class="flex-1 h-px bg-[var(--border-subtle)]" />
              <span class="text-[9px] text-[var(--text-muted)] uppercase">or API key</span>
              <div class="flex-1 h-px bg-[var(--border-subtle)]" />
            </div>
          </Show>

          {/* API Key */}
          <div class="relative">
            <input
              type={showKey() ? 'text' : 'password'}
              value={apiKey()}
              onInput={(e) => setApiKey(e.currentTarget.value)}
              onFocus={() => apiKey().includes('••••') && setApiKey('')}
              onBlur={handleSaveKey}
              placeholder={`${props.provider.name} API key`}
              class="
                w-full px-3 py-2 pr-16
                bg-[var(--input-background)]
                text-xs text-[var(--text-primary)] font-mono
                placeholder:text-[var(--input-placeholder)]
                border border-[var(--input-border)]
                rounded-[var(--radius-md)]
                focus:outline-none focus:border-[var(--input-border-focus)]
                transition-colors
              "
            />
            <div class="absolute right-2 top-1/2 -translate-y-1/2 flex items-center gap-1">
              <button
                type="button"
                onClick={() => setShowKey(!showKey())}
                class="p-1 text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
              >
                <Show when={showKey()} fallback={<Eye class="w-3 h-3" />}>
                  <EyeOff class="w-3 h-3" />
                </Show>
              </button>
              <Show when={props.provider.apiKey}>
                <button
                  type="button"
                  onClick={() => props.onClearApiKey?.()}
                  class="p-1 text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
                  title="Clear API key"
                >
                  <AlertCircle class="w-3 h-3" />
                </button>
              </Show>
            </div>
          </div>

          {/* Model Selector */}
          <Show when={props.provider.models.length > 0}>
            <div class="flex items-center gap-2">
              <select
                value={props.provider.defaultModel || ''}
                onChange={(e) => props.onSetDefaultModel?.(e.currentTarget.value)}
                class="
                  flex-1 px-3 py-2
                  bg-[var(--input-background)]
                  text-xs text-[var(--text-primary)]
                  border border-[var(--input-border)]
                  rounded-[var(--radius-md)]
                  focus:outline-none focus:border-[var(--input-border-focus)]
                  transition-colors appearance-none cursor-pointer
                "
                style={{
                  'background-image': `url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%2352525b' stroke-width='2'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E")`,
                  'background-repeat': 'no-repeat',
                  'background-position': 'right 10px center',
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
                  class="px-2 py-2 text-[var(--text-muted)] hover:text-[var(--text-primary)] disabled:opacity-50 transition-colors"
                >
                  <Show
                    when={!isLoadingModels()}
                    fallback={<Loader2 class="w-3 h-3 animate-spin" />}
                  >
                    <RefreshCw class="w-3 h-3" />
                  </Show>
                </button>
              </Show>
            </div>
            <Show when={modelError()}>
              <p class="text-[10px] text-[var(--error)] px-1">{modelError()}</p>
            </Show>
          </Show>

          {/* Base URL for Ollama/Custom */}
          <Show when={props.provider.id === 'ollama' || props.provider.id === 'custom'}>
            <input
              type="url"
              value={props.provider.baseUrl || ''}
              placeholder="http://localhost:11434"
              class="
                w-full px-3 py-2
                bg-[var(--input-background)]
                text-xs text-[var(--text-primary)]
                placeholder:text-[var(--input-placeholder)]
                border border-[var(--input-border)]
                rounded-[var(--radius-md)]
                focus:outline-none focus:border-[var(--input-border-focus)]
                transition-colors
              "
            />
          </Show>

          {/* Footer links */}
          <div class="flex items-center justify-between">
            <Show when={props.provider.apiKey}>
              <button
                type="button"
                onClick={() => props.onTestConnection?.()}
                class="text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
              >
                Test connection
              </button>
            </Show>
            <div class="flex-1" />
            <a
              href={getProviderDocsUrl(props.provider.id)}
              target="_blank"
              rel="noopener noreferrer"
              class="flex items-center gap-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            >
              Docs
              <ExternalLink class="w-2.5 h-2.5" />
            </a>
          </div>

          {/* Error */}
          <Show when={props.provider.status === 'error' && props.provider.error}>
            <p class="text-[10px] text-[var(--error)] px-1">{props.provider.error}</p>
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
