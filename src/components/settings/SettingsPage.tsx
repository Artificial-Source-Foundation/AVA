/**
 * Settings Page — Full-Page Design
 *
 * Premium settings experience. Each section is a full-width page.
 * No sidebar — just a horizontal nav strip + scrollable content.
 * Sidebar auto-collapses when entering settings (handled by navigation store).
 */

import {
  ArrowLeft,
  Bot,
  Check,
  Code,
  ExternalLink,
  Heart,
  Info,
  Keyboard,
  Server,
  Sparkles,
  Terminal,
  Wand2,
  X,
  Zap,
} from 'lucide-solid'
import { type Component, createSignal, For, type JSXElement, Show } from 'solid-js'
import { useNavigation } from '../../stores/navigation'
import { useSettings } from '../../stores/settings'
import { type AgentPreset, AgentsTab } from './tabs/AgentsTab'
import { defaultKeybindings, type Keybinding, KeybindingsTab } from './tabs/KeybindingsTab'
import { defaultMCPServers, MCPServersTab } from './tabs/MCPServersTab'
import { ProvidersTab } from './tabs/ProvidersTab'

type SettingsTab = 'providers' | 'agents' | 'mcp' | 'keybindings' | 'about'

const tabs: { id: SettingsTab; label: string; icon: typeof Zap }[] = [
  { id: 'providers', label: 'Providers', icon: Zap },
  { id: 'agents', label: 'Agents', icon: Bot },
  { id: 'mcp', label: 'MCP Servers', icon: Server },
  { id: 'keybindings', label: 'Shortcuts', icon: Keyboard },
  { id: 'about', label: 'About', icon: Info },
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

// ============================================================================
// Available Capabilities
// ============================================================================

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

// ============================================================================
// Main Settings Page
// ============================================================================

export const SettingsPage: Component = () => {
  const { goToChat } = useNavigation()
  const { settings, updateProvider, updateAgent, addAgent, removeAgent } = useSettings()
  const [activeTab, setActiveTab] = createSignal<SettingsTab>('providers')
  const [saveStatus, setSaveStatus] = createSignal<'idle' | 'saved' | 'error'>('idle')

  const [mcpServers] = createSignal(defaultMCPServers)
  const [keybindings, setKeybindings] = createSignal(defaultKeybindings)

  const [editingAgent, setEditingAgent] = createSignal<AgentPreset | null>(null)
  const [editingKeybinding, setEditingKeybinding] = createSignal<Keybinding | null>(null)
  const [creatingAgent, setCreatingAgent] = createSignal(false)

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
    setKeybindings((prev) => prev.map((k) => (k.id === kb.id ? { ...kb, isCustom: true } : k)))
    setEditingKeybinding(null)
  }

  const handleResetKeybinding = (id: string) => {
    const original = defaultKeybindings.find((k) => k.id === id)
    if (original) {
      setKeybindings((prev) =>
        prev.map((k) => (k.id === id ? { ...original, isCustom: false } : k))
      )
    }
  }

  const handleResetAllKeybindings = () => {
    setKeybindings(defaultKeybindings.map((k) => ({ ...k, isCustom: false })))
  }

  return (
    <div class="h-full flex flex-col bg-[var(--background)]">
      {/* Top bar — back button + tab strip */}
      <div class="flex items-center gap-1 px-4 h-11 border-b border-[var(--border-subtle)] bg-[var(--gray-1)] flex-shrink-0">
        <button
          type="button"
          onClick={goToChat}
          class="
            flex items-center gap-2 px-2 py-1.5
            rounded-[var(--radius-md)]
            text-[var(--text-muted)]
            hover:text-[var(--text-primary)]
            hover:bg-[var(--alpha-white-5)]
            transition-colors duration-[var(--duration-fast)]
            text-xs font-medium
          "
        >
          <ArrowLeft class="w-3.5 h-3.5" />
          <span class="hidden sm:inline">Back</span>
        </button>

        <div class="w-px h-5 bg-[var(--border-subtle)] mx-1" />

        {/* Tab strip */}
        <div class="flex items-center gap-0.5 flex-1 overflow-x-auto scrollbar-none">
          <For each={tabs}>
            {(tab) => {
              const Icon = tab.icon
              return (
                <button
                  type="button"
                  onClick={() => setActiveTab(tab.id)}
                  class={`
                    flex items-center gap-1.5 px-3 py-1.5
                    text-xs font-medium
                    rounded-[var(--radius-md)]
                    transition-colors duration-[var(--duration-fast)]
                    whitespace-nowrap
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

        {/* Save button (inline, compact) */}
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
      </div>

      {/* Content — translateZ(0) forces GPU layer for smooth scroll in WebKitGTK */}
      <div class="flex-1 overflow-y-auto" style={{ transform: 'translateZ(0)' }}>
        <div class="max-w-2xl mx-auto px-6 py-8">
          <Show when={activeTab() === 'providers'}>
            <ProvidersTab
              providers={settings().providers}
              onToggle={(id, enabled) => updateProvider(id, { enabled })}
              onSaveApiKey={(id, key) => updateProvider(id, { apiKey: key, status: 'connected' })}
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
            />
          </Show>

          <Show when={activeTab() === 'agents'}>
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
            <MCPServersTab servers={mcpServers()} />
          </Show>

          <Show when={activeTab() === 'keybindings'}>
            <KeybindingsTab
              keybindings={keybindings()}
              onEdit={handleEditKeybinding}
              onReset={handleResetKeybinding}
              onResetAll={handleResetAllKeybindings}
            />
          </Show>

          <Show when={activeTab() === 'about'}>
            <AboutSection />
          </Show>
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
  )
}

// ============================================================================
// About Section — Premium Design
// ============================================================================

const AboutSection: Component = () => (
  <div class="space-y-8">
    {/* Hero card */}
    <div class="relative overflow-hidden rounded-2xl border border-[var(--border-subtle)] bg-gradient-to-b from-[var(--accent-subtle)] to-[var(--surface)]">
      <div class="absolute inset-0 bg-[radial-gradient(ellipse_at_top,var(--accent-muted),transparent_70%)] opacity-40" />
      <div class="relative text-center py-10 px-6">
        <div
          class="
            w-20 h-20 mx-auto mb-5
            rounded-2xl
            bg-[var(--accent)]
            flex items-center justify-center
            shadow-lg
          "
          style={{ 'box-shadow': '0 0 16px rgba(139, 92, 246, 0.2)' }}
        >
          <Sparkles class="w-10 h-10 text-white" />
        </div>
        <h2 class="text-2xl font-bold text-[var(--text-primary)] tracking-tight">Estela</h2>
        <p class="text-sm text-[var(--text-secondary)] mt-1.5 max-w-sm mx-auto leading-relaxed">
          The Obsidian of AI coding. A desktop-first multi-agent assistant with a virtual dev team
          and community plugin ecosystem.
        </p>
        <span
          class="
            inline-block mt-4 px-3 py-1
            text-[11px] font-[var(--font-ui-mono)] tracking-wide
            text-[var(--accent)]
            bg-[var(--accent-subtle)]
            border border-[var(--accent-muted)]
            rounded-full
          "
        >
          v0.1.0-alpha
        </span>
      </div>
    </div>

    {/* Stats grid */}
    <div class="grid grid-cols-3 gap-3">
      <StatCard label="Providers" value="12+" icon={<Zap class="w-4 h-4" />} />
      <StatCard label="Tools" value="19" icon={<Terminal class="w-4 h-4" />} />
      <StatCard label="Agents" value="5" icon={<Bot class="w-4 h-4" />} />
    </div>

    {/* Tech stack */}
    <div class="rounded-xl border border-[var(--border-subtle)] bg-[var(--surface-raised)] overflow-hidden">
      <div class="px-4 py-2.5 border-b border-[var(--border-subtle)]">
        <span class="text-xs font-medium text-[var(--text-muted)] uppercase tracking-wider">
          Stack
        </span>
      </div>
      <div class="divide-y divide-[var(--border-subtle)]">
        <InfoRow label="Runtime" value="Tauri v2 + SolidJS" />
        <InfoRow label="Agent Protocol" value="ACP + A2A" />
        <InfoRow label="Language" value="TypeScript (strict)" />
        <InfoRow label="License" value="MIT" />
        <InfoRow label="Platform" value="Linux / macOS / Windows" />
      </div>
    </div>

    {/* Philosophy */}
    <div class="rounded-xl border border-[var(--border-subtle)] bg-[var(--surface-raised)] p-5">
      <h3 class="text-sm font-semibold text-[var(--text-primary)] mb-3">Philosophy</h3>
      <div class="space-y-2.5 text-sm text-[var(--text-secondary)] leading-relaxed">
        <p>
          Desktop-first, not a web app in disguise. Multi-provider, not locked to one vendor. Open
          source with a plugin ecosystem anyone can extend.
        </p>
        <p>
          Built for experienced devs who want full control, vibe coders who want the magic, and
          plugin creators who want to shape the ecosystem.
        </p>
      </div>
    </div>

    {/* Links */}
    <div class="flex items-center justify-center gap-4 pb-4">
      <a
        href="https://github.com/estela-ai/estela"
        target="_blank"
        rel="noopener noreferrer"
        class="inline-flex items-center gap-1.5 text-sm text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
      >
        <Code class="w-4 h-4" />
        Source
        <ExternalLink class="w-3 h-3" />
      </a>
      <a
        href="https://github.com/estela-ai/estela/issues"
        target="_blank"
        rel="noopener noreferrer"
        class="inline-flex items-center gap-1.5 text-sm text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
      >
        Issues
        <ExternalLink class="w-3 h-3" />
      </a>
    </div>

    <div class="text-center pb-2">
      <p class="text-xs text-[var(--text-muted)] flex items-center justify-center gap-1">
        Made with <Heart class="w-3 h-3 text-[var(--error)]" /> for developers who think in code
      </p>
    </div>
  </div>
)

const StatCard: Component<{ label: string; value: string; icon: JSXElement }> = (props) => (
  <div class="text-center p-4 rounded-xl bg-[var(--surface-raised)] border border-[var(--border-subtle)]">
    <div class="w-8 h-8 mx-auto mb-2 rounded-lg bg-[var(--accent-subtle)] flex items-center justify-center text-[var(--accent)]">
      {props.icon}
    </div>
    <div class="text-lg font-bold text-[var(--text-primary)] font-[var(--font-ui-mono)]">
      {props.value}
    </div>
    <div class="text-[10px] text-[var(--text-muted)] uppercase tracking-wider mt-0.5">
      {props.label}
    </div>
  </div>
)

const InfoRow: Component<{ label: string; value: string }> = (props) => (
  <div class="flex items-center justify-between px-4 py-2.5">
    <span class="text-xs text-[var(--text-muted)]">{props.label}</span>
    <span class="text-xs text-[var(--text-primary)] font-medium font-[var(--font-ui-mono)]">
      {props.value}
    </span>
  </div>
)

// ============================================================================
// Agent Edit/Create Modal — Full CRUD
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
      class="fixed inset-0 bg-black/60 flex items-center justify-center z-50 p-4 animate-fade-in"
      onClick={(e) => e.target === e.currentTarget && props.onClose()}
      onKeyDown={(e) => e.key === 'Escape' && props.onClose()}
    >
      <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-lg shadow-xl animate-scale-in max-h-[85vh] flex flex-col">
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
// Keybinding Edit Modal — Record new shortcut
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
      class="fixed inset-0 bg-black/60 flex items-center justify-center z-50 p-4 animate-fade-in"
      onClick={(e) => e.target === e.currentTarget && props.onClose()}
      onKeyDown={(e) => e.key === 'Escape' && props.onClose()}
    >
      <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] w-full max-w-md shadow-xl animate-scale-in">
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

// ============================================================================
// Shared Components
// ============================================================================

const FieldGroup: Component<{ label: string; children: JSXElement }> = (props) => (
  <div>
    <span class="block text-xs font-medium text-[var(--text-muted)] mb-1.5">{props.label}</span>
    {props.children}
  </div>
)
