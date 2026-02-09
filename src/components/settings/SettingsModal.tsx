/**
 * Settings Modal — OpenCode-inspired Design
 *
 * Large centered modal with left sidebar navigation.
 * Replaces the full-page SettingsPage.
 * Opens via Ctrl+, or the gear icon in the activity bar.
 */

import {
  Bot,
  Check,
  Code2,
  Cpu,
  Download,
  ExternalLink,
  Info,
  Keyboard,
  Monitor,
  Palette,
  Puzzle,
  Server,
  type Settings,
  Sliders,
  Trash2,
  Upload,
  Wand2,
  X,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, type JSXElement, Show } from 'solid-js'
import { fetchModels } from '../../services/providers/model-fetcher'
import { useLayout } from '../../stores/layout'
import type { UISettings } from '../../stores/settings'
import { useSettings } from '../../stores/settings'
import { useShortcuts } from '../../stores/shortcuts'
import type { LLMProvider } from '../../types/llm'
import { type AgentPreset, AgentsTab } from './tabs/AgentsTab'
import { AppearanceTab } from './tabs/AppearanceTab'
import { BehaviorTab } from './tabs/BehaviorTab'
import { DeveloperTab } from './tabs/DeveloperTab'
import { type Keybinding, KeybindingsTab } from './tabs/KeybindingsTab'
import { LLMTab } from './tabs/LLMTab'
import { type MCPServer, MCPServersTab } from './tabs/MCPServersTab'
import { ProvidersTab } from './tabs/ProvidersTab'

// ============================================================================
// Tab Configuration
// ============================================================================

type SettingsTab =
  | 'general'
  | 'appearance'
  | 'behavior'
  | 'shortcuts'
  | 'providers'
  | 'llm'
  | 'models'
  | 'mcp'
  | 'plugins'
  | 'developer'
  | 'about'

interface TabGroup {
  label: string
  tabs: { id: SettingsTab; label: string; icon: typeof Settings }[]
}

const tabGroups: TabGroup[] = [
  {
    label: 'Desktop',
    tabs: [
      { id: 'general', label: 'General', icon: Monitor },
      { id: 'appearance', label: 'Appearance', icon: Palette },
      { id: 'behavior', label: 'Behavior', icon: Sliders },
      { id: 'shortcuts', label: 'Shortcuts', icon: Keyboard },
    ],
  },
  {
    label: 'AI',
    tabs: [
      { id: 'providers', label: 'Providers', icon: Zap },
      { id: 'llm', label: 'LLM', icon: Cpu },
      { id: 'models', label: 'Models', icon: Bot },
    ],
  },
  {
    label: 'Extensions',
    tabs: [
      { id: 'mcp', label: 'MCP Servers', icon: Server },
      { id: 'plugins', label: 'Plugins', icon: Puzzle },
    ],
  },
  {
    label: 'Advanced',
    tabs: [{ id: 'developer', label: 'Developer', icon: Code2 }],
  },
  {
    label: '',
    tabs: [{ id: 'about', label: 'About', icon: Info }],
  },
]

// ============================================================================
// Available Models (2026)
// ============================================================================

const AVAILABLE_MODELS = [
  { id: '', label: 'Use default' },
  { id: 'claude-opus-4-6', label: 'Claude Opus 4.6' },
  { id: 'claude-sonnet-4-5-20250929', label: 'Claude Sonnet 4.5' },
  { id: 'claude-haiku-4-5-20251001', label: 'Claude Haiku 4.5' },
  { id: 'gpt-4.1', label: 'GPT-4.1' },
  { id: 'gpt-4.1-mini', label: 'GPT-4.1 Mini' },
  { id: 'gpt-4.1-nano', label: 'GPT-4.1 Nano' },
  { id: 'o3', label: 'o3' },
  { id: 'o4-mini', label: 'o4-mini' },
  { id: 'gemini-2.5-pro', label: 'Gemini 2.5 Pro' },
  { id: 'gemini-2.5-flash', label: 'Gemini 2.5 Flash' },
  { id: 'deepseek-r1', label: 'DeepSeek R1' },
  { id: 'deepseek-v3', label: 'DeepSeek V3' },
  { id: 'qwen-2.5-coder', label: 'Qwen 2.5 Coder' },
]

const ALL_CAPABILITIES = [
  'code-generation',
  'debugging',
  'refactoring',
  'code-review',
  'git-status',
  'commit-messages',
  'branch-management',
  'merge-resolution',
  'command-execution',
  'process-management',
  'environment-setup',
  'readme',
  'api-docs',
  'comments',
  'tutorials',
  'quick-answers',
  'simple-tasks',
  'web-search',
  'file-management',
  'testing',
  'security-analysis',
  'performance-optimization',
]

// Built-in skills for the Plugins tab
const builtInSkills = [
  {
    name: 'Code Navigation',
    description: 'Jump to definitions, references, and symbols',
    active: true,
  },
  { name: 'Git Integration', description: 'Stage, commit, and diff from chat', active: true },
  { name: 'Test Runner', description: 'Run and debug tests inline', active: true },
  { name: 'Web Search', description: 'Search the web for documentation', active: true },
  { name: 'Browser Automation', description: 'Puppeteer-based web interaction', active: false },
]

// ============================================================================
// Main Settings Modal
// ============================================================================

export const SettingsModal: Component = () => {
  const { settingsOpen, closeSettings } = useLayout()
  const {
    settings,
    updateProvider,
    updateAgent,
    addAgent,
    removeAgent,
    addMcpServer,
    removeMcpServer,
  } = useSettings()
  const { shortcuts, updateShortcut, resetShortcut, resetAll: resetAllShortcuts } = useShortcuts()
  const [activeTab, setActiveTab] = createSignal<SettingsTab>('general')
  const [saveStatus, setSaveStatus] = createSignal<'idle' | 'saved'>('idle')

  // Map core MCPServerConfig[] → MCPServer[] for the UI tab
  const mcpServers = (): MCPServer[] =>
    settings().mcpServers.map((s) => ({
      id: s.name,
      name: s.name,
      url: s.url ?? (s.command ? `${s.command} ${(s.args ?? []).join(' ')}` : 'stdio'),
      status: 'disconnected' as const,
      description: `${s.type} transport`,
    }))

  const [editingAgent, setEditingAgent] = createSignal<AgentPreset | null>(null)
  const [editingKeybinding, setEditingKeybinding] = createSignal<Keybinding | null>(null)
  const [creatingAgent, setCreatingAgent] = createSignal(false)

  // Bridge shortcuts store → KeybindingsTab (maps ShortcutDef → Keybinding)
  const keybindings = () =>
    shortcuts().map((s) => ({
      id: s.id,
      action: s.label,
      keys: s.keys,
      description: s.description,
      category: s.category,
      isCustom: s.isCustom,
    }))

  const handleSave = () => {
    setSaveStatus('saved')
    setTimeout(() => setSaveStatus('idle'), 1500)
  }

  const handleCreateAgent = () => {
    setCreatingAgent(true)
    setEditingAgent({
      id: `custom-${Date.now()}`,
      name: '',
      description: '',
      icon: Wand2,
      enabled: true,
      capabilities: [],
      model: '',
      isCustom: true,
      type: 'custom',
    })
  }

  const handleSaveAgent = (agent: AgentPreset) => {
    if (creatingAgent()) {
      addAgent(agent)
      setCreatingAgent(false)
    } else {
      updateAgent(agent.id, agent)
    }
    setEditingAgent(null)
  }

  const handleDeleteAgent = (id: string) => {
    removeAgent(id)
  }

  const handleEditKeybinding = (id: string) => {
    const kb = keybindings().find((k) => k.id === id)
    if (kb) setEditingKeybinding(kb)
  }

  const handleSaveKeybinding = (kb: Keybinding) => {
    // Normalize 'meta' → 'ctrl' to match shortcuts store convention
    const normalizedKeys = kb.keys.map((k) => (k === 'meta' ? 'ctrl' : k))
    updateShortcut(kb.id, normalizedKeys)
    setEditingKeybinding(null)
  }

  const handleResetKeybinding = (id: string) => {
    resetShortcut(id)
  }

  const handleResetAllKeybindings = () => {
    resetAllShortcuts()
  }

  const handleBackdropClick = (e: MouseEvent) => {
    if (e.target === e.currentTarget) closeSettings()
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape') closeSettings()
  }

  return (
    <Show when={settingsOpen()}>
      {/* biome-ignore lint/a11y/noStaticElementInteractions: backdrop click to close is a standard modal pattern */}
      <div
        class="fixed inset-0 bg-black/60 flex items-center justify-center z-50 animate-fade-in"
        onClick={handleBackdropClick}
        onKeyDown={handleKeyDown}
      >
        <div
          class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-3xl shadow-2xl animate-scale-in flex overflow-hidden"
          style={{ height: 'min(85vh, 640px)' }}
        >
          {/* Left sidebar nav */}
          <nav class="w-44 flex-shrink-0 border-r border-[var(--border-subtle)] bg-[var(--gray-1)] flex flex-col py-3">
            <div class="px-4 mb-3">
              <h2 class="text-sm font-semibold text-[var(--text-primary)]">Settings</h2>
            </div>

            <div class="flex-1 space-y-3 px-2">
              <For each={tabGroups}>
                {(group) => (
                  <div>
                    <Show when={group.label}>
                      <p class="px-2 mb-1 text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider">
                        {group.label}
                      </p>
                    </Show>
                    <div class="space-y-0.5">
                      <For each={group.tabs}>
                        {(tab) => {
                          const Icon = tab.icon
                          return (
                            <button
                              type="button"
                              onClick={() => setActiveTab(tab.id)}
                              class={`
                                w-full flex items-center gap-2 px-2 py-1.5
                                text-xs rounded-[var(--radius-md)]
                                transition-colors duration-[var(--duration-fast)]
                                ${
                                  activeTab() === tab.id
                                    ? 'text-[var(--text-primary)] bg-[var(--alpha-white-8)]'
                                    : 'text-[var(--text-muted)] hover:text-[var(--text-secondary)] hover:bg-[var(--alpha-white-5)]'
                                }
                              `}
                            >
                              <Icon class="w-3.5 h-3.5" />
                              {tab.label}
                            </button>
                          )
                        }}
                      </For>
                    </div>
                  </div>
                )}
              </For>
            </div>
          </nav>

          {/* Right content area */}
          <div class="flex-1 flex flex-col min-w-0">
            {/* Header bar */}
            <div class="flex items-center justify-between px-5 py-3 border-b border-[var(--border-subtle)] flex-shrink-0">
              <span class="text-sm font-medium text-[var(--text-primary)] capitalize">
                {tabGroups.flatMap((g) => g.tabs).find((t) => t.id === activeTab())?.label}
              </span>
              <div class="flex items-center gap-2">
                <Show when={activeTab() === 'providers'}>
                  <button
                    type="button"
                    onClick={handleSave}
                    disabled={saveStatus() === 'saved'}
                    class={`
                      flex items-center gap-1.5 px-3 py-1.5
                      rounded-[var(--radius-md)]
                      text-xs font-medium
                      transition-colors duration-[var(--duration-fast)]
                      ${
                        saveStatus() === 'saved'
                          ? 'bg-[var(--success)] text-white'
                          : 'bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white'
                      }
                    `}
                  >
                    <Show when={saveStatus() === 'saved'} fallback="Save">
                      <Check class="w-3 h-3" />
                      Saved
                    </Show>
                  </button>
                </Show>
                <button
                  type="button"
                  onClick={closeSettings}
                  class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
                >
                  <X class="w-4 h-4" />
                </button>
              </div>
            </div>

            {/* Scrollable content — translateZ(0) for smooth WebKitGTK scroll */}
            <div class="flex-1 overflow-y-auto" style={{ transform: 'translateZ(0)' }}>
              <div class="max-w-2xl mx-auto px-6 py-6">
                <Show when={activeTab() === 'general'}>
                  <GeneralSection />
                </Show>

                <Show when={activeTab() === 'appearance'}>
                  <AppearanceTab />
                </Show>

                <Show when={activeTab() === 'behavior'}>
                  <BehaviorTab />
                </Show>

                <Show when={activeTab() === 'shortcuts'}>
                  <KeybindingsTab
                    keybindings={keybindings()}
                    onEdit={handleEditKeybinding}
                    onReset={handleResetKeybinding}
                    onResetAll={handleResetAllKeybindings}
                  />
                </Show>

                <Show when={activeTab() === 'providers'}>
                  <ProvidersTab
                    providers={settings().providers}
                    onToggle={(id, enabled) => updateProvider(id, { enabled })}
                    onSaveApiKey={(id, key) =>
                      updateProvider(id, { apiKey: key, status: 'connected' })
                    }
                    onClearApiKey={(id) =>
                      updateProvider(id, { apiKey: undefined, status: 'disconnected' })
                    }
                    onSetDefaultModel={(providerId, modelId) =>
                      updateProvider(providerId, { defaultModel: modelId })
                    }
                    onUpdateModels={(providerId, models) => {
                      updateProvider(providerId, {
                        models,
                        defaultModel: models.find((m) => m.isDefault)?.id || models[0]?.id,
                      })
                    }}
                    onTestConnection={async (id) => {
                      const provider = settings().providers.find((p) => p.id === id)
                      if (!provider?.apiKey) return
                      try {
                        const models = await fetchModels(id as LLMProvider, {
                          apiKey: provider.apiKey,
                          baseUrl: provider.baseUrl,
                        })
                        updateProvider(id, {
                          status: models.length > 0 ? 'connected' : 'disconnected',
                          error: undefined,
                        })
                      } catch (err) {
                        updateProvider(id, {
                          status: 'error',
                          error: err instanceof Error ? err.message : 'Connection failed',
                        })
                      }
                    }}
                  />
                </Show>

                <Show when={activeTab() === 'llm'}>
                  <LLMTab />
                </Show>

                <Show when={activeTab() === 'models'}>
                  <AgentsTab
                    agents={settings().agents}
                    onToggle={(id, enabled) => updateAgent(id, { enabled })}
                    onEdit={(id) => {
                      const agent = settings().agents.find((a) => a.id === id)
                      if (agent) {
                        setCreatingAgent(false)
                        setEditingAgent(agent)
                      }
                    }}
                    onDelete={handleDeleteAgent}
                    onCreate={handleCreateAgent}
                  />
                </Show>

                <Show when={activeTab() === 'mcp'}>
                  <MCPServersTab
                    servers={mcpServers()}
                    onRemove={(id) => removeMcpServer(id)}
                    onAdd={() => {
                      // Add a placeholder server for the user to edit
                      const name = `server-${Date.now()}`
                      addMcpServer({ name, type: 'sse', url: 'http://localhost:3001' })
                    }}
                  />
                </Show>

                <Show when={activeTab() === 'plugins'}>
                  <PluginsSection />
                </Show>

                <Show when={activeTab() === 'developer'}>
                  <DeveloperTab />
                </Show>

                <Show when={activeTab() === 'about'}>
                  <AboutSection />
                </Show>
              </div>
            </div>
          </div>
        </div>

        {/* Agent Edit/Create Modal */}
        <Show when={editingAgent()}>
          <AgentEditModal
            agent={editingAgent()!}
            isCreating={creatingAgent()}
            onClose={() => {
              setEditingAgent(null)
              setCreatingAgent(false)
            }}
            onSave={handleSaveAgent}
          />
        </Show>

        {/* Keybinding Edit Modal */}
        <Show when={editingKeybinding()}>
          <KeybindingEditModal
            keybinding={editingKeybinding()!}
            onClose={() => setEditingKeybinding(null)}
            onSave={handleSaveKeybinding}
          />
        </Show>
      </div>
    </Show>
  )
}

// ============================================================================
// General Section
// ============================================================================

const GeneralSection: Component = () => {
  const { settings, updateUI, updateAgentLimits, exportSettings, importSettings, resetSettings } =
    useSettings()
  const [confirmClear, setConfirmClear] = createSignal(false)

  const uiToggles: { key: keyof UISettings; label: string }[] = [
    { key: 'showBottomPanel', label: 'Show memory panel on start' },
    { key: 'showAgentActivity', label: 'Show agent activity panel' },
    { key: 'compactMessages', label: 'Compact message layout' },
    { key: 'showInfoBar', label: 'Show chat info bar' },
    { key: 'showTokenCount', label: 'Show token count' },
    { key: 'showModelInTitleBar', label: 'Show model in title bar' },
  ]

  const handleClearAll = () => {
    localStorage.clear()
    resetSettings()
    setConfirmClear(false)
    window.location.reload()
  }

  return (
    <div class="space-y-4">
      <div>
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Interface
        </h3>
        <div class="space-y-0.5">
          <For each={uiToggles}>
            {(toggle) => (
              <div class="flex items-center justify-between py-1.5">
                <span class="text-xs text-[var(--text-secondary)]">{toggle.label}</span>
                <button
                  type="button"
                  onClick={() => updateUI({ [toggle.key]: !settings().ui[toggle.key] })}
                  class={`
                    w-9 h-5 rounded-full transition-colors flex-shrink-0
                    flex items-center
                    ${settings().ui[toggle.key] ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'}
                  `}
                >
                  <span
                    class={`
                      w-4 h-4 rounded-full bg-white shadow-sm
                      transition-transform duration-150
                      ${settings().ui[toggle.key] ? 'translate-x-[18px]' : 'translate-x-[2px]'}
                    `}
                  />
                </button>
              </div>
            )}
          </For>
        </div>
      </div>

      {/* Agent Behavior */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Agent
        </h3>
        <div class="flex items-center justify-between py-1.5">
          <div>
            <span class="text-xs text-[var(--text-secondary)]">
              Auto-fix lint errors after edits
            </span>
            <p class="text-[10px] text-[var(--text-muted)]">
              Run linter after file changes and feed errors back to agent
            </p>
          </div>
          <button
            type="button"
            onClick={() => updateAgentLimits({ autoFixLint: !settings().agentLimits.autoFixLint })}
            class={`
              w-9 h-5 rounded-full transition-colors flex-shrink-0
              flex items-center
              ${settings().agentLimits.autoFixLint ? 'bg-[var(--accent)]' : 'bg-[var(--border-default)]'}
            `}
          >
            <span
              class={`
                w-4 h-4 rounded-full bg-white shadow-sm
                transition-transform duration-150
                ${settings().agentLimits.autoFixLint ? 'translate-x-[18px]' : 'translate-x-[2px]'}
              `}
            />
          </button>
        </div>
      </div>

      {/* Data Management */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
          Data
        </h3>
        <div class="flex flex-wrap gap-2">
          <button
            type="button"
            onClick={() => exportSettings()}
            class="flex items-center gap-1.5 px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
          >
            <Download class="w-3 h-3" />
            Export Settings
          </button>
          <button
            type="button"
            onClick={() => importSettings()}
            class="flex items-center gap-1.5 px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--accent-muted)] transition-colors"
          >
            <Upload class="w-3 h-3" />
            Import Settings
          </button>
          <Show
            when={confirmClear()}
            fallback={
              <button
                type="button"
                onClick={() => setConfirmClear(true)}
                class="flex items-center gap-1.5 px-2.5 py-1.5 text-[11px] text-[var(--error)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] hover:border-[var(--error)] transition-colors"
              >
                <Trash2 class="w-3 h-3" />
                Clear All Data
              </button>
            }
          >
            <div class="flex items-center gap-1.5">
              <span class="text-[11px] text-[var(--error)]">Are you sure?</span>
              <button
                type="button"
                onClick={handleClearAll}
                class="px-2.5 py-1.5 text-[11px] text-white bg-[var(--error)] rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
              >
                Yes, clear everything
              </button>
              <button
                type="button"
                onClick={() => setConfirmClear(false)}
                class="px-2.5 py-1.5 text-[11px] text-[var(--text-secondary)] bg-[var(--surface-raised)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
              >
                Cancel
              </button>
            </div>
          </Show>
        </div>
      </div>

      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <span class="text-[10px] font-mono text-[var(--text-muted)]">Estela v0.1.0-alpha</span>
      </div>
    </div>
  )
}

// ============================================================================
// Plugins Section
// ============================================================================

const PluginsSection: Component = () => (
  <div class="space-y-3">
    <p class="text-[10px] text-[var(--text-muted)]">
      Obsidian-style plugin ecosystem coming in Phase 2.
    </p>

    <div>
      <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
        Built-in Skills
      </h3>
      <div class="space-y-1">
        <For each={builtInSkills}>
          {(skill) => (
            <div
              class={`
                flex items-center gap-2.5 px-2.5 py-2
                rounded-[var(--radius-md)]
                border border-[var(--border-subtle)]
                ${skill.active ? 'bg-[var(--surface)]' : 'bg-[var(--surface-sunken)] opacity-60'}
              `}
            >
              <Puzzle class="w-3.5 h-3.5 flex-shrink-0 text-[var(--text-muted)]" />
              <div class="flex-1 min-w-0">
                <p class="text-xs text-[var(--text-primary)]">{skill.name}</p>
                <p class="text-[10px] text-[var(--text-muted)]">{skill.description}</p>
              </div>
              <span
                class={`w-1.5 h-1.5 rounded-full flex-shrink-0 ${skill.active ? 'bg-[var(--success)]' : 'bg-[var(--gray-6)]'}`}
              />
            </div>
          )}
        </For>
      </div>
    </div>
  </div>
)

// ============================================================================
// About Section
// ============================================================================

const AboutSection: Component = () => {
  const info: [string, string][] = [
    ['Runtime', 'Tauri v2 + SolidJS'],
    ['Language', 'TypeScript (strict)'],
    ['License', 'MIT'],
    ['Platform', 'Linux / macOS / Windows'],
  ]

  return (
    <div class="space-y-4">
      <div>
        <h3 class="text-sm font-semibold text-[var(--text-primary)]">Estela</h3>
        <p class="text-xs text-[var(--text-muted)] mt-1">
          Desktop AI coding app with a virtual dev team and community plugins.
        </p>
        <span class="inline-block mt-2 px-2 py-0.5 text-[10px] font-mono text-[var(--accent)] bg-[var(--accent-subtle)] rounded-[var(--radius-sm)]">
          v0.1.0-alpha
        </span>
      </div>

      <div class="space-y-0.5">
        <For each={info}>
          {([label, value]) => (
            <div class="flex items-center justify-between py-1.5">
              <span class="text-xs text-[var(--text-muted)]">{label}</span>
              <span class="text-xs text-[var(--text-primary)] font-mono">{value}</span>
            </div>
          )}
        </For>
      </div>

      <a
        href="https://github.com/estela-ai/estela"
        target="_blank"
        rel="noopener noreferrer"
        class="inline-flex items-center gap-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
      >
        Source code <ExternalLink class="w-3 h-3" />
      </a>
    </div>
  )
}

// ============================================================================
// Shared Components
// ============================================================================

const FieldGroup: Component<{ label: string; children: JSXElement }> = (props) => (
  <div>
    <span class="block text-xs font-medium text-[var(--text-muted)] mb-1.5">{props.label}</span>
    {props.children}
  </div>
)

// ============================================================================
// Agent Edit/Create Modal
// ============================================================================

const AgentEditModal: Component<{
  agent: AgentPreset
  isCreating: boolean
  onClose: () => void
  onSave: (agent: AgentPreset) => void
}> = (props) => {
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [name, setName] = createSignal(props.agent.name)
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [description, setDescription] = createSignal(props.agent.description)
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [model, setModel] = createSignal(props.agent.model || '')
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [capabilities, setCapabilities] = createSignal<string[]>([...props.agent.capabilities])
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [systemPrompt, setSystemPrompt] = createSignal(props.agent.systemPrompt || '')

  const toggleCapability = (cap: string) => {
    setCapabilities((prev) => (prev.includes(cap) ? prev.filter((c) => c !== cap) : [...prev, cap]))
  }

  const handleSave = () => {
    if (!name().trim()) return
    props.onSave({
      ...props.agent,
      name: name().trim(),
      description: description().trim(),
      model: model() || undefined,
      capabilities: capabilities(),
      systemPrompt: systemPrompt() || undefined,
    })
  }

  return (
    <div
      role="dialog"
      class="fixed inset-0 bg-black/60 flex items-center justify-center z-[60] p-4"
      onClick={(e) => e.target === e.currentTarget && props.onClose()}
      onKeyDown={(e) => e.key === 'Escape' && props.onClose()}
    >
      <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-lg shadow-xl max-h-[85vh] flex flex-col">
        <div class="flex items-center justify-between px-5 py-3 border-b border-[var(--border-subtle)] flex-shrink-0">
          <h2 class="text-sm font-semibold text-[var(--text-primary)]">
            {props.isCreating ? 'Create Agent' : 'Edit Agent'}
          </h2>
          <button
            type="button"
            onClick={() => props.onClose()}
            class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
          >
            <X class="w-4 h-4" />
          </button>
        </div>

        <div class="p-5 space-y-4 overflow-y-auto flex-1">
          <FieldGroup label="Name">
            <input
              type="text"
              value={name()}
              onInput={(e) => setName(e.currentTarget.value)}
              placeholder="e.g. Security Auditor"
              class="settings-input"
            />
          </FieldGroup>

          <FieldGroup label="Description">
            <textarea
              value={description()}
              onInput={(e) => setDescription(e.currentTarget.value)}
              placeholder="What does this agent specialize in?"
              rows={2}
              class="settings-input resize-none"
            />
          </FieldGroup>

          <FieldGroup label="Model">
            <select
              value={model()}
              onChange={(e) => setModel(e.currentTarget.value)}
              class="settings-input"
            >
              <For each={AVAILABLE_MODELS}>{(m) => <option value={m.id}>{m.label}</option>}</For>
            </select>
          </FieldGroup>

          <FieldGroup label="Capabilities">
            <div class="flex flex-wrap gap-1.5">
              <For each={ALL_CAPABILITIES}>
                {(cap) => {
                  const isActive = () => capabilities().includes(cap)
                  return (
                    <button
                      type="button"
                      onClick={() => toggleCapability(cap)}
                      class={`
                        px-2 py-0.5 text-[10px] rounded-[var(--radius-md)]
                        transition-colors duration-[var(--duration-fast)]
                        ${
                          isActive()
                            ? 'bg-[var(--accent)] text-white'
                            : 'bg-[var(--alpha-white-5)] text-[var(--text-tertiary)] hover:bg-[var(--alpha-white-8)] hover:text-[var(--text-secondary)]'
                        }
                      `}
                    >
                      {cap}
                    </button>
                  )
                }}
              </For>
            </div>
          </FieldGroup>

          <FieldGroup label="System Prompt (optional)">
            <textarea
              value={systemPrompt()}
              onInput={(e) => setSystemPrompt(e.currentTarget.value)}
              placeholder="Custom instructions for this agent..."
              rows={3}
              class="settings-input resize-none font-mono text-[11px]"
            />
          </FieldGroup>
        </div>

        <div class="flex items-center justify-end gap-2 px-5 py-3 border-t border-[var(--border-subtle)] flex-shrink-0">
          <button
            type="button"
            onClick={() => props.onClose()}
            class="px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)] rounded-[var(--radius-md)] transition-colors"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={handleSave}
            disabled={!name().trim()}
            class="px-3 py-1.5 text-xs bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white rounded-[var(--radius-md)] font-medium transition-colors disabled:opacity-50"
          >
            {props.isCreating ? 'Create' : 'Save'}
          </button>
        </div>
      </div>
    </div>
  )
}

// ============================================================================
// Keybinding Edit Modal
// ============================================================================

const KeybindingEditModal: Component<{
  keybinding: Keybinding
  onClose: () => void
  onSave: (kb: Keybinding) => void
}> = (props) => {
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [keys, setKeys] = createSignal<string[]>([...props.keybinding.keys])
  const [recording, setRecording] = createSignal(false)

  const startRecording = () => {
    setRecording(true)
    setKeys([])

    const handler = (e: KeyboardEvent) => {
      e.preventDefault()
      e.stopPropagation()

      const newKeys: string[] = []
      if (e.ctrlKey || e.metaKey) newKeys.push('meta')
      if (e.shiftKey) newKeys.push('shift')
      if (e.altKey) newKeys.push('alt')

      const key = e.key.toLowerCase()
      if (!['control', 'shift', 'alt', 'meta'].includes(key)) {
        newKeys.push(key)
      }

      if (newKeys.some((k) => !['meta', 'shift', 'alt'].includes(k))) {
        setKeys(newKeys)
        setRecording(false)
        document.removeEventListener('keydown', handler, true)
      }
    }

    document.addEventListener('keydown', handler, true)
  }

  const handleSave = () => {
    if (keys().length > 0) {
      props.onSave({ ...props.keybinding, keys: keys() })
    }
  }

  const formatKey = (key: string): string => {
    const keyMap: Record<string, string> = {
      meta: 'Ctrl',
      shift: 'Shift',
      alt: 'Alt',
      enter: 'Enter',
      escape: 'Esc',
      backspace: 'Bksp',
      arrowup: 'Up',
      arrowdown: 'Down',
      arrowleft: 'Left',
      arrowright: 'Right',
    }
    return keyMap[key] || key.toUpperCase()
  }

  return (
    <div
      role="dialog"
      class="fixed inset-0 bg-black/60 flex items-center justify-center z-[60] p-4"
      onClick={(e) => e.target === e.currentTarget && props.onClose()}
      onKeyDown={(e) => e.key === 'Escape' && props.onClose()}
    >
      <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-md shadow-xl">
        <div class="flex items-center justify-between px-5 py-3 border-b border-[var(--border-subtle)]">
          <h2 class="text-sm font-semibold text-[var(--text-primary)]">Edit Shortcut</h2>
          <button
            type="button"
            onClick={() => props.onClose()}
            class="p-1.5 rounded-[var(--radius-md)] text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)] transition-colors"
          >
            <X class="w-4 h-4" />
          </button>
        </div>

        <div class="p-5 space-y-4">
          <div>
            <p class="text-sm font-medium text-[var(--text-primary)]">{props.keybinding.action}</p>
            <p class="text-xs text-[var(--text-muted)] mt-0.5">{props.keybinding.description}</p>
          </div>

          <FieldGroup label="Shortcut">
            <button
              type="button"
              onClick={startRecording}
              class={`
                w-full px-4 py-3 text-center text-sm
                rounded-[var(--radius-lg)]
                border-2 border-dashed
                transition-colors duration-[var(--duration-fast)]
                ${
                  recording()
                    ? 'border-[var(--accent)] bg-[var(--accent-subtle)] text-[var(--accent)] animate-pulse'
                    : 'border-[var(--border-default)] bg-[var(--input-background)] text-[var(--text-primary)] hover:border-[var(--accent-muted)]'
                }
              `}
            >
              <Show
                when={!recording()}
                fallback={<span class="font-medium">Press your shortcut...</span>}
              >
                <div class="flex items-center justify-center gap-1">
                  <For each={keys()}>
                    {(key, index) => (
                      <>
                        <kbd class="px-2 py-1 bg-[var(--surface-raised)] border border-[var(--border-default)] rounded-md text-xs font-mono font-medium shadow-[0_1px_0_var(--alpha-black-20)]">
                          {formatKey(key)}
                        </kbd>
                        <Show when={index() < keys().length - 1}>
                          <span class="text-[var(--text-muted)]">+</span>
                        </Show>
                      </>
                    )}
                  </For>
                </div>
              </Show>
            </button>
            <p class="text-[10px] text-[var(--text-muted)] mt-1.5">
              Click to record a new shortcut
            </p>
          </FieldGroup>
        </div>

        <div class="flex items-center justify-end gap-2 px-5 py-3 border-t border-[var(--border-subtle)]">
          <button
            type="button"
            onClick={() => props.onClose()}
            class="px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)] rounded-[var(--radius-md)] transition-colors"
          >
            Cancel
          </button>
          <button
            type="button"
            onClick={handleSave}
            disabled={keys().length === 0}
            class="px-3 py-1.5 text-xs bg-[var(--accent)] hover:bg-[var(--accent-hover)] text-white rounded-[var(--radius-md)] font-medium transition-colors disabled:opacity-50"
          >
            Save
          </button>
        </div>
      </div>
    </div>
  )
}
