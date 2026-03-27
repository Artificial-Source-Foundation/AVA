/**
 * Agents Tab — Right Panel (Detail)
 *
 * System-prompt-first design with categorized tool/capability toggles.
 * Model dropdown shows all models from enabled providers.
 */

import { Copy, Info } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, For, Show } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { aggregateModels } from '../../dialogs/model-browser/model-browser-helpers'
import { SETTINGS_INPUT_CLASS } from '../settings-constants'
import { CategoryToggleSection } from './agents-detail/CategoryToggleSection'
import { ALL_TOOLS, CAPABILITY_CATEGORIES, TOOL_CATEGORIES } from './agents-tab-data'

export interface AgentsTabDetailProps {
  agent: AgentPreset | null
  providers: LLMProviderConfig[]
  onSave: (agent: AgentPreset) => void
  onDelete?: (id: string) => void
  isCreating?: boolean
}

const cls = SETTINGS_INPUT_CLASS

export const AgentsTabDetail: Component<AgentsTabDetailProps> = (props) => {
  const [name, setName] = createSignal('')
  const [description, setDescription] = createSignal('')
  const [systemPrompt, setSystemPrompt] = createSignal('')
  const [model, setModel] = createSignal('')
  const [provider, setProvider] = createSignal('')
  const [tools, setTools] = createSignal<string[]>([])
  const [capabilities, setCapabilities] = createSignal<string[]>([])
  const [loadedId, setLoadedId] = createSignal<string | null>(null)
  const [copied, setCopied] = createSignal(false)

  const loadAgent = (agent: AgentPreset) => {
    setName(agent.name)
    setDescription(agent.description)
    setSystemPrompt(agent.systemPrompt || '')
    setModel(agent.model || '')
    setProvider(agent.provider || '')
    setTools([...(agent.tools ?? [])])
    setCapabilities([...agent.capabilities])
    setLoadedId(agent.id)
  }

  createEffect(() => {
    const agent = props.agent
    if (agent && agent.id !== loadedId()) loadAgent(agent)
  })

  const modelGroups = createMemo(() => {
    const enabled = props.providers.filter((p) => p.enabled)
    const all = aggregateModels(enabled)
    const groups = new Map<string, Array<{ id: string; name: string }>>()
    for (const m of all) {
      if (!groups.has(m.providerName)) groups.set(m.providerName, [])
      groups.get(m.providerName)!.push({ id: m.id, name: m.name })
    }
    return groups
  })

  const enabledProviders = createMemo(() => props.providers.filter((p) => p.enabled))

  const toggle = (list: string[], setter: (v: string[]) => void, value: string) => {
    setter(list.includes(value) ? list.filter((v2) => v2 !== value) : [...list, value])
  }

  const handleSave = () => {
    const a = props.agent
    if (!a || !name().trim()) return
    props.onSave({
      ...a,
      name: name().trim(),
      description: description().trim(),
      systemPrompt: systemPrompt() || undefined,
      model: model() || undefined,
      provider: provider() || undefined,
      tools: tools().length > 0 ? tools() : undefined,
      capabilities: capabilities(),
    })
  }

  const handleCopyJson = () => {
    const a = props.agent
    if (!a) return
    const { icon: _, ...rest } = a
    navigator.clipboard.writeText(JSON.stringify(rest, null, 2))
    setCopied(true)
    setTimeout(() => setCopied(false), 1500)
  }

  // Adapt TOOL_CATEGORIES shape for CategoryToggleSection
  const toolCategories = TOOL_CATEGORIES.map((c) => ({ label: c.label, items: c.tools }))
  const capCategories = CAPABILITY_CATEGORIES.map((c) => ({ label: c.label, items: c.items }))

  return (
    <div class="h-full overflow-y-auto p-5">
      <Show
        when={props.agent}
        fallback={
          <div class="flex items-center justify-center h-full text-center">
            <div>
              <p class="text-[var(--settings-text-label)] text-[var(--text-muted)]">
                Select an agent to configure
              </p>
              <p class="text-[var(--settings-text-description)] text-[var(--text-muted)] mt-1 opacity-60">
                Or click "+ New" to create a custom agent
              </p>
            </div>
          </div>
        }
      >
        <div class="space-y-5 max-w-2xl">
          {/* Name + Description */}
          <div class="space-y-2">
            <input
              type="text"
              value={name()}
              onInput={(e) => setName(e.currentTarget.value)}
              placeholder="Agent name"
              class={`${cls} text-[var(--settings-text-label)] font-semibold`}
            />
            <input
              type="text"
              value={description()}
              onInput={(e) => setDescription(e.currentTarget.value)}
              placeholder="What does this agent specialize in?"
              class={cls}
            />
          </div>

          {/* System Prompt — hero section */}
          <div>
            <span class="text-[var(--settings-text-label)] font-medium text-[var(--text-secondary)] mb-1.5 block">
              System Prompt
            </span>
            <textarea
              value={systemPrompt()}
              onInput={(e) => setSystemPrompt(e.currentTarget.value)}
              placeholder="Define the agent's behavior, personality, and constraints. This is the most important field — it shapes how the agent thinks and acts."
              rows={12}
              class={`${cls} font-mono text-[var(--settings-text-button)] resize-y min-h-[160px]`}
            />
          </div>

          {/* Model + Provider */}
          <div class="grid grid-cols-2 gap-3">
            <div>
              <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)] mb-1 block">
                Model
              </span>
              <select value={model()} onChange={(e) => setModel(e.currentTarget.value)} class={cls}>
                <option value="">Use default</option>
                <For each={[...modelGroups().entries()]}>
                  {([providerName, models]) => (
                    <optgroup label={providerName}>
                      <For each={models}>{(m) => <option value={m.id}>{m.name}</option>}</For>
                    </optgroup>
                  )}
                </For>
              </select>
            </div>
            <div>
              <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)] mb-1 block">
                Provider
              </span>
              <select
                value={provider()}
                onChange={(e) => setProvider(e.currentTarget.value)}
                class={cls}
              >
                <option value="">Auto-detect</option>
                <For each={enabledProviders()}>{(p) => <option value={p.id}>{p.name}</option>}</For>
              </select>
            </div>
          </div>

          {/* Tools — categorized */}
          <CategoryToggleSection
            title="Tools"
            countLabel={`${tools().length}/${ALL_TOOLS.length}`}
            categories={toolCategories}
            selected={tools()}
            onToggle={(tool) => toggle(tools(), setTools, tool)}
            onSelectAll={() => setTools([...ALL_TOOLS])}
            onSelectNone={() => setTools([])}
          />

          {/* Capabilities — categorized */}
          <CategoryToggleSection
            title="Capabilities"
            countLabel={`${capabilities().length}`}
            categories={capCategories}
            selected={capabilities()}
            onToggle={(cap) => toggle(capabilities(), setCapabilities, cap)}
          />

          {/* Storage info */}
          <div class="flex items-start gap-2 p-3 rounded-[var(--radius-md)] bg-[var(--alpha-white-3)] border border-[var(--border-subtle)]">
            <Info class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0 mt-0.5" />
            <div class="text-[var(--settings-text-badge)] text-[var(--text-muted)] space-y-1 flex-1">
              <p>
                Agents are stored in app settings (localStorage). Use{' '}
                <span class="text-[var(--text-secondary)]">Export</span> from the list panel to
                create shareable JSON files for manual editing.
              </p>
              <p>
                Built-in HQ agents like <span class="text-[var(--text-secondary)]">Commander</span>{' '}
                and the domain leads are now honored by the desktop HQ runtime, so provider, model,
                and prompt changes here affect new HQ planning runs.
              </p>
              <button
                type="button"
                onClick={handleCopyJson}
                class="inline-flex items-center gap-1 text-[var(--accent)] hover:underline"
              >
                <Copy class="w-3 h-3" />
                {copied() ? 'Copied!' : 'Copy agent as JSON'}
              </button>
            </div>
          </div>

          {/* Actions */}
          <div class="flex items-center gap-2 pt-2 border-t border-[var(--border-subtle)]">
            <Show when={props.agent?.isCustom && props.onDelete}>
              <button
                type="button"
                onClick={() => props.onDelete?.(props.agent!.id)}
                class="px-3 py-1.5 text-xs text-[var(--error)] hover:bg-[var(--error)]/10 rounded-[var(--radius-md)] transition-colors"
              >
                Delete
              </button>
            </Show>
            <div class="flex-1" />
            <button
              type="button"
              onClick={() => {
                const a = props.agent
                if (a) loadAgent(a)
              }}
              class="px-3 py-1.5 text-xs text-[var(--text-secondary)] hover:text-[var(--text-primary)] rounded-[var(--radius-md)] transition-colors"
            >
              Reset
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
      </Show>
    </div>
  )
}
