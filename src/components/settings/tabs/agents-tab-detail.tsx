/**
 * Agents Tab — Right Panel (Detail)
 *
 * System-prompt-first design with categorized tool/capability toggles.
 * Model dropdown shows all models from enabled providers.
 */

import { Copy, Info } from 'lucide-solid'
import { type Component, createMemo, createSignal, For, Show } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { aggregateModels } from '../../dialogs/model-browser/model-browser-helpers'
import { ALL_TOOLS, CAPABILITY_CATEGORIES, TOOL_CATEGORIES } from './agents-tab-data'

export interface AgentsTabDetailProps {
  agent: AgentPreset | null
  providers: LLMProviderConfig[]
  onSave: (agent: AgentPreset) => void
  onDelete?: (id: string) => void
  isCreating?: boolean
}

const cls =
  'w-full px-2.5 py-1.5 bg-[var(--input-background)] text-[var(--text-primary)] border border-[var(--input-border)] rounded-[var(--radius-md)] text-xs focus:outline-none focus:border-[var(--input-border-focus)]'

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

  const current = createMemo(() => {
    const a = props.agent
    if (a && a.id !== loadedId()) loadAgent(a)
    return a
  })

  // Show models from ALL enabled providers (not just those with API keys)
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
    const a = current()
    if (!a || !name().trim()) return
    props.onSave({
      ...a, // preserves tier, domain, delegates, icon, etc.
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
    const a = current()
    if (!a) return
    const { icon: _, ...rest } = a
    navigator.clipboard.writeText(JSON.stringify(rest, null, 2))
    setCopied(true)
    setTimeout(() => setCopied(false), 1500)
  }

  return (
    <div class="h-full overflow-y-auto p-5">
      <Show
        when={current()}
        fallback={
          <div class="flex items-center justify-center h-full text-center">
            <div>
              <p class="text-xs text-[var(--text-muted)]">Select an agent to configure</p>
              <p class="text-[10px] text-[var(--text-muted)] mt-1 opacity-60">
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
              class={`${cls} text-sm font-semibold`}
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
            <span class="text-[11px] font-medium text-[var(--text-secondary)] mb-1.5 block">
              System Prompt
            </span>
            <textarea
              value={systemPrompt()}
              onInput={(e) => setSystemPrompt(e.currentTarget.value)}
              placeholder="Define the agent's behavior, personality, and constraints. This is the most important field — it shapes how the agent thinks and acts."
              rows={12}
              class={`${cls} font-mono text-[11px] resize-y min-h-[160px]`}
            />
          </div>

          {/* Model + Provider */}
          <div class="grid grid-cols-2 gap-3">
            <div>
              <span class="text-[10px] text-[var(--text-muted)] mb-1 block">Model</span>
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
              <span class="text-[10px] text-[var(--text-muted)] mb-1 block">Provider</span>
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
          <div>
            <div class="flex items-center justify-between mb-2">
              <span class="text-[11px] font-medium text-[var(--text-secondary)]">
                Tools{' '}
                <span class="text-[var(--text-muted)] font-normal">
                  ({tools().length}/{ALL_TOOLS.length})
                </span>
              </span>
              <div class="flex gap-2">
                <button
                  type="button"
                  onClick={() => setTools([...ALL_TOOLS])}
                  class="text-[9px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
                >
                  All
                </button>
                <button
                  type="button"
                  onClick={() => setTools([])}
                  class="text-[9px] text-[var(--text-muted)] hover:text-[var(--accent)] transition-colors"
                >
                  None
                </button>
              </div>
            </div>
            <div class="space-y-1.5">
              <For each={TOOL_CATEGORIES}>
                {(cat) => (
                  <div class="flex items-start gap-2">
                    <span class="text-[9px] text-[var(--text-muted)] w-14 flex-shrink-0 pt-0.5 text-right">
                      {cat.label}
                    </span>
                    <div class="flex flex-wrap gap-1">
                      <For each={cat.tools}>
                        {(tool) => (
                          <Chip
                            label={tool}
                            active={tools().includes(tool)}
                            onClick={() => toggle(tools(), setTools, tool)}
                          />
                        )}
                      </For>
                    </div>
                  </div>
                )}
              </For>
            </div>
          </div>

          {/* Capabilities — categorized */}
          <div>
            <span class="text-[11px] font-medium text-[var(--text-secondary)] mb-2 block">
              Capabilities{' '}
              <span class="text-[var(--text-muted)] font-normal">({capabilities().length})</span>
            </span>
            <div class="space-y-1.5">
              <For each={CAPABILITY_CATEGORIES}>
                {(cat) => (
                  <div class="flex items-start gap-2">
                    <span class="text-[9px] text-[var(--text-muted)] w-14 flex-shrink-0 pt-0.5 text-right">
                      {cat.label}
                    </span>
                    <div class="flex flex-wrap gap-1">
                      <For each={cat.items}>
                        {(cap) => (
                          <Chip
                            label={cap}
                            active={capabilities().includes(cap)}
                            onClick={() => toggle(capabilities(), setCapabilities, cap)}
                          />
                        )}
                      </For>
                    </div>
                  </div>
                )}
              </For>
            </div>
          </div>

          {/* Storage info */}
          <div class="flex items-start gap-2 p-3 rounded-[var(--radius-md)] bg-[var(--alpha-white-3)] border border-[var(--border-subtle)]">
            <Info class="w-3.5 h-3.5 text-[var(--text-muted)] flex-shrink-0 mt-0.5" />
            <div class="text-[10px] text-[var(--text-muted)] space-y-1 flex-1">
              <p>
                Agents are stored in app settings (localStorage). Use{' '}
                <span class="text-[var(--text-secondary)]">Export</span> from the list panel to
                create shareable JSON files for manual editing.
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
            <Show when={current()?.isCustom && props.onDelete}>
              <button
                type="button"
                onClick={() => props.onDelete?.(current()!.id)}
                class="px-3 py-1.5 text-xs text-[var(--error)] hover:bg-[var(--error)]/10 rounded-[var(--radius-md)] transition-colors"
              >
                Delete
              </button>
            </Show>
            <div class="flex-1" />
            <button
              type="button"
              onClick={() => {
                const a = current()
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

// ============================================================================
// Chip Toggle — compact, toggleable label
// ============================================================================

const Chip: Component<{ label: string; active: boolean; onClick: () => void }> = (props) => (
  <button
    type="button"
    onClick={props.onClick}
    class={`px-1.5 py-0.5 text-[10px] rounded-[var(--radius-sm)] transition-colors ${
      props.active
        ? 'bg-[var(--accent)] text-white'
        : 'bg-[var(--alpha-white-5)] text-[var(--text-tertiary)] hover:bg-[var(--alpha-white-8)] hover:text-[var(--text-secondary)]'
    }`}
  >
    {props.label}
  </button>
)
