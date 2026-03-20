/**
 * Provider Card Expanded
 *
 * Expanded configuration: OAuth, API key, base URL, test, docs, model count.
 * Port of the logic from provider-row-expanded.tsx into the card layout.
 */

import { ExternalLink, Server, Trash2 } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
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
import { OllamaModelBrowser } from '../../OllamaModelBrowser'
import { ProviderRowApiKeyInput } from '../provider-row-api-key-input'
import { ProviderRowClearConfirm } from '../provider-row-clear-confirm'
import { checkStoredOAuth, clearProviderCredentials } from '../providers-tab-helpers'
import { getProviderDocsUrl, oauthButtonText } from '../providers-tab-utils'
import { ModelsListSection } from './ModelsListSection'
import { OAuthSection } from './OAuthSection'

interface ProviderCardExpandedProps {
  provider: LLMProviderConfig
  onSaveApiKey?: (key: string) => void
  onClearApiKey?: () => void
  onSetDefaultModel?: (modelId: string) => void
  onTestConnection?: () => void
  onUpdateModels?: (models: ProviderModel[]) => void
  onSaveBaseUrl?: (url: string) => void
}

export const ProviderCardExpanded: Component<ProviderCardExpandedProps> = (props) => {
  const [apiKey, setApiKey] = createSignal(props.provider.apiKey ? '••••••••••••' : '')
  const [showKey, setShowKey] = createSignal(false)
  const [baseUrl, setBaseUrl] = createSignal(props.provider.baseUrl || '')
  const [isLoadingModels, setIsLoadingModels] = createSignal(false)
  const [isOAuthLoading, setIsOAuthLoading] = createSignal(false)
  const [isOAuthConnected, setIsOAuthConnected] = createSignal(checkStoredOAuth(props.provider.id))
  const [modelError, setModelError] = createSignal<string | null>(null)
  const [oauthError, setOauthError] = createSignal<string | null>(null)
  const [deviceCode, setDeviceCode] = createSignal<DeviceCodeResponse | null>(null)
  const [showClearConfirm, setShowClearConfirm] = createSignal(false)
  const [showOllamaBrowser, setShowOllamaBrowser] = createSignal(false)

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
        <OAuthSection
          isConnected={isOAuthConnected()}
          isLoading={isOAuthLoading()}
          error={oauthError()}
          buttonLabel={oauthButtonText(props.provider.id).label}
          onConnect={handleOAuthClick}
          onDisconnect={() => setShowClearConfirm(true)}
        />
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
          value={baseUrl()}
          onInput={(e) => setBaseUrl(e.currentTarget.value)}
          onBlur={() => props.onSaveBaseUrl?.(baseUrl())}
          placeholder="http://localhost:11434"
          class="w-full px-3 py-2 bg-[var(--input-background)] text-xs text-[var(--text-primary)] placeholder:text-[var(--input-placeholder)] border border-[var(--input-border)] rounded-[var(--radius-md)] focus:outline-none focus:border-[var(--input-border-focus)] transition-colors"
        />
      </Show>

      {/* Ollama model browser */}
      <Show when={props.provider.id === 'ollama'}>
        <button
          type="button"
          onClick={() => setShowOllamaBrowser(true)}
          class="flex items-center gap-2 w-full px-3 py-2 text-[11px] text-[var(--text-secondary)] hover:text-[var(--accent)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
        >
          <Server class="w-3 h-3" />
          Manage Local Models
        </button>
        <OllamaModelBrowser
          open={showOllamaBrowser()}
          onClose={() => setShowOllamaBrowser(false)}
          baseUrl={props.provider.baseUrl}
        />
      </Show>

      {/* Models list */}
      <Show when={props.provider.models.length > 0}>
        <ModelsListSection
          models={props.provider.models}
          defaultModel={props.provider.defaultModel}
          isLoading={isLoadingModels()}
          error={modelError()}
          onRefresh={handleRefreshModels}
        />
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
