/**
 * Settings Modal Component
 *
 * Full-featured settings modal with theme selection, API keys, and preferences.
 * Premium design with multiple sections and smooth animations.
 */

import {
  Bot,
  Check,
  Heart,
  Info,
  Keyboard,
  Monitor,
  Moon,
  Palette,
  Server,
  Sparkles,
  Sun,
  Terminal,
  X,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { useTheme } from '../../contexts/theme'
import { AgentsTab, defaultAgentPresets } from './tabs/AgentsTab'
import { defaultKeybindings, KeybindingsTab } from './tabs/KeybindingsTab'
import { defaultMCPServers, MCPServersTab } from './tabs/MCPServersTab'
import { defaultProviders, ProvidersTab } from './tabs/ProvidersTab'

interface SettingsModalProps {
  isOpen: boolean
  onClose: () => void
}

type SettingsTab = 'appearance' | 'providers' | 'agents' | 'mcp' | 'keybindings' | 'about'

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
  const [saveStatus, setSaveStatus] = createSignal<'idle' | 'saved' | 'error'>('idle')

  // State for new tabs
  const [mcpServers] = createSignal(defaultMCPServers)
  const [keybindings] = createSignal(defaultKeybindings)
  const [agents, setAgents] = createSignal(defaultAgentPresets)
  const [providers, setProviders] = createSignal(defaultProviders)

  const handleSave = () => {
    try {
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
    { id: 'providers' as const, label: 'Providers', icon: Zap },
    { id: 'agents' as const, label: 'Agents', icon: Bot },
    { id: 'mcp' as const, label: 'MCP', icon: Server },
    { id: 'keybindings' as const, label: 'Keys', icon: Keyboard },
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
            w-full max-w-2xl
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

            {/* Providers Tab */}
            <Show when={activeTab() === 'providers'}>
              <ProvidersTab
                providers={providers()}
                onToggle={(id, enabled) => {
                  setProviders((prev) => prev.map((p) => (p.id === id ? { ...p, enabled } : p)))
                }}
              />
            </Show>

            {/* Agents Tab */}
            <Show when={activeTab() === 'agents'}>
              <AgentsTab
                agents={agents()}
                onToggle={(id, enabled) => {
                  setAgents((prev) => prev.map((a) => (a.id === id ? { ...a, enabled } : a)))
                }}
              />
            </Show>

            {/* MCP Servers Tab */}
            <Show when={activeTab() === 'mcp'}>
              <MCPServersTab servers={mcpServers()} />
            </Show>

            {/* Keybindings Tab */}
            <Show when={activeTab() === 'keybindings'}>
              <KeybindingsTab keybindings={keybindings()} />
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
            <Show when={activeTab() === 'providers'}>
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
