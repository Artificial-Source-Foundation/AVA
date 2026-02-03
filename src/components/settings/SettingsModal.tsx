/**
 * Settings Modal Component
 *
 * Full-featured settings modal with theme selection, API keys, and preferences.
 * Premium design with multiple sections and smooth animations.
 */

import {
  Check,
  Eye,
  EyeOff,
  Heart,
  Info,
  Key,
  Monitor,
  Moon,
  Palette,
  Shield,
  Sparkles,
  Sun,
  Terminal,
  Trash2,
  X,
} from 'lucide-solid'
import { type Component, createSignal, For, onMount, Show } from 'solid-js'
import { useTheme } from '../../contexts/theme'
import { clearCredentials, getApiKey, setApiKey } from '../../services/auth/credentials'
import type { LLMProvider } from '../../types/llm'

interface SettingsModalProps {
  isOpen: boolean
  onClose: () => void
}

type SettingsTab = 'appearance' | 'api-keys' | 'about'

const themes = [
  { id: 'glass', name: 'Glass', icon: Sparkles, description: 'Apple-inspired with subtle blur' },
  { id: 'minimal', name: 'Minimal', icon: Monitor, description: 'Clean and focused like Linear' },
  {
    id: 'terminal',
    name: 'Terminal',
    icon: Terminal,
    description: 'Catppuccin-inspired hacker vibe',
  },
  { id: 'soft', name: 'Soft', icon: Heart, description: 'Warm and friendly aesthetic' },
] as const

export const SettingsModal: Component<SettingsModalProps> = (props) => {
  const { theme, setTheme, mode, setMode } = useTheme()
  const [activeTab, setActiveTab] = createSignal<SettingsTab>('appearance')
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
      setAnthropicKey('sk-ant-••••••••')
    }
    if (existingOpenrouter) {
      setOpenrouterKey('sk-or-••••••••')
    }
  })

  const handleSave = () => {
    try {
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

  const tabs = [
    { id: 'appearance' as const, label: 'Appearance', icon: Palette },
    { id: 'api-keys' as const, label: 'API Keys', icon: Key },
    { id: 'about' as const, label: 'About', icon: Info },
  ]

  return (
    <Show when={props.isOpen}>
      <div
        role="dialog"
        aria-modal="true"
        aria-labelledby="settings-modal-title"
        class="
          fixed inset-0
          bg-black/50 backdrop-blur-sm
          flex items-center justify-center
          z-50 p-4
        "
        onClick={handleBackdropClick}
        onKeyDown={handleKeyDown}
      >
        <div
          class="
            bg-[var(--surface-overlay)]
            border border-[var(--border-default)]
            rounded-[var(--radius-xl)]
            w-full max-w-lg
            shadow-xl
            overflow-hidden
          "
        >
          {/* Header */}
          <div
            class="
              flex items-center justify-between
              px-6 py-4
              border-b border-[var(--border-subtle)]
            "
          >
            <h2
              id="settings-modal-title"
              class="text-lg font-semibold text-[var(--text-primary)] font-display"
            >
              Settings
            </h2>
            <button
              type="button"
              onClick={props.onClose}
              class="
                p-2
                rounded-[var(--radius-md)]
                text-[var(--text-tertiary)]
                hover:text-[var(--text-primary)]
                hover:bg-[var(--surface-raised)]
                transition-colors duration-[var(--duration-fast)]
              "
              aria-label="Close settings"
            >
              <X class="w-5 h-5" />
            </button>
          </div>

          {/* Tab Navigation */}
          <div class="flex border-b border-[var(--border-subtle)]">
            <For each={tabs}>
              {(tab) => (
                <button
                  type="button"
                  onClick={() => setActiveTab(tab.id)}
                  class={`
                    flex-1 flex items-center justify-center gap-2
                    px-4 py-3
                    text-sm font-medium
                    transition-all duration-[var(--duration-fast)]
                    border-b-2
                    ${
                      activeTab() === tab.id
                        ? 'text-[var(--accent)] border-[var(--accent)] bg-[var(--accent-subtle)]'
                        : 'text-[var(--text-tertiary)] border-transparent hover:text-[var(--text-primary)] hover:bg-[var(--surface-raised)]'
                    }
                  `}
                >
                  <tab.icon class="w-4 h-4" />
                  {tab.label}
                </button>
              )}
            </For>
          </div>

          {/* Content */}
          <div class="p-6 max-h-[60vh] overflow-y-auto">
            {/* Appearance Tab */}
            <Show when={activeTab() === 'appearance'}>
              <div class="space-y-6">
                {/* Theme Selection */}
                <div>
                  <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-3">Theme</h3>
                  <div class="grid grid-cols-2 gap-3">
                    <For each={themes}>
                      {(t) => (
                        <button
                          type="button"
                          onClick={() => setTheme(t.id)}
                          class={`
                            flex items-start gap-3
                            p-3
                            rounded-[var(--radius-lg)]
                            border
                            text-left
                            transition-all duration-[var(--duration-fast)]
                            ${
                              theme() === t.id
                                ? 'border-[var(--accent)] bg-[var(--accent-subtle)]'
                                : 'border-[var(--border-subtle)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                            }
                          `}
                        >
                          <div
                            class={`
                              p-2 rounded-[var(--radius-md)]
                              ${
                                theme() === t.id
                                  ? 'bg-[var(--accent)] text-white'
                                  : 'bg-[var(--surface-raised)] text-[var(--text-tertiary)]'
                              }
                            `}
                          >
                            <t.icon class="w-4 h-4" />
                          </div>
                          <div class="flex-1 min-w-0">
                            <p
                              class={`
                                text-sm font-medium
                                ${
                                  theme() === t.id
                                    ? 'text-[var(--accent)]'
                                    : 'text-[var(--text-primary)]'
                                }
                              `}
                            >
                              {t.name}
                            </p>
                            <p class="text-xs text-[var(--text-muted)] truncate">{t.description}</p>
                          </div>
                          <Show when={theme() === t.id}>
                            <Check class="w-4 h-4 text-[var(--accent)] flex-shrink-0" />
                          </Show>
                        </button>
                      )}
                    </For>
                  </div>
                </div>

                {/* Mode Toggle */}
                <div>
                  <h3 class="text-sm font-medium text-[var(--text-secondary)] mb-3">Mode</h3>
                  <div class="flex gap-2">
                    <button
                      type="button"
                      onClick={() => setMode('light')}
                      class={`
                        flex-1 flex items-center justify-center gap-2
                        px-4 py-3
                        rounded-[var(--radius-lg)]
                        border
                        text-sm font-medium
                        transition-all duration-[var(--duration-fast)]
                        ${
                          mode() === 'light'
                            ? 'border-[var(--accent)] bg-[var(--accent-subtle)] text-[var(--accent)]'
                            : 'border-[var(--border-subtle)] text-[var(--text-secondary)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                        }
                      `}
                    >
                      <Sun class="w-4 h-4" />
                      Light
                    </button>
                    <button
                      type="button"
                      onClick={() => setMode('dark')}
                      class={`
                        flex-1 flex items-center justify-center gap-2
                        px-4 py-3
                        rounded-[var(--radius-lg)]
                        border
                        text-sm font-medium
                        transition-all duration-[var(--duration-fast)]
                        ${
                          mode() === 'dark'
                            ? 'border-[var(--accent)] bg-[var(--accent-subtle)] text-[var(--accent)]'
                            : 'border-[var(--border-subtle)] text-[var(--text-secondary)] hover:border-[var(--border-default)] hover:bg-[var(--surface-raised)]'
                        }
                      `}
                    >
                      <Moon class="w-4 h-4" />
                      Dark
                    </button>
                  </div>
                </div>
              </div>
            </Show>

            {/* API Keys Tab */}
            <Show when={activeTab() === 'api-keys'}>
              <div class="space-y-6">
                {/* Anthropic API Key */}
                <div>
                  <label
                    for="anthropic-key"
                    class="block text-sm font-medium text-[var(--text-secondary)] mb-2"
                  >
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
                        class="
                          w-full px-4 py-2.5 pr-10
                          bg-[var(--input-background)]
                          text-[var(--text-primary)]
                          placeholder-[var(--text-muted)]
                          border border-[var(--input-border)]
                          rounded-[var(--radius-lg)]
                          text-sm
                          transition-all duration-[var(--duration-fast)]
                          focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
                        "
                      />
                      <button
                        type="button"
                        onClick={() => setShowAnthropicKey(!showAnthropicKey())}
                        class="
                          absolute right-3 top-1/2 -translate-y-1/2
                          text-[var(--text-tertiary)] hover:text-[var(--text-primary)]
                          transition-colors duration-[var(--duration-fast)]
                        "
                        aria-label={showAnthropicKey() ? 'Hide API key' : 'Show API key'}
                      >
                        <Show when={showAnthropicKey()} fallback={<Eye class="w-4 h-4" />}>
                          <EyeOff class="w-4 h-4" />
                        </Show>
                      </button>
                    </div>
                    <Show when={anthropicKey()}>
                      <button
                        type="button"
                        onClick={() => handleClearKey('anthropic')}
                        class="
                          p-2.5
                          text-[var(--error)] hover:text-[var(--error-hover)]
                          hover:bg-[var(--error-subtle)]
                          rounded-[var(--radius-lg)]
                          transition-colors duration-[var(--duration-fast)]
                        "
                        aria-label="Clear Anthropic API key"
                      >
                        <Trash2 class="w-5 h-5" />
                      </button>
                    </Show>
                  </div>
                  <p class="text-xs text-[var(--text-muted)] mt-1.5">
                    Direct access to Claude models
                  </p>
                </div>

                {/* OpenRouter API Key */}
                <div>
                  <label
                    for="openrouter-key"
                    class="block text-sm font-medium text-[var(--text-secondary)] mb-2"
                  >
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
                        class="
                          w-full px-4 py-2.5 pr-10
                          bg-[var(--input-background)]
                          text-[var(--text-primary)]
                          placeholder-[var(--text-muted)]
                          border border-[var(--input-border)]
                          rounded-[var(--radius-lg)]
                          text-sm
                          transition-all duration-[var(--duration-fast)]
                          focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
                        "
                      />
                      <button
                        type="button"
                        onClick={() => setShowOpenrouterKey(!showOpenrouterKey())}
                        class="
                          absolute right-3 top-1/2 -translate-y-1/2
                          text-[var(--text-tertiary)] hover:text-[var(--text-primary)]
                          transition-colors duration-[var(--duration-fast)]
                        "
                        aria-label={showOpenrouterKey() ? 'Hide API key' : 'Show API key'}
                      >
                        <Show when={showOpenrouterKey()} fallback={<Eye class="w-4 h-4" />}>
                          <EyeOff class="w-4 h-4" />
                        </Show>
                      </button>
                    </div>
                    <Show when={openrouterKey()}>
                      <button
                        type="button"
                        onClick={() => handleClearKey('openrouter')}
                        class="
                          p-2.5
                          text-[var(--error)] hover:text-[var(--error-hover)]
                          hover:bg-[var(--error-subtle)]
                          rounded-[var(--radius-lg)]
                          transition-colors duration-[var(--duration-fast)]
                        "
                        aria-label="Clear OpenRouter API key"
                      >
                        <Trash2 class="w-5 h-5" />
                      </button>
                    </Show>
                  </div>
                  <p class="text-xs text-[var(--text-muted)] mt-1.5">
                    Access to 100+ models via OpenRouter
                  </p>
                </div>

                {/* Security Info */}
                <div
                  class="
                    flex items-start gap-3
                    p-4
                    bg-[var(--surface-sunken)]
                    border border-[var(--border-subtle)]
                    rounded-[var(--radius-lg)]
                  "
                >
                  <Shield class="w-5 h-5 text-[var(--success)] flex-shrink-0 mt-0.5" />
                  <p class="text-sm text-[var(--text-secondary)]">
                    API keys are stored locally on your device and never sent to any server except
                    the respective provider.
                  </p>
                </div>
              </div>
            </Show>

            {/* About Tab */}
            <Show when={activeTab() === 'about'}>
              <div class="space-y-6">
                <div class="text-center py-4">
                  <div
                    class="
                      w-16 h-16 mx-auto mb-4
                      rounded-[var(--radius-xl)]
                      bg-[var(--accent)]
                      flex items-center justify-center
                      shadow-lg
                    "
                  >
                    <Sparkles class="w-8 h-8 text-white" />
                  </div>
                  <h3 class="text-xl font-semibold text-[var(--text-primary)] font-display">
                    Estela
                  </h3>
                  <p class="text-sm text-[var(--text-tertiary)] mt-1">
                    Multi-Agent AI Coding Assistant
                  </p>
                  <p class="text-xs text-[var(--text-muted)] mt-2">Version 0.1.0</p>
                </div>

                <div class="space-y-3">
                  <div
                    class="
                      flex items-center justify-between
                      p-3
                      bg-[var(--surface-sunken)]
                      rounded-[var(--radius-lg)]
                    "
                  >
                    <span class="text-sm text-[var(--text-secondary)]">Built with</span>
                    <span class="text-sm text-[var(--text-primary)] font-medium">
                      SolidJS + Tauri
                    </span>
                  </div>
                  <div
                    class="
                      flex items-center justify-between
                      p-3
                      bg-[var(--surface-sunken)]
                      rounded-[var(--radius-lg)]
                    "
                  >
                    <span class="text-sm text-[var(--text-secondary)]">License</span>
                    <span class="text-sm text-[var(--text-primary)] font-medium">MIT</span>
                  </div>
                </div>
              </div>
            </Show>
          </div>

          {/* Footer */}
          <div
            class="
              flex items-center justify-end gap-3
              px-6 py-4
              border-t border-[var(--border-subtle)]
              bg-[var(--surface-sunken)]
            "
          >
            <button
              type="button"
              onClick={props.onClose}
              class="
                px-4 py-2
                text-[var(--text-secondary)] hover:text-[var(--text-primary)]
                hover:bg-[var(--surface-raised)]
                rounded-[var(--radius-lg)]
                text-sm font-medium
                transition-colors duration-[var(--duration-fast)]
              "
            >
              Cancel
            </button>
            <Show when={activeTab() === 'api-keys'}>
              <button
                type="button"
                onClick={handleSave}
                disabled={saveStatus() === 'saved'}
                class={`
                  px-4 py-2
                  rounded-[var(--radius-lg)]
                  text-sm font-medium
                  transition-all duration-[var(--duration-fast)]
                  flex items-center gap-2
                  ${
                    saveStatus() === 'saved'
                      ? 'bg-[var(--success)] text-white'
                      : saveStatus() === 'error'
                        ? 'bg-[var(--error)] text-white'
                        : 'bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white'
                  }
                `}
              >
                <Show when={saveStatus() === 'saved'} fallback="Save Changes">
                  <Check class="w-4 h-4" />
                  Saved!
                </Show>
              </button>
            </Show>
          </div>
        </div>
      </div>
    </Show>
  )
}
