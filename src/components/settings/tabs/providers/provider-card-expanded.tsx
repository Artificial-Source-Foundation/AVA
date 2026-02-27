/**
 * Provider Card Expanded
 *
 * Expanded configuration: OAuth, API key, base URL, test, docs, model count.
 * Port of the logic from provider-row-expanded.tsx into the card layout.
 */

import { ExternalLink, Loader2, LogIn, LogOut, RefreshCw, Trash2 } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type {
  LLMProviderConfig,
  ProviderModel,
} from '../../../../config/defaults/provider-defaults'
import { removeStoredAuth } from '../../../../lib/auth-helpers'
import {
  type DeviceCodeResponse,
  isOAuthSupported,
  startOAuthFlow,
} from '../../../../services/auth/oauth'
import { logError } from '../../../../services/logger'
import { fetchModels } from '../../../../services/providers/model-fetcher'
import type { LLMProvider } from '../../../../types/llm'
import { DeviceCodeDialog } from '../../DeviceCodeDialog'
import { ProviderRowApiKeyInput } from '../provider-row-api-key-input'
import { ProviderRowClearConfirm } from '../provider-row-clear-confirm'
import { checkStoredOAuth, clearProviderCredentials } from '../providers-tab-helpers'
import { formatContextWindow, getProviderDocsUrl, oauthButtonText } from '../providers-tab-utils'

interface ProviderCardExpandedProps {
  provider: LLMProviderConfig
  onSaveApiKey?: (key: string) => void
  onClearApiKey?: () => void
  onSetDefaultModel?: (modelId: string) => void
  onTestConnection?: () => void
  onUpdateModels?: (models: ProviderModel[]) => void
}

export const ProviderCardExpanded: Component<ProviderCardExpandedProps> = (props) => {
  const [apiKey, setApiKey] = createSignal(props.provider.apiKey ? '••••••••••••' : '')
  const [showKey, setShowKey] = createSignal(false)
  const [isLoadingModels, setIsLoadingModels] = createSignal(false)
  const [isOAuthLoading, setIsOAuthLoading] = createSignal(false)
  const [isOAuthConnected, setIsOAuthConnected] = createSignal(checkStoredOAuth(props.provider.id))
  const [modelError, setModelError] = createSignal<string | null>(null)
  const [oauthError, setOauthError] = createSignal<string | null>(null)
  const [deviceCode, setDeviceCode] = createSignal<DeviceCodeResponse | null>(null)
  const [showClearConfirm, setShowClearConfirm] = createSignal(false)

  const hasAnyCredentials = () => !!props.provider.apiKey || isOAuthConnected()

  const handleOAuthClick = async () => {
    setOauthError(null)
    setIsOAuthLoading(true)
    try {
      const result = await startOAuthFlow(props.provider.id as LLMProvider)
      if ('userCode' in result) {
        setDeviceCode(result as DeviceCodeResponse)
      } else {
        setIsOAuthConnected(true)
      }
    } catch (err) {
      logError('providers', 'OAuth flow failed', err)
      setOauthError(err instanceof Error ? err.message : 'OAuth flow failed')
    } finally {
      setIsOAuthLoading(false)
    }
  }

  const handleClearAll = async () => {
    try {
      await removeStoredAuth(props.provider.id as LLMProvider).catch(() => {})
      clearProviderCredentials(props.provider.id)
      setIsOAuthConnected(false)
      setApiKey('')
      setShowClearConfirm(false)
      props.onClearApiKey?.()
    } catch (err) {
      logError('providers', 'Clear credentials failed', err)
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
      logError('providers', 'Failed to fetch models', err)
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
    <div class="px-3 pb-3 space-y-3 border-t border-[var(--border-subtle)]">
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

      {/* OAuth section */}
      <Show when={isOAuthSupported(props.provider.id as LLMProvider)}>
        <div class="pt-3">
          <Show
            when={isOAuthConnected()}
            fallback={
              <button
                type="button"
                onClick={handleOAuthClick}
                disabled={isOAuthLoading()}
                class="flex items-center gap-2 px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] hover:text-[var(--accent)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors w-full disabled:opacity-50"
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
            }
          >
            <div class="flex items-center gap-2">
              <div class="flex items-center gap-1.5 flex-1 px-2.5 py-1.5 text-[11px] text-[var(--success)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]">
                <span class="w-1.5 h-1.5 rounded-full bg-[var(--success)]" />
                <span>Connected via OAuth</span>
              </div>
              <button
                type="button"
                onClick={() => setShowClearConfirm(true)}
                class="px-2 py-1.5 text-[11px] text-[var(--text-muted)] hover:text-[var(--error)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
                title="Disconnect OAuth"
              >
                <LogOut class="w-3 h-3" />
              </button>
            </div>
          </Show>
          <Show when={oauthError()}>
            <p class="text-[10px] text-[var(--error)] px-1 mt-1">{oauthError()}</p>
          </Show>
          <div class="flex items-center gap-2 px-1 mt-2">
            <div class="flex-1 h-px bg-[var(--border-subtle)]" />
            <span class="text-[9px] text-[var(--text-muted)] uppercase">or API key</span>
            <div class="flex-1 h-px bg-[var(--border-subtle)]" />
          </div>
        </div>
      </Show>

      {/* API Key input */}
      <div class="pt-2">
        <ProviderRowApiKeyInput
          providerName={props.provider.name}
          hasStoredApiKey={!!props.provider.apiKey}
          apiKey={apiKey}
          showKey={showKey}
          onInput={setApiKey}
          onToggleVisibility={() => setShowKey(!showKey())}
          onClearClick={() => setShowClearConfirm(true)}
          onBlur={handleSaveKey}
        />
      </div>

      {/* Base URL for local providers */}
      <Show when={props.provider.id === 'ollama' || props.provider.id === 'custom'}>
        <input
          type="url"
          value={props.provider.baseUrl || ''}
          placeholder="http://localhost:11434"
          class="w-full px-3 py-2 bg-[var(--input-background)] text-xs text-[var(--text-primary)] placeholder:text-[var(--input-placeholder)] border border-[var(--input-border)] rounded-[var(--radius-md)] focus:outline-none focus:border-[var(--input-border-focus)] transition-colors"
        />
      </Show>

      {/* Models list */}
      <Show when={props.provider.models.length > 0}>
        <div class="space-y-1">
          <div class="flex items-center justify-between">
            <span class="text-[10px] font-medium text-[var(--text-muted)]">
              {props.provider.models.length} models
            </span>
            <button
              type="button"
              onClick={handleRefreshModels}
              disabled={isLoadingModels()}
              class="flex items-center gap-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] disabled:opacity-50 transition-colors"
              title="Refresh models from API"
            >
              <Show when={isLoadingModels()} fallback={<RefreshCw class="w-2.5 h-2.5" />}>
                <Loader2 class="w-2.5 h-2.5 animate-spin" />
              </Show>
              Refresh
            </button>
          </div>
          <div class="flex flex-wrap gap-1">
            <For each={props.provider.models.slice(0, 6)}>
              {(model) => (
                <span
                  class={`px-1.5 py-0.5 text-[9px] rounded-[var(--radius-sm)] border cursor-default ${
                    model.id === props.provider.defaultModel
                      ? 'border-[var(--accent)] text-[var(--accent)] bg-[var(--accent)]/5'
                      : 'border-[var(--border-subtle)] text-[var(--text-muted)]'
                  }`}
                  title={`${model.name} · ${formatContextWindow(model.contextWindow)}`}
                >
                  {model.name}
                </span>
              )}
            </For>
            <Show when={props.provider.models.length > 6}>
              <span class="px-1.5 py-0.5 text-[9px] text-[var(--text-muted)]">
                +{props.provider.models.length - 6} more
              </span>
            </Show>
          </div>
          <Show when={modelError()}>
            <p class="text-[10px] text-[var(--error)] px-1">{modelError()}</p>
          </Show>
        </div>
      </Show>

      {/* Clear confirm */}
      <Show when={showClearConfirm()}>
        <ProviderRowClearConfirm
          providerName={props.provider.name}
          hasOAuth={isOAuthConnected()}
          hasApiKey={!!props.provider.apiKey}
          onConfirm={handleClearAll}
          onCancel={() => setShowClearConfirm(false)}
        />
      </Show>

      {/* Footer actions */}
      <div class="flex items-center justify-between pt-1">
        <div class="flex items-center gap-3">
          <Show when={props.provider.apiKey}>
            <button
              type="button"
              onClick={() => props.onTestConnection?.()}
              class="text-[10px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
            >
              Test connection
            </button>
          </Show>
          <Show when={hasAnyCredentials() && !showClearConfirm()}>
            <button
              type="button"
              onClick={() => setShowClearConfirm(true)}
              class="flex items-center gap-1 text-[10px] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
              title="Clear all credentials"
            >
              <Trash2 class="w-2.5 h-2.5" />
              Clear
            </button>
          </Show>
        </div>
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

      <Show when={props.provider.status === 'error' && props.provider.error}>
        <p class="text-[10px] text-[var(--error)] px-1">{props.provider.error}</p>
      </Show>
    </div>
  )
}
