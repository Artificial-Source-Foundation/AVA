/**
 * Agents Tab — Right Panel (Detail)
 *
 * Pencil design: clean detail form with agent name as title, "Built-in" badge,
 * description subtitle, system prompt textarea, model dropdown, and save/reset buttons.
 */

import { type Component, createEffect, createMemo, createSignal, Show } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { useSession } from '../../../stores/session'
import { ModelPickerField } from '../../dialogs/model-browser/ModelPickerField'
import { buildModelSpec } from '../../dialogs/model-browser/model-browser-helpers'

export interface AgentsTabDetailProps {
  agent: AgentPreset | null
  providers: LLMProviderConfig[]
  onSave: (agent: AgentPreset) => void
  onDelete?: (id: string) => void
  isCreating?: boolean
}

export const AgentsTabDetail: Component<AgentsTabDetailProps> = (props) => {
  const { selectedModel, selectedProvider } = useSession()
  const [name, setName] = createSignal('')
  const [description, setDescription] = createSignal('')
  const [systemPrompt, setSystemPrompt] = createSignal('')
  const [model, setModel] = createSignal('')
  const [provider, setProvider] = createSignal('')
  const [tools, setTools] = createSignal<string[]>([])
  const [capabilities, setCapabilities] = createSignal<string[]>([])
  const [loadedId, setLoadedId] = createSignal<string | null>(null)

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

  const providerAccessor = () => props.providers
  const lastUsedModelSpec = createMemo(() =>
    selectedModel() ? buildModelSpec(selectedModel(), selectedProvider()) : ''
  )

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

  return (
    <div
      class="settings-scroll-area h-full min-h-0 overflow-y-auto"
      style={{
        'overscroll-behavior': 'contain',
        'scrollbar-gutter': 'stable',
        padding: '24px 32px',
      }}
    >
      <Show
        when={props.agent}
        fallback={
          <div class="flex items-center justify-center h-full text-center">
            <div>
              <p
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: '#C8C8CC',
                }}
              >
                Select an agent to configure
              </p>
              <p
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                  'margin-top': '4px',
                }}
              >
                Or click "+ New" to create a custom agent
              </p>
            </div>
          </div>
        }
      >
        <div style={{ display: 'flex', 'flex-direction': 'column', gap: '20px' }}>
          {/* Header: Title + Badge */}
          <div>
            <div class="flex items-center justify-between" style={{ width: '100%' }}>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '18px',
                  'font-weight': '600',
                  color: '#F5F5F7',
                }}
              >
                {name()}
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '10px',
                  'font-weight': '500',
                  color: '#0A84FF',
                  background: '#0A84FF18',
                  'border-radius': '6px',
                  padding: '3px 10px',
                }}
              >
                {props.agent?.isCustom ? 'Custom' : 'Built-in'}
              </span>
            </div>
            <p
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
                'margin-top': '4px',
              }}
            >
              {description()}
            </p>
          </div>

          {/* System Prompt */}
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '11px',
                'font-weight': '600',
                color: '#C8C8CC',
              }}
            >
              System Prompt
            </span>
            <div
              style={{
                'border-radius': '8px',
                background: '#ffffff08',
                border: '1px solid #ffffff0a',
                padding: '8px 12px',
                height: '120px',
                overflow: 'auto',
              }}
            >
              <textarea
                value={systemPrompt()}
                onInput={(e) => setSystemPrompt(e.currentTarget.value)}
                placeholder="Define the agent's behavior, personality, and constraints..."
                style={{
                  width: '100%',
                  height: '100%',
                  background: 'transparent',
                  border: 'none',
                  outline: 'none',
                  resize: 'none',
                  'font-family': 'Geist Mono, monospace',
                  'font-size': '11px',
                  color: '#48484A',
                  'line-height': '1.5',
                }}
              />
            </div>
          </div>

          {/* Model */}
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '11px',
                'font-weight': '600',
                color: '#C8C8CC',
              }}
            >
              Model
            </span>
            <ModelPickerField
              value={() => buildModelSpec(model(), provider() || null)}
              selectedProvider={() => provider() || null}
              providers={providerAccessor}
              fallbackValue={lastUsedModelSpec}
              autoLabel="Use default"
              buttonClass=""
              buttonStyle={{
                width: '100%',
                'border-radius': '8px',
                background: '#ffffff08',
                border: '1px solid #ffffff0a',
                padding: '8px 12px',
                'font-family': 'Geist Mono, monospace',
                'font-size': '12px',
                color: '#F5F5F7',
                cursor: 'pointer',
                'text-align': 'left',
              }}
              onSelect={(modelId, providerId) => {
                setModel(modelId)
                setProvider(providerId)
              }}
              onClear={() => {
                setModel('')
                setProvider('')
              }}
            />
          </div>

          {/* Action buttons */}
          <div class="flex items-center justify-end" style={{ gap: '8px' }}>
            <Show when={props.agent?.isCustom && props.onDelete}>
              <button
                type="button"
                onClick={() => props.onDelete?.(props.agent!.id)}
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#FF453A',
                  background: 'transparent',
                  border: 'none',
                  'border-radius': '8px',
                  padding: '6px 12px',
                  cursor: 'pointer',
                  'margin-right': 'auto',
                }}
              >
                Delete
              </button>
            </Show>
            <button
              type="button"
              onClick={() => {
                const a = props.agent
                if (a) loadAgent(a)
              }}
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#C8C8CC',
                background: 'transparent',
                border: 'none',
                'border-radius': '8px',
                padding: '6px 12px',
                cursor: 'pointer',
              }}
            >
              Reset
            </button>
            <button
              type="button"
              onClick={handleSave}
              disabled={!name().trim()}
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                'font-weight': '500',
                color: '#FFFFFF',
                background: '#0A84FF',
                border: 'none',
                'border-radius': '8px',
                padding: '6px 12px',
                cursor: 'pointer',
                opacity: name().trim() ? '1' : '0.5',
              }}
            >
              {props.isCreating ? 'Create' : 'Save'}
            </button>
          </div>
        </div>
      </Show>
    </div>
  )
}
