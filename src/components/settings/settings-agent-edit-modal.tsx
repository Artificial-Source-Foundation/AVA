import { X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { AgentPreset, AgentTier } from '../../config/defaults/agent-defaults'
import { FieldGroup } from './settings-field-group'
import { ALL_CAPABILITIES, AVAILABLE_MODELS } from './settings-modal-config'

interface AgentEditModalProps {
  agent: AgentPreset
  isCreating: boolean
  onClose: () => void
  onSave: (agent: AgentPreset) => void
}

const TIER_OPTIONS: Array<{ value: AgentTier; label: string }> = [
  { value: 'worker', label: 'Worker' },
  { value: 'lead', label: 'Lead' },
  { value: 'commander', label: 'Commander' },
]

const DOMAIN_OPTIONS = ['', 'frontend', 'backend', 'testing', 'devops', 'fullstack']

const AVAILABLE_TOOLS = [
  'read_file',
  'write_file',
  'create_file',
  'delete_file',
  'edit',
  'bash',
  'grep',
  'glob',
  'ls',
  'question',
  'attempt_completion',
  'websearch',
  'webfetch',
  'repo_map',
]

const AVAILABLE_DELEGATES = [
  'coder',
  'tester',
  'reviewer',
  'researcher',
  'debugger',
  'architect',
  'planner',
  'devops',
  'frontend-lead',
  'backend-lead',
  'qa-lead',
  'fullstack-lead',
]

export const AgentEditModal: Component<AgentEditModalProps> = (props) => {
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
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [tier, setTier] = createSignal<AgentTier>(props.agent.tier ?? 'worker')
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [tools, setTools] = createSignal<string[]>([...(props.agent.tools ?? [])])
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [delegates, setDelegates] = createSignal<string[]>([...(props.agent.delegates ?? [])])
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [domain, setDomain] = createSignal(props.agent.domain || '')
  // eslint-disable-next-line solid/reactivity -- initial value for editing
  const [provider, setProvider] = createSignal(props.agent.provider || '')

  const isBuiltIn = () => !props.agent.isCustom && !props.isCreating

  const toggleCapability = (cap: string) => {
    setCapabilities((prev) => (prev.includes(cap) ? prev.filter((c) => c !== cap) : [...prev, cap]))
  }

  const toggleTool = (tool: string) => {
    setTools((prev) => (prev.includes(tool) ? prev.filter((t) => t !== tool) : [...prev, tool]))
  }

  const toggleDelegate = (id: string) => {
    setDelegates((prev) => (prev.includes(id) ? prev.filter((d) => d !== id) : [...prev, id]))
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
      tier: tier(),
      tools: tools().length > 0 ? tools() : undefined,
      delegates: delegates().length > 0 ? delegates() : undefined,
      domain: domain() || undefined,
      provider: provider() || undefined,
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

          {/* Tier */}
          <FieldGroup label="Tier">
            <select
              value={tier()}
              onChange={(e) => setTier(e.currentTarget.value as AgentTier)}
              class="settings-input"
              disabled={isBuiltIn()}
            >
              <For each={TIER_OPTIONS}>{(t) => <option value={t.value}>{t.label}</option>}</For>
            </select>
          </FieldGroup>

          {/* Domain */}
          <FieldGroup label="Domain">
            <select
              value={domain()}
              onChange={(e) => setDomain(e.currentTarget.value)}
              class="settings-input"
            >
              <For each={DOMAIN_OPTIONS}>{(d) => <option value={d}>{d || 'None'}</option>}</For>
            </select>
          </FieldGroup>

          {/* Model */}
          <FieldGroup label="Model">
            <select
              value={model()}
              onChange={(e) => setModel(e.currentTarget.value)}
              class="settings-input"
            >
              <For each={AVAILABLE_MODELS}>{(m) => <option value={m.id}>{m.label}</option>}</For>
            </select>
          </FieldGroup>

          {/* Provider */}
          <FieldGroup label="Provider (optional)">
            <input
              type="text"
              value={provider()}
              onInput={(e) => setProvider(e.currentTarget.value)}
              placeholder="e.g. openrouter, anthropic"
              class="settings-input"
            />
          </FieldGroup>

          {/* Tools — for workers */}
          <Show when={tier() === 'worker'}>
            <FieldGroup label="Tools">
              <div class="flex flex-wrap gap-1.5">
                <For each={AVAILABLE_TOOLS}>
                  {(tool) => {
                    const isActive = () => tools().includes(tool)
                    return (
                      <button
                        type="button"
                        onClick={() => toggleTool(tool)}
                        class={`px-2 py-0.5 text-[10px] rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)] ${isActive() ? 'bg-[var(--accent)] text-white' : 'bg-[var(--alpha-white-5)] text-[var(--text-tertiary)] hover:bg-[var(--alpha-white-8)] hover:text-[var(--text-secondary)]'}`}
                      >
                        {tool}
                      </button>
                    )
                  }}
                </For>
              </div>
            </FieldGroup>
          </Show>

          {/* Delegates — for leads and commander */}
          <Show when={tier() === 'lead' || tier() === 'commander'}>
            <FieldGroup label="Delegates to">
              <div class="flex flex-wrap gap-1.5">
                <For each={AVAILABLE_DELEGATES}>
                  {(id) => {
                    const isActive = () => delegates().includes(id)
                    return (
                      <button
                        type="button"
                        onClick={() => toggleDelegate(id)}
                        class={`px-2 py-0.5 text-[10px] rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)] ${isActive() ? 'bg-[var(--accent)] text-white' : 'bg-[var(--alpha-white-5)] text-[var(--text-tertiary)] hover:bg-[var(--alpha-white-8)] hover:text-[var(--text-secondary)]'}`}
                      >
                        {id}
                      </button>
                    )
                  }}
                </For>
              </div>
            </FieldGroup>
          </Show>

          {/* Capabilities */}
          <FieldGroup label="Capabilities">
            <div class="flex flex-wrap gap-1.5">
              <For each={ALL_CAPABILITIES}>
                {(cap) => {
                  const isActive = () => capabilities().includes(cap)
                  return (
                    <button
                      type="button"
                      onClick={() => toggleCapability(cap)}
                      class={`px-2 py-0.5 text-[10px] rounded-[var(--radius-md)] transition-colors duration-[var(--duration-fast)] ${isActive() ? 'bg-[var(--accent)] text-white' : 'bg-[var(--alpha-white-5)] text-[var(--text-tertiary)] hover:bg-[var(--alpha-white-8)] hover:text-[var(--text-secondary)]'}`}
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
