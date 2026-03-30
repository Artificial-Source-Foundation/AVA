/**
 * Step 2: Connect a Provider
 *
 * POPULAR label + 4-column grid of square cards (90px height).
 * Connected state: green circle-check + accent border + green text.
 * MORE label + second row with Copilot, OpenRouter, Ollama, "16 more".
 * Nav: Back <- | dots | Skip + Continue button.
 */

import { CheckCircle, Eye, EyeOff, MoreHorizontal, Server } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import {
  isOAuthSupported,
  type OAuthTokens,
  startOAuthFlow,
  storeOAuthCredentials,
} from '../../../services/auth/oauth'
import type { LLMProvider } from '../../../types/llm'
import {
  AnthropicLogo,
  CopilotLogo,
  GoogleLogo,
  OllamaLogo,
  OpenAILogo,
  OpenRouterLogo,
} from '../../icons/provider-logos'

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

interface ProviderDef {
  id: string
  name: string
  authText: string
  color: string
  logo: Component<{ class?: string }> | null
  useLucideIcon?: boolean
  authOptions: Array<{ label: string; type: 'oauth' | 'apikey' | 'configure' }>
}

const POPULAR_PROVIDERS: ProviderDef[] = [
  {
    id: 'anthropic',
    name: 'Anthropic',
    authText: 'API Key',
    color: '#D4A274',
    logo: AnthropicLogo,
    authOptions: [{ label: 'API Key', type: 'apikey' }],
  },
  {
    id: 'openai',
    name: 'OpenAI',
    authText: 'OAuth \u00B7 API Key',
    color: '#10A37F',
    logo: OpenAILogo,
    authOptions: [
      { label: 'OAuth', type: 'oauth' },
      { label: 'API Key', type: 'apikey' },
    ],
  },
  {
    id: 'chatgpt',
    name: 'ChatGPT',
    authText: 'OAuth \u00B7 Pro/Plus sub',
    color: '#10A37F',
    logo: OpenAILogo,
    authOptions: [{ label: 'OAuth', type: 'oauth' }],
  },
  {
    id: 'google',
    name: 'Gemini',
    authText: 'API Key',
    color: '#4285F4',
    logo: GoogleLogo,
    authOptions: [{ label: 'API Key', type: 'apikey' }],
  },
]

const MORE_PROVIDERS: ProviderDef[] = [
  {
    id: 'copilot',
    name: 'Copilot',
    authText: 'OAuth \u00B7 GitHub sub',
    color: '#1F2328',
    logo: CopilotLogo,
    authOptions: [{ label: 'OAuth', type: 'oauth' }],
  },
  {
    id: 'openrouter',
    name: 'OpenRouter',
    authText: 'API Key \u00B7 Credits',
    color: '#FF6200',
    logo: OpenRouterLogo,
    authOptions: [{ label: 'API Key', type: 'apikey' }],
  },
  {
    id: 'ollama',
    name: 'Ollama',
    authText: 'Local \u00B7 Free',
    color: '#0F172A',
    logo: OllamaLogo,
    authOptions: [{ label: 'Configure', type: 'configure' }],
  },
]

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

export interface ProviderStepProps {
  onPrev: () => void
  onNext: () => void
  onSkip: () => void
  providerKeys: Record<string, string>
  onSetProviderKey: (providerId: string, key: string) => void
}

// ---------------------------------------------------------------------------
// ProviderCard sub-component
// ---------------------------------------------------------------------------

const ProviderCard: Component<{
  provider: ProviderDef
  isConnected: boolean
  isExpanded: boolean
  showKey: boolean
  apiKeyValue: string
  oauthLoading: boolean
  onToggleExpand: () => void
  onToggleShowKey: () => void
  onSetKey: (key: string) => void
  onAuth: (provider: ProviderDef, type: string) => void
}> = (cardProps) => (
  <button
    type="button"
    onClick={() => {
      const firstAuth = cardProps.provider.authOptions[0]
      if (firstAuth) {
        cardProps.onAuth(cardProps.provider, firstAuth.type)
      }
    }}
    class="flex flex-col items-start p-3 rounded-xl transition-all text-left"
    style={{
      background: 'var(--surface)',
      border: cardProps.isConnected ? '1px solid var(--accent)' : '1px solid var(--border-subtle)',
      height: '90px',
    }}
  >
    {/* Top row: logo + optional check */}
    <div class="flex items-start justify-between w-full mb-auto">
      <div
        class="w-7 h-7 rounded-lg flex items-center justify-center flex-shrink-0"
        style={{ background: cardProps.provider.color }}
      >
        <Show when={cardProps.provider.logo} fallback={<Server class="w-3.5 h-3.5 text-white" />}>
          {(() => {
            const Logo = cardProps.provider.logo!
            return <Logo class="w-3.5 h-3.5 text-white" />
          })()}
        </Show>
      </div>
      <Show when={cardProps.isConnected}>
        <CheckCircle class="w-4 h-4" style={{ color: '#22C55E' }} />
      </Show>
    </div>

    {/* Name */}
    <p class="text-xs font-medium text-[var(--text-primary)] mt-1">{cardProps.provider.name}</p>

    {/* Auth text */}
    <p
      class="text-[10px] mt-0.5"
      style={{
        color: cardProps.isConnected ? '#22C55E' : 'var(--text-muted)',
      }}
    >
      {cardProps.isConnected ? `API Key \u00B7 Connected` : cardProps.provider.authText}
    </p>
  </button>
)

// ---------------------------------------------------------------------------
// Component
// ---------------------------------------------------------------------------

export const ProviderStep: Component<ProviderStepProps> = (props) => {
  const [expandedProvider, setExpandedProvider] = createSignal<string | null>(null)
  const [showKey, setShowKey] = createSignal<Record<string, boolean>>({})

  const toggleShowKey = (id: string): void => {
    setShowKey((prev) => ({ ...prev, [id]: !prev[id] }))
  }

  const [oauthLoading, setOauthLoading] = createSignal<string | null>(null)

  const handleAuth = async (provider: ProviderDef, type: string): Promise<void> => {
    if (type === 'apikey') {
      setExpandedProvider((prev) => (prev === provider.id ? null : provider.id))
      return
    }
    if (type === 'oauth' && isOAuthSupported(provider.id as LLMProvider)) {
      setOauthLoading(provider.id)
      try {
        const result = await startOAuthFlow(provider.id as LLMProvider)
        if ('accessToken' in result) {
          await storeOAuthCredentials(provider.id as LLMProvider, result as OAuthTokens)
        }
        props.onSetProviderKey(provider.id, '(oauth)')
      } catch (e) {
        console.error(`OAuth flow failed for ${provider.id}:`, e)
      } finally {
        setOauthLoading(null)
      }
    }
  }

  const isConnected = (id: string): boolean => Boolean(props.providerKeys[id])

  return (
    <div class="flex flex-col items-center w-full max-w-[520px]">
      {/* Header */}
      <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight mb-2">
        Connect a Provider
      </h2>
      <p class="text-sm text-[var(--text-muted)] mb-8">Sign in or add an API key</p>

      {/* POPULAR label */}
      <p class="text-[9px] font-semibold uppercase tracking-[0.1em] text-[var(--text-muted)] mb-3 self-start">
        Popular
      </p>

      {/* 4-column grid */}
      <div class="w-full grid grid-cols-4 gap-2 mb-5">
        <For each={POPULAR_PROVIDERS}>
          {(provider) => (
            <ProviderCard
              provider={provider}
              isConnected={isConnected(provider.id)}
              isExpanded={expandedProvider() === provider.id}
              showKey={showKey()[provider.id] ?? false}
              apiKeyValue={props.providerKeys[provider.id] ?? ''}
              oauthLoading={oauthLoading() === provider.id}
              onToggleExpand={() =>
                setExpandedProvider((prev) => (prev === provider.id ? null : provider.id))
              }
              onToggleShowKey={() => toggleShowKey(provider.id)}
              onSetKey={(key) => props.onSetProviderKey(provider.id, key)}
              onAuth={handleAuth}
            />
          )}
        </For>
      </div>

      {/* Expanded API key input (below grid) */}
      <Show when={expandedProvider()}>
        {(expandedId) => {
          const provider = (): ProviderDef | undefined =>
            [...POPULAR_PROVIDERS, ...MORE_PROVIDERS].find((p) => p.id === expandedId())
          return (
            <Show when={provider()}>
              {(p) => (
                <div class="w-full mb-5">
                  <div class="relative">
                    <input
                      type={showKey()[p().id] ? 'text' : 'password'}
                      value={props.providerKeys[p().id] ?? ''}
                      onInput={(e) => props.onSetProviderKey(p().id, e.currentTarget.value)}
                      placeholder={`Enter ${p().name} API key...`}
                      class="w-full px-3 py-2 pr-10 bg-[var(--surface)] text-[var(--text-primary)] placeholder-[var(--text-muted)] rounded-lg text-sm outline-none transition-colors"
                      style={{ border: '1px solid var(--border-subtle)' }}
                      onFocus={(e) => {
                        e.currentTarget.style.borderColor = 'var(--accent)'
                      }}
                      onBlur={(e) => {
                        e.currentTarget.style.borderColor = 'var(--border-subtle)'
                      }}
                    />
                    <button
                      type="button"
                      onClick={() => toggleShowKey(p().id)}
                      class="absolute right-3 top-1/2 -translate-y-1/2 text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
                    >
                      <Show when={showKey()[p().id]} fallback={<Eye class="w-4 h-4" />}>
                        <EyeOff class="w-4 h-4" />
                      </Show>
                    </button>
                  </div>
                </div>
              )}
            </Show>
          )
        }}
      </Show>

      {/* MORE label */}
      <p class="text-[9px] font-semibold uppercase tracking-[0.1em] text-[var(--text-muted)] mb-3 self-start">
        More
      </p>

      {/* Second row: 4 columns (3 providers + "16 more") */}
      <div class="w-full grid grid-cols-4 gap-2 mb-10">
        <For each={MORE_PROVIDERS}>
          {(provider) => (
            <ProviderCard
              provider={provider}
              isConnected={isConnected(provider.id)}
              isExpanded={expandedProvider() === provider.id}
              showKey={showKey()[provider.id] ?? false}
              apiKeyValue={props.providerKeys[provider.id] ?? ''}
              oauthLoading={oauthLoading() === provider.id}
              onToggleExpand={() =>
                setExpandedProvider((prev) => (prev === provider.id ? null : provider.id))
              }
              onToggleShowKey={() => toggleShowKey(provider.id)}
              onSetKey={(key) => props.onSetProviderKey(provider.id, key)}
              onAuth={handleAuth}
            />
          )}
        </For>

        {/* "16 more" card */}
        <div
          class="flex flex-col items-center justify-center rounded-xl"
          style={{
            background: 'var(--surface)',
            border: '1px solid var(--border-subtle)',
            height: '90px',
          }}
        >
          <MoreHorizontal class="w-5 h-5 text-[var(--text-muted)] mb-1" />
          <span class="text-[10px] text-[var(--text-muted)]">16 more</span>
        </div>
      </div>

      {/* Navigation: Back <- | (dots in parent) | Skip + Continue */}
      <div class="w-full flex items-center justify-between">
        <button
          type="button"
          onClick={() => props.onPrev()}
          class="text-sm text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors flex items-center gap-1"
        >
          <span aria-hidden="true">&larr;</span>
          Back
        </button>
        <div class="flex items-center gap-4">
          <button
            type="button"
            onClick={() => props.onSkip()}
            class="text-sm text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
          >
            Skip
          </button>
          <button
            type="button"
            onClick={() => props.onNext()}
            class="px-6 py-2 bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white text-sm font-medium rounded-[10px] transition-colors"
          >
            Continue
          </button>
        </div>
      </div>
    </div>
  )
}
