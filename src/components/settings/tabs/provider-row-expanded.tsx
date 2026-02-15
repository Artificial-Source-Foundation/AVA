import { removeStoredAuth } from '@ava/core'
import { ExternalLink, Loader2, LogIn, LogOut, Trash2 } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import type { ProviderModel } from '../../../config/defaults/provider-defaults'
import {
  type DeviceCodeResponse,
  isOAuthSupported,
  type OAuthTokens,
  startOAuthFlow,
} from '../../../services/auth/oauth'
import { logError } from '../../../services/logger'
import { fetchModels } from '../../../services/providers/model-fetcher'
import type { LLMProvider } from '../../../types/llm'
import { DeviceCodeDialog } from '../DeviceCodeDialog'
import { ProviderRowApiKeyInput } from './provider-row-api-key-input'
import { ProviderRowClearConfirm } from './provider-row-clear-confirm'
import { ProviderRowModelSelector } from './provider-row-model-selector'
import { checkStoredOAuth, clearProviderCredentials } from './providers-tab-helpers'
import type { ProviderRowProps } from './providers-tab-types'
import { getProviderDocsUrl, oauthButtonText } from './providers-tab-utils'

export const ProviderRowExpanded: Component<ProviderRowProps> = (props) => {
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
        const tokens = result as OAuthTokens
        if (props.provider.id === 'anthropic') props.onSaveApiKey?.(tokens.accessToken)
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
    if (apiKey() && !apiKey().includes('••••')) props.onSaveApiKey?.(apiKey())
  }

  return (
    <div class="pl-2 pb-3 space-y-3 border-l border-[var(--border-subtle)] ml-1 mb-2">
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

      <Show when={isOAuthSupported(props.provider.id as LLMProvider)}>
        <Show
          when={isOAuthConnected()}
          fallback={
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
          }
        >
          <div class="flex items-center gap-2">
            <div class="flex items-center gap-1.5 flex-1 px-2.5 py-1.5 text-[11px] text-[var(--success)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]">
              <span class="w-1.5 h-1.5 rounded-full bg-[var(--success)]" />
              <span>Connected via OAuth</span>
            </div>
            <button
              type="button"
              onClick={() => setShowClearConfirm(true)}
              class="px-2 py-1.5 text-[11px] text-[var(--text-muted)] hover:text-[var(--error)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
              title="Disconnect OAuth"
            >
              <LogOut class="w-3 h-3" />
            </button>
          </div>
        </Show>
        <Show when={oauthError()}>
          <p class="text-[10px] text-[var(--error)] px-1">{oauthError()}</p>
        </Show>
        <div class="flex items-center gap-2 px-1">
          <div class="flex-1 h-px bg-[var(--border-subtle)]" />
          <span class="text-[9px] text-[var(--text-muted)] uppercase">or API key</span>
          <div class="flex-1 h-px bg-[var(--border-subtle)]" />
        </div>
      </Show>

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

      <ProviderRowModelSelector
        providerId={props.provider.id}
        models={props.provider.models}
        defaultModel={props.provider.defaultModel}
        modelError={modelError}
        isLoadingModels={isLoadingModels}
        onSetDefaultModel={(modelId) => props.onSetDefaultModel?.(modelId)}
        onRefreshModels={handleRefreshModels}
      />

      <Show when={props.provider.id === 'ollama' || props.provider.id === 'custom'}>
        <input
          type="url"
          value={props.provider.baseUrl || ''}
          placeholder="http://localhost:11434"
          class="w-full px-3 py-2 bg-[var(--input-background)] text-xs text-[var(--text-primary)] placeholder:text-[var(--input-placeholder)] border border-[var(--input-border)] rounded-[var(--radius-md)] focus:outline-none focus:border-[var(--input-border-focus)] transition-colors"
        />
      </Show>

      <Show when={showClearConfirm()}>
        <ProviderRowClearConfirm
          providerName={props.provider.name}
          hasOAuth={isOAuthConnected()}
          hasApiKey={!!props.provider.apiKey}
          onConfirm={handleClearAll}
          onCancel={() => setShowClearConfirm(false)}
        />
      </Show>

      <div class="flex items-center justify-between">
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
              title="Clear all credentials for this provider"
            >
              <Trash2 class="w-2.5 h-2.5" />
              Clear credentials
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
