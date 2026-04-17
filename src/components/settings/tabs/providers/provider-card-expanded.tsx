/**
 * Provider Card Expanded
 *
 * Expanded configuration with auth, models, base URL, and test controls.
 */

import { Loader2, Server, TestTube2, Trash2 } from 'lucide-solid'
import { type Component, createEffect, createSignal, Show } from 'solid-js'
import type { LLMProviderConfig } from '../../../../config/defaults/provider-defaults'
import { removeStoredAuth } from '../../../../lib/auth-helpers'
import {
  type DeviceCodeResponse,
  isOAuthSupported,
  startOAuthFlow,
} from '../../../../services/auth/oauth'
import { logError } from '../../../../services/logger'
import type { LLMProvider } from '../../../../types/llm'
import { DeviceCodeDialog } from '../../DeviceCodeDialog'
import { OllamaModelBrowser } from '../../OllamaModelBrowser'
import { SettingsInput } from '../../shared-settings-components'
import { ProviderRowApiKeyInput } from '../provider-row-api-key-input'
import { ProviderRowClearConfirm } from '../provider-row-clear-confirm'
import { checkStoredOAuth, clearProviderCredentials } from '../providers-tab-helpers'
import { oauthButtonText } from '../providers-tab-utils'
import { ModelsListSection } from './ModelsListSection'
import { OAuthSection } from './OAuthSection'

interface ProviderCardExpandedProps {
  provider: LLMProviderConfig
  onSaveApiKey?: (key: string) => void
  onClearApiKey?: () => void
  onOAuthConnected?: () => void
  onTestConnection?: () => void
  onUpdateModels?: (models: LLMProviderConfig['models']) => void
  onSaveBaseUrl?: (url: string) => void
  onSetDefaultModel?: (modelId: string) => void
}

export const ProviderCardExpanded: Component<ProviderCardExpandedProps> = (props) => {
  const [apiKey, setApiKey] = createSignal('')
  const [showKey, setShowKey] = createSignal(false)
  const [isOAuthLoading, setIsOAuthLoading] = createSignal(false)
  const [isOAuthConnected, setIsOAuthConnected] = createSignal(false)
  const [oauthError, setOauthError] = createSignal<string | null>(null)
  const [deviceCode, setDeviceCode] = createSignal<DeviceCodeResponse | null>(null)
  const [showClearConfirm, setShowClearConfirm] = createSignal(false)
  const [showOllamaBrowser, setShowOllamaBrowser] = createSignal(false)

  createEffect(() => {
    setApiKey(props.provider.apiKey ? '••••••••••••' : '')
    setIsOAuthConnected(checkStoredOAuth(props.provider.id))
  })

  const hasAnyCredentials = () => !!props.provider.apiKey || isOAuthConnected()

  const handleOAuthClick = async () => {
    setOauthError(null)
    setIsOAuthLoading(true)
    try {
      const result = await startOAuthFlow(props.provider.id as LLMProvider)
      if (result.kind === 'pending') {
        setDeviceCode(result.deviceCode as DeviceCodeResponse)
      } else {
        setIsOAuthConnected(true)
        props.onOAuthConnected?.()
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
      await removeStoredAuth(props.provider.id as LLMProvider)
      clearProviderCredentials(props.provider.id)
      setIsOAuthConnected(false)
      setApiKey('')
      setShowClearConfirm(false)
      setOauthError(null)
      props.onClearApiKey?.()
    } catch (err) {
      logError('providers', 'Clear credentials failed', err)
      setOauthError(err instanceof Error ? err.message : 'Failed to clear stored credentials')
    }
  }

  const handleSaveKey = () => {
    if (apiKey() && !apiKey().includes('••••')) {
      props.onSaveApiKey?.(apiKey())
    }
  }

  const [baseUrl, setBaseUrl] = createSignal(props.provider.baseUrl || '')
  const [isTesting, setIsTesting] = createSignal(false)
  const [isRefreshingModels, setIsRefreshingModels] = createSignal(false)
  const [modelsError, setModelsError] = createSignal<string | null>(null)

  createEffect(() => {
    setBaseUrl(props.provider.baseUrl || '')
  })

  const handleSaveBaseUrl = () => {
    if (baseUrl() !== (props.provider.baseUrl || '')) {
      props.onSaveBaseUrl?.(baseUrl())
    }
  }

  const handleTest = async () => {
    setIsTesting(true)
    try {
      await props.onTestConnection?.()
    } finally {
      setIsTesting(false)
    }
  }

  const handleRefreshModels = async () => {
    setIsRefreshingModels(true)
    setModelsError(null)
    try {
      // Models refresh happens via onTestConnection which updates provider state
      await props.onTestConnection?.()
    } catch (err) {
      setModelsError(err instanceof Error ? err.message : 'Failed to refresh models')
    } finally {
      setIsRefreshingModels(false)
    }
  }

  return (
    <div class="rounded-[10px] border border-[var(--border-subtle)] bg-[var(--surface-sunken)]/40 px-3 py-3 space-y-3">
      <Show when={deviceCode()}>
        <DeviceCodeDialog
          provider={props.provider.id as LLMProvider}
          deviceCode={deviceCode()!}
          onClose={() => setDeviceCode(null)}
          onSuccess={() => {
            setIsOAuthConnected(true)
            props.onOAuthConnected?.()
            setDeviceCode(null)
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
      <div>
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

      {/* Base URL input (for providers that support it) */}
      <Show
        when={
          props.provider.id === 'ollama' ||
          props.provider.id === 'openrouter' ||
          props.provider.baseUrl
        }
      >
        <div class="space-y-1">
          <SettingsInput
            value={baseUrl()}
            onInput={(v) => setBaseUrl(v)}
            placeholder="https://api.example.com/v1"
            label="Base URL"
          />
          <button
            type="button"
            onClick={handleSaveBaseUrl}
            disabled={baseUrl() === (props.provider.baseUrl || '')}
            class="text-[var(--settings-text-badge)] text-[var(--accent)] hover:text-[var(--accent-hover)] disabled:opacity-50 disabled:cursor-not-allowed transition-colors"
          >
            Save Base URL
          </button>
        </div>
      </Show>

      <Show when={props.provider.id === 'ollama'}>
        <button
          type="button"
          onClick={() => setShowOllamaBrowser(true)}
          class="flex items-center gap-2 w-full px-3 py-2 text-[var(--settings-text-button)] text-[var(--text-secondary)] hover:text-[var(--accent)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
        >
          <Server class="w-3 h-3" />
          Manage Local Models
        </button>
        <OllamaModelBrowser
          open={showOllamaBrowser()}
          onClose={() => setShowOllamaBrowser(false)}
          baseUrl={baseUrl() || props.provider.baseUrl}
        />
      </Show>

      {/* Models list with refresh */}
      <Show when={props.provider.models.length > 0 || hasAnyCredentials()}>
        <ModelsListSection
          models={props.provider.models}
          defaultModel={props.provider.defaultModel}
          isLoading={isRefreshingModels()}
          error={modelsError()}
          onRefresh={handleRefreshModels}
          onSelectDefault={props.onSetDefaultModel}
        />
      </Show>

      {/* Test connection button */}
      <div class="flex items-center justify-between pt-1">
        <button
          type="button"
          onClick={handleTest}
          disabled={isTesting() || !hasAnyCredentials()}
          class="flex items-center gap-1.5 px-2.5 py-1.5 text-[var(--settings-text-button)] bg-[var(--surface-raised)] hover:bg-[var(--alpha-white-5)] disabled:opacity-50 disabled:cursor-not-allowed rounded-[var(--radius-md)] transition-colors"
          title="Test connection and refresh models"
        >
          <Show when={isTesting()} fallback={<TestTube2 class="w-3.5 h-3.5" />}>
            <Loader2 class="w-3.5 h-3.5 animate-spin" />
          </Show>
          Test Connection
        </button>
      </div>

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

      <div class="flex items-center justify-end pt-1">
        <Show when={hasAnyCredentials() && !showClearConfirm()}>
          <button
            type="button"
            onClick={() => setShowClearConfirm(true)}
            class="flex items-center gap-1 text-[var(--settings-text-badge)] text-[var(--text-muted)] hover:text-[var(--error)] transition-colors"
            title="Clear all credentials"
          >
            <Trash2 class="w-2.5 h-2.5" />
            {isOAuthConnected() ? 'Log out' : 'Clear'}
          </button>
        </Show>
      </div>

      <Show when={props.provider.status === 'error' && props.provider.error}>
        <p class="text-[var(--settings-text-badge)] text-[var(--error)] px-1">
          {props.provider.error}
        </p>
      </Show>
    </div>
  )
}
