/**
 * Step 2: Connect a Provider
 *
 * Shows 5 provider cards with auth options (OAuth / API Key / Configure).
 * Each card has a colored logo square and provider description.
 */

import { Eye, EyeOff, Server } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { AnthropicLogo, GoogleLogo, OpenAILogo, OpenRouterLogo } from '../../icons/provider-logos'

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

interface ProviderDef {
  id: string
  name: string
  description: string
  color: string
  logo: Component<{ class?: string }> | null
  /** null = icon-only (Ollama uses a Lucide icon) */
  useLucideIcon?: boolean
  authOptions: Array<{ label: string; type: 'oauth' | 'apikey' | 'configure' }>
}

const PROVIDERS: ProviderDef[] = [
  {
    id: 'anthropic',
    name: 'Anthropic',
    description: 'Claude 4, Sonnet, Haiku',
    color: '#D97706',
    logo: AnthropicLogo,
    authOptions: [{ label: 'API Key', type: 'apikey' }],
  },
  {
    id: 'openai',
    name: 'OpenAI',
    description: 'GPT-4.1, o3, o4-mini',
    color: '#10A37F',
    logo: OpenAILogo,
    authOptions: [
      { label: 'OAuth', type: 'oauth' },
      { label: 'API Key', type: 'apikey' },
    ],
  },
  {
    id: 'google',
    name: 'Google',
    description: 'Gemini 2.5 Pro, Flash',
    color: '#4285F4',
    logo: GoogleLogo,
    authOptions: [
      { label: 'OAuth', type: 'oauth' },
      { label: 'API Key', type: 'apikey' },
    ],
  },
  {
    id: 'openrouter',
    name: 'OpenRouter',
    description: '100+ models, one API key',
    color: '#6366F1',
    logo: OpenRouterLogo,
    authOptions: [{ label: 'API Key', type: 'apikey' }],
  },
  {
    id: 'ollama',
    name: 'Ollama',
    description: 'Local models, no API key',
    color: '#0F172A',
    logo: null,
    useLucideIcon: true,
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
// Component
// ---------------------------------------------------------------------------

export const ProviderStep: Component<ProviderStepProps> = (props) => {
  const [expandedProvider, setExpandedProvider] = createSignal<string | null>(null)
  const [showKey, setShowKey] = createSignal<Record<string, boolean>>({})

  const toggleShowKey = (id: string): void => {
    setShowKey((prev) => ({ ...prev, [id]: !prev[id] }))
  }

  const handleAuth = (provider: ProviderDef, type: string): void => {
    if (type === 'apikey') {
      setExpandedProvider((prev) => (prev === provider.id ? null : provider.id))
    }
    // OAuth and Configure are no-ops for now — they'll connect to Tauri IPC later
  }

  return (
    <div class="flex flex-col items-center">
      {/* Header */}
      <h2 class="text-2xl font-bold text-[#FAFAFA] tracking-tight mb-2">Connect a Provider</h2>
      <p class="text-sm text-[#71717A] mb-8">Add an LLM provider to start chatting</p>

      {/* Provider cards */}
      <div class="w-full max-w-[560px] flex flex-col gap-3 mb-8">
        <For each={PROVIDERS}>
          {(provider) => (
            <div class="bg-[#18181B] border border-[#27272A] rounded-xl p-4 transition-colors">
              <div class="flex items-center gap-3">
                {/* Logo square */}
                <div
                  class="w-10 h-10 rounded-[10px] flex items-center justify-center flex-shrink-0"
                  style={{ background: provider.color }}
                >
                  <Show
                    when={!provider.useLucideIcon && provider.logo}
                    fallback={<Server class="w-5 h-5 text-white" />}
                  >
                    {(() => {
                      const Logo = provider.logo!
                      return <Logo class="w-5 h-5 text-white" />
                    })()}
                  </Show>
                </div>

                {/* Name + description */}
                <div class="flex-1 min-w-0">
                  <p class="text-sm font-medium text-[#FAFAFA]">{provider.name}</p>
                  <p class="text-xs text-[#71717A]">{provider.description}</p>
                </div>

                {/* Auth buttons */}
                <div class="flex items-center gap-2 flex-shrink-0">
                  <For each={provider.authOptions}>
                    {(opt) => (
                      <button
                        type="button"
                        onClick={() => handleAuth(provider, opt.type)}
                        class="px-3 py-1.5 text-xs font-medium rounded-lg transition-colors"
                        classList={{
                          'bg-[#A78BFA] text-white hover:bg-[#8B5CF6]': opt.type === 'oauth',
                          'bg-[#27272A] text-[#FAFAFA] hover:bg-[#3F3F46]':
                            opt.type === 'apikey' || opt.type === 'configure',
                        }}
                      >
                        {opt.label}
                      </button>
                    )}
                  </For>
                </div>
              </div>

              {/* Expanded API key input */}
              <Show when={expandedProvider() === provider.id}>
                <div class="mt-3 pt-3 border-t border-[#27272A]">
                  <div class="relative">
                    <input
                      type={showKey()[provider.id] ? 'text' : 'password'}
                      value={props.providerKeys[provider.id] ?? ''}
                      onInput={(e) => props.onSetProviderKey(provider.id, e.currentTarget.value)}
                      placeholder={`Enter ${provider.name} API key...`}
                      class="w-full px-3 py-2 pr-10 bg-[#09090B] text-[#FAFAFA] placeholder-[#52525B] border border-[#27272A] rounded-lg text-sm outline-none focus:border-[#A78BFA] transition-colors"
                    />
                    <button
                      type="button"
                      onClick={() => toggleShowKey(provider.id)}
                      class="absolute right-3 top-1/2 -translate-y-1/2 text-[#52525B] hover:text-[#FAFAFA] transition-colors"
                    >
                      <Show when={showKey()[provider.id]} fallback={<Eye class="w-4 h-4" />}>
                        <EyeOff class="w-4 h-4" />
                      </Show>
                    </button>
                  </div>
                </div>
              </Show>
            </div>
          )}
        </For>
      </div>

      {/* Navigation */}
      <div class="w-full max-w-[560px] flex items-center justify-between">
        <button
          type="button"
          onClick={props.onPrev}
          class="text-sm text-[#71717A] hover:text-[#FAFAFA] transition-colors"
        >
          Back
        </button>
        <div class="flex items-center gap-4">
          <button
            type="button"
            onClick={props.onSkip}
            class="text-sm text-[#52525B] hover:text-[#71717A] transition-colors"
          >
            Skip for now
          </button>
          <button
            type="button"
            onClick={props.onNext}
            class="px-6 py-2.5 bg-[#A78BFA] hover:bg-[#8B5CF6] text-white text-sm font-medium rounded-xl transition-colors"
          >
            Continue
          </button>
        </div>
      </div>
    </div>
  )
}
