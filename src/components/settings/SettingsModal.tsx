/**
 * SettingsModal Component
 * Modal for configuring API keys and app settings
 */

import { type Component, createSignal, onMount, Show } from 'solid-js'
import { clearCredentials, getApiKey, setApiKey } from '../../services/auth/credentials'
import type { LLMProvider } from '../../types/llm'

interface SettingsModalProps {
  isOpen: boolean
  onClose: () => void
}

export const SettingsModal: Component<SettingsModalProps> = (props) => {
  const [anthropicKey, setAnthropicKey] = createSignal('')
  const [openrouterKey, setOpenrouterKey] = createSignal('')
  const [showAnthropicKey, setShowAnthropicKey] = createSignal(false)
  const [showOpenrouterKey, setShowOpenrouterKey] = createSignal(false)
  const [saveStatus, setSaveStatus] = createSignal<'idle' | 'saved' | 'error'>('idle')

  // Load existing keys on mount
  onMount(() => {
    const existingAnthropic = getApiKey('anthropic')
    const existingOpenrouter = getApiKey('openrouter')

    if (existingAnthropic) {
      // Show masked version
      setAnthropicKey('sk-ant-••••••••')
    }
    if (existingOpenrouter) {
      setOpenrouterKey('sk-or-••••••••')
    }
  })

  const handleSave = () => {
    try {
      // Only save if the key looks like a real key (not masked)
      if (anthropicKey() && !anthropicKey().includes('••••')) {
        setApiKey('anthropic', anthropicKey())
      }
      if (openrouterKey() && !openrouterKey().includes('••••')) {
        setApiKey('openrouter', openrouterKey())
      }

      setSaveStatus('saved')
      setTimeout(() => {
        setSaveStatus('idle')
        props.onClose()
      }, 1000)
    } catch (err) {
      console.error('Failed to save settings:', err)
      setSaveStatus('error')
      setTimeout(() => setSaveStatus('idle'), 2000)
    }
  }

  const handleClearKey = (provider: LLMProvider) => {
    clearCredentials(provider)
    if (provider === 'anthropic') {
      setAnthropicKey('')
    } else if (provider === 'openrouter') {
      setOpenrouterKey('')
    }
  }

  const handleBackdropClick = (e: MouseEvent) => {
    if (e.target === e.currentTarget) {
      props.onClose()
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') {
      props.onClose()
    }
  }

  return (
    <Show when={props.isOpen}>
      <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="settings-modal-title"
        class="fixed inset-0 bg-black/60 flex items-center justify-center z-50 p-4"
        onClick={handleBackdropClick}
        onKeyDown={handleKeyDown}
      >
        <div class="bg-gray-800 rounded-xl w-full max-w-md shadow-2xl border border-gray-700">
          {/* Header */}
          <div class="flex items-center justify-between p-4 border-b border-gray-700">
            <h2 id="settings-modal-title" class="text-lg font-semibold text-white">
              Settings
            </h2>
            <button
              type="button"
              onClick={props.onClose}
              class="p-1 hover:bg-gray-700 rounded-lg transition-colors"
              aria-label="Close settings"
            >
              <svg
                class="w-5 h-5 text-gray-400"
                fill="none"
                stroke="currentColor"
                viewBox="0 0 24 24"
                role="img"
                aria-hidden="true"
              >
                <path
                  stroke-linecap="round"
                  stroke-linejoin="round"
                  stroke-width="2"
                  d="M6 18L18 6M6 6l12 12"
                />
              </svg>
            </button>
          </div>

          {/* Content */}
          <div class="p-4 space-y-6">
            {/* API Keys Section */}
            <div>
              <h3 class="text-sm font-medium text-gray-300 mb-3">API Keys</h3>
              <div class="space-y-4">
                {/* Anthropic API Key */}
                <div>
                  <label for="anthropic-key" class="block text-sm text-gray-400 mb-1.5">
                    Anthropic API Key
                  </label>
                  <div class="flex gap-2">
                    <div class="flex-1 relative">
                      <input
                        id="anthropic-key"
                        type={showAnthropicKey() ? 'text' : 'password'}
                        value={anthropicKey()}
                        onInput={(e) => setAnthropicKey(e.currentTarget.value)}
                        placeholder="sk-ant-api03-..."
                        class="w-full px-3 py-2 bg-gray-700 border border-gray-600 rounded-lg text-white placeholder-gray-500 focus:outline-none focus:border-blue-500 pr-10"
                      />
                      <button
                        type="button"
                        onClick={() => setShowAnthropicKey(!showAnthropicKey())}
                        class="absolute right-2 top-1/2 -translate-y-1/2 p-1 text-gray-400 hover:text-gray-300"
                        aria-label={showAnthropicKey() ? 'Hide API key' : 'Show API key'}
                      >
                        <Show
                          when={showAnthropicKey()}
                          fallback={
                            <svg
                              class="w-4 h-4"
                              fill="none"
                              stroke="currentColor"
                              viewBox="0 0 24 24"
                              role="img"
                              aria-hidden="true"
                            >
                              <path
                                stroke-linecap="round"
                                stroke-linejoin="round"
                                stroke-width="2"
                                d="M15 12a3 3 0 11-6 0 3 3 0 016 0z"
                              />
                              <path
                                stroke-linecap="round"
                                stroke-linejoin="round"
                                stroke-width="2"
                                d="M2.458 12C3.732 7.943 7.523 5 12 5c4.478 0 8.268 2.943 9.542 7-1.274 4.057-5.064 7-9.542 7-4.477 0-8.268-2.943-9.542-7z"
                              />
                            </svg>
                          }
                        >
                          <svg
                            class="w-4 h-4"
                            fill="none"
                            stroke="currentColor"
                            viewBox="0 0 24 24"
                            role="img"
                            aria-hidden="true"
                          >
                            <path
                              stroke-linecap="round"
                              stroke-linejoin="round"
                              stroke-width="2"
                              d="M13.875 18.825A10.05 10.05 0 0112 19c-4.478 0-8.268-2.943-9.543-7a9.97 9.97 0 011.563-3.029m5.858.908a3 3 0 114.243 4.243M9.878 9.878l4.242 4.242M9.88 9.88l-3.29-3.29m7.532 7.532l3.29 3.29M3 3l3.59 3.59m0 0A9.953 9.953 0 0112 5c4.478 0 8.268 2.943 9.543 7a10.025 10.025 0 01-4.132 5.411m0 0L21 21"
                            />
                          </svg>
                        </Show>
                      </button>
                    </div>
                    <Show when={anthropicKey()}>
                      <button
                        type="button"
                        onClick={() => handleClearKey('anthropic')}
                        class="px-2 py-2 text-red-400 hover:text-red-300 hover:bg-red-900/20 rounded-lg transition-colors"
                        aria-label="Clear Anthropic API key"
                      >
                        <svg
                          class="w-5 h-5"
                          fill="none"
                          stroke="currentColor"
                          viewBox="0 0 24 24"
                          role="img"
                          aria-hidden="true"
                        >
                          <path
                            stroke-linecap="round"
                            stroke-linejoin="round"
                            stroke-width="2"
                            d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"
                          />
                        </svg>
                      </button>
                    </Show>
                  </div>
                  <p class="text-xs text-gray-500 mt-1">Direct access to Claude models</p>
                </div>

                {/* OpenRouter API Key */}
                <div>
                  <label for="openrouter-key" class="block text-sm text-gray-400 mb-1.5">
                    OpenRouter API Key
                  </label>
                  <div class="flex gap-2">
                    <div class="flex-1 relative">
                      <input
                        id="openrouter-key"
                        type={showOpenrouterKey() ? 'text' : 'password'}
                        value={openrouterKey()}
                        onInput={(e) => setOpenrouterKey(e.currentTarget.value)}
                        placeholder="sk-or-v1-..."
                        class="w-full px-3 py-2 bg-gray-700 border border-gray-600 rounded-lg text-white placeholder-gray-500 focus:outline-none focus:border-blue-500 pr-10"
                      />
                      <button
                        type="button"
                        onClick={() => setShowOpenrouterKey(!showOpenrouterKey())}
                        class="absolute right-2 top-1/2 -translate-y-1/2 p-1 text-gray-400 hover:text-gray-300"
                        aria-label={showOpenrouterKey() ? 'Hide API key' : 'Show API key'}
                      >
                        <Show
                          when={showOpenrouterKey()}
                          fallback={
                            <svg
                              class="w-4 h-4"
                              fill="none"
                              stroke="currentColor"
                              viewBox="0 0 24 24"
                              role="img"
                              aria-hidden="true"
                            >
                              <path
                                stroke-linecap="round"
                                stroke-linejoin="round"
                                stroke-width="2"
                                d="M15 12a3 3 0 11-6 0 3 3 0 016 0z"
                              />
                              <path
                                stroke-linecap="round"
                                stroke-linejoin="round"
                                stroke-width="2"
                                d="M2.458 12C3.732 7.943 7.523 5 12 5c4.478 0 8.268 2.943 9.542 7-1.274 4.057-5.064 7-9.542 7-4.477 0-8.268-2.943-9.542-7z"
                              />
                            </svg>
                          }
                        >
                          <svg
                            class="w-4 h-4"
                            fill="none"
                            stroke="currentColor"
                            viewBox="0 0 24 24"
                            role="img"
                            aria-hidden="true"
                          >
                            <path
                              stroke-linecap="round"
                              stroke-linejoin="round"
                              stroke-width="2"
                              d="M13.875 18.825A10.05 10.05 0 0112 19c-4.478 0-8.268-2.943-9.543-7a9.97 9.97 0 011.563-3.029m5.858.908a3 3 0 114.243 4.243M9.878 9.878l4.242 4.242M9.88 9.88l-3.29-3.29m7.532 7.532l3.29 3.29M3 3l3.59 3.59m0 0A9.953 9.953 0 0112 5c4.478 0 8.268 2.943 9.543 7a10.025 10.025 0 01-4.132 5.411m0 0L21 21"
                            />
                          </svg>
                        </Show>
                      </button>
                    </div>
                    <Show when={openrouterKey()}>
                      <button
                        type="button"
                        onClick={() => handleClearKey('openrouter')}
                        class="px-2 py-2 text-red-400 hover:text-red-300 hover:bg-red-900/20 rounded-lg transition-colors"
                        aria-label="Clear OpenRouter API key"
                      >
                        <svg
                          class="w-5 h-5"
                          fill="none"
                          stroke="currentColor"
                          viewBox="0 0 24 24"
                          role="img"
                          aria-hidden="true"
                        >
                          <path
                            stroke-linecap="round"
                            stroke-linejoin="round"
                            stroke-width="2"
                            d="M19 7l-.867 12.142A2 2 0 0116.138 21H7.862a2 2 0 01-1.995-1.858L5 7m5 4v6m4-6v6m1-10V4a1 1 0 00-1-1h-4a1 1 0 00-1 1v3M4 7h16"
                          />
                        </svg>
                      </button>
                    </Show>
                  </div>
                  <p class="text-xs text-gray-500 mt-1">Access to 100+ models via OpenRouter</p>
                </div>
              </div>
            </div>

            {/* Info box */}
            <div class="p-3 bg-gray-700/50 rounded-lg text-sm text-gray-400">
              <p>
                API keys are stored locally on your device and never sent to any server except the
                respective provider.
              </p>
            </div>
          </div>

          {/* Footer */}
          <div class="flex items-center justify-end gap-3 p-4 border-t border-gray-700">
            <button
              type="button"
              onClick={props.onClose}
              class="px-4 py-2 text-gray-300 hover:text-white hover:bg-gray-700 rounded-lg transition-colors"
            >
              Cancel
            </button>
            <button
              type="button"
              onClick={handleSave}
              disabled={saveStatus() === 'saved'}
              class={`px-4 py-2 rounded-lg font-medium transition-colors ${
                saveStatus() === 'saved'
                  ? 'bg-green-600 text-white'
                  : saveStatus() === 'error'
                    ? 'bg-red-600 text-white'
                    : 'bg-blue-600 hover:bg-blue-500 text-white'
              }`}
            >
              <Show when={saveStatus() === 'saved'} fallback="Save">
                Saved!
              </Show>
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}
