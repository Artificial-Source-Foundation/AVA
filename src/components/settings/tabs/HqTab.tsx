/**
 * HQ Settings Tab
 *
 * Dedicated HQ settings surface for director behavior, runtime routing,
 * lead execution controls, and HQ-specific agent overrides.
 */

import { Bot, Crown, Layers, Plus, ShieldCheck, Trash2, UserCog, Wrench } from 'lucide-solid'
import { type Component, createEffect, createMemo, createSignal, For, Show } from 'solid-js'
import type { AgentPreset } from '../../../config/defaults/agent-defaults'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { useHq } from '../../../stores/hq'
import { type LeadConfig, useSettings } from '../../../stores/settings'
import { aggregateModels } from '../../dialogs/model-browser/model-browser-helpers'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'

type HqAgentRole = 'direct' | 'support'

const domainLabels: Record<string, string> = {
  backend: 'Backend',
  frontend: 'Frontend',
  qa: 'QA',
  research: 'Research',
  devops: 'DevOps',
  debug: 'Debug',
  fullstack: 'Full Stack',
}

const directRuntimeAgents = new Set([
  'commander',
  'frontend-lead',
  'backend-lead',
  'qa-lead',
  'research-lead',
  'debug-lead',
  'fullstack-lead',
  'devops-lead',
])

function roleForAgent(agent: AgentPreset): HqAgentRole {
  return directRuntimeAgents.has(agent.id) ? 'direct' : 'support'
}

function isAvailableProvider(provider: LLMProviderConfig): boolean {
  return provider.enabled || provider.status === 'connected'
}

function useAvailableModels(providers: () => LLMProviderConfig[]) {
  return createMemo(() => aggregateModels(providers().filter(isAvailableProvider)))
}

function summarizeRuntimeUse(agent: AgentPreset): string {
  if (agent.id === 'commander') return 'Directly controls HQ planning and orchestration.'
  if (agent.tier === 'lead') return 'Directly controls this HQ domain lead during new runs.'
  if (agent.id === 'coder') return 'Used as the default execution-model override for HQ workers.'
  if (agent.id === 'researcher' || agent.id === 'explorer') {
    return 'Used as the scout/research model fallback for HQ discovery work.'
  }
  return 'Available as a supporting HQ preset and future runtime expansion point.'
}

const HqAgentEditor: Component<{
  agent: AgentPreset | null
  providers: LLMProviderConfig[]
  onUpdateAgent: (id: string, patch: Partial<AgentPreset>) => void
}> = (props) => {
  const allModels = useAvailableModels(() => props.providers)
  const [loadedId, setLoadedId] = createSignal<string | null>(null)
  const [provider, setProvider] = createSignal('')
  const [model, setModel] = createSignal('')
  const [systemPrompt, setSystemPrompt] = createSignal('')

  createEffect(() => {
    const agent = props.agent
    if (!agent || agent.id === loadedId()) return
    setLoadedId(agent.id)
    setProvider(agent.provider ?? '')
    setModel(agent.model ?? '')
    setSystemPrompt(agent.systemPrompt ?? '')
  })

  const modelGroups = createMemo(() => {
    const groups = new Map<string, Array<{ id: string; name: string; providerId: string }>>()
    for (const entry of allModels()) {
      if (!groups.has(entry.providerName)) groups.set(entry.providerName, [])
      groups.get(entry.providerName)?.push({
        id: entry.id,
        name: entry.name,
        providerId: entry.providerId,
      })
    }
    return [...groups.entries()]
  })

  const runtimeRole = createMemo(() => (props.agent ? roleForAgent(props.agent) : 'support'))

  const save = (): void => {
    const agent = props.agent
    if (!agent) return
    props.onUpdateAgent(agent.id, {
      provider: provider() || undefined,
      model: model() || undefined,
      systemPrompt: systemPrompt().trim() || undefined,
    })
  }

  const clearOverrides = (): void => {
    const agent = props.agent
    if (!agent) return
    setProvider('')
    setModel('')
    setSystemPrompt('')
    props.onUpdateAgent(agent.id, {
      provider: undefined,
      model: undefined,
      systemPrompt: undefined,
    })
  }

  return (
    <Show
      when={props.agent}
      fallback={
        <div class="flex items-center justify-center h-full text-center px-6">
          <div>
            <p class="text-sm font-medium" style={{ color: 'var(--text-secondary)' }}>
              Select an HQ agent
            </p>
            <p class="text-xs mt-1" style={{ color: 'var(--text-muted)' }}>
              Edit the provider, model, and prompt HQ will use for that role.
            </p>
          </div>
        </div>
      }
    >
      {(agent) => (
        <div
          class="flex h-full min-h-0 flex-col gap-4 overflow-y-auto p-4"
          style={{ 'overscroll-behavior': 'contain', 'scrollbar-gutter': 'stable' }}
        >
          <div class="flex items-start justify-between gap-3">
            <div>
              <div class="flex items-center gap-2">
                <span class="text-sm font-semibold" style={{ color: 'var(--text-primary)' }}>
                  {agent().name}
                </span>
                <span
                  class="px-2 py-0.5 rounded-full text-[10px] font-semibold uppercase tracking-wide"
                  style={{
                    color: runtimeRole() === 'direct' ? 'var(--accent)' : 'var(--text-secondary)',
                    'background-color':
                      runtimeRole() === 'direct'
                        ? 'var(--accent-subtle)'
                        : 'rgba(255,255,255,0.04)',
                  }}
                >
                  {runtimeRole() === 'direct' ? 'Direct Runtime Role' : 'Support Role'}
                </span>
              </div>
              <p class="text-xs mt-1" style={{ color: 'var(--text-muted)' }}>
                {summarizeRuntimeUse(agent())}
              </p>
            </div>
            <button
              type="button"
              class="px-2.5 py-1.5 rounded-md text-[11px] font-medium"
              style={{
                color: 'var(--text-secondary)',
                border: '1px solid var(--border-subtle)',
                'background-color': 'rgba(255,255,255,0.02)',
              }}
              onClick={clearOverrides}
            >
              Reset Overrides
            </button>
          </div>

          <div class="grid grid-cols-1 md:grid-cols-2 gap-3">
            <Field label="Provider">
              <select
                value={provider()}
                class={INPUT_CLASS}
                onChange={(event) => setProvider(event.currentTarget.value)}
              >
                <option value="">Auto</option>
                <For each={props.providers.filter(isAvailableProvider)}>
                  {(entry) => <option value={entry.id}>{entry.name}</option>}
                </For>
              </select>
            </Field>
            <Field label="Model">
              <select
                value={model()}
                class={INPUT_CLASS}
                onChange={(event) => {
                  const nextModel = event.currentTarget.value
                  setModel(nextModel)
                  const entry = allModels().find((item) => item.id === nextModel)
                  if (entry && !provider()) setProvider(entry.providerId)
                }}
              >
                <option value="">Auto</option>
                <For each={modelGroups()}>
                  {([providerName, models]) => (
                    <optgroup label={providerName}>
                      <For each={models}>
                        {(entry) => <option value={entry.id}>{entry.name}</option>}
                      </For>
                    </optgroup>
                  )}
                </For>
              </select>
            </Field>
          </div>

          <Field label="System Prompt">
            <textarea
              value={systemPrompt()}
              rows={10}
              class={`${INPUT_CLASS} resize-y min-h-[160px] font-mono text-[11px]`}
              placeholder="Add HQ-specific instructions for this role..."
              onInput={(event) => setSystemPrompt(event.currentTarget.value)}
            />
          </Field>

          <div class="flex justify-end">
            <button
              type="button"
              class="px-3 py-1.5 rounded-md text-xs font-semibold"
              style={{ 'background-color': 'var(--accent)', color: 'white' }}
              onClick={save}
            >
              Save HQ Override
            </button>
          </div>
        </div>
      )}
    </Show>
  )
}

const INPUT_CLASS =
  'w-full h-9 px-2.5 rounded-md text-xs outline-none bg-[rgba(255,255,255,0.04)] border border-[var(--border-subtle)] text-[var(--text-primary)]'

const Field: Component<{ label: string; children: import('solid-js').JSX.Element }> = (props) => (
  <div class="flex flex-col gap-1.5">
    <span class="text-[11px] font-semibold" style={{ color: 'var(--text-secondary)' }}>
      {props.label}
    </span>
    {props.children}
  </div>
)

const ToneButton: Component<{
  label: string
  description: string
  active: boolean
  onClick: () => void
}> = (props) => (
  <button
    type="button"
    class="flex-1 flex flex-col gap-1 p-3 rounded-lg text-left transition-colors"
    style={{
      'background-color': props.active ? 'var(--accent-subtle)' : 'rgba(255,255,255,0.02)',
      border: `1px solid ${props.active ? 'var(--accent-border)' : 'var(--border-subtle)'}`,
    }}
    onClick={props.onClick}
  >
    <span
      class="text-xs font-semibold"
      style={{ color: props.active ? 'var(--text-primary)' : 'var(--text-secondary)' }}
    >
      {props.label}
    </span>
    <span class="text-[10px]" style={{ color: 'var(--text-muted)', 'line-height': '1.4' }}>
      {props.description}
    </span>
  </button>
)

const ToggleSwitch: Component<{ enabled: boolean; onToggle: () => void }> = (props) => (
  <button
    type="button"
    class="w-9 h-5 rounded-full transition-colors relative"
    style={{
      'background-color': props.enabled ? 'var(--accent)' : 'var(--border-default)',
    }}
    onClick={props.onToggle}
  >
    <div
      class="absolute top-0.5 w-4 h-4 rounded-full bg-white transition-transform"
      style={{ left: props.enabled ? '18px' : '2px' }}
    />
  </button>
)

const HqTab: Component = () => {
  const { settings, updateAgent, updateTeam } = useSettings()
  const { hqSettings, updateSettings } = useHq()
  const [selectedAgentId, setSelectedAgentId] = createSignal<string>('commander')
  const [newWorkerName, setNewWorkerName] = createSignal('')

  const hqAgents = createMemo(() => settings().agents.filter((agent) => agent.tier != null))
  const selectedAgent = createMemo(
    () => hqAgents().find((agent) => agent.id === selectedAgentId()) ?? hqAgents()[0] ?? null
  )

  createEffect(() => {
    const current = selectedAgentId()
    const exists = hqAgents().some((agent) => agent.id === current)
    if (!exists && hqAgents()[0]) setSelectedAgentId(hqAgents()[0]!.id)
  })

  const availableModelOptions = createMemo(() => {
    const models = [{ id: '', label: 'Auto (strongest available)' }]
    for (const provider of settings().providers) {
      if (!isAvailableProvider(provider)) continue
      for (const model of provider.models) {
        models.push({ id: model.id, label: `${provider.name} — ${model.name || model.id}` })
      }
    }
    return models
  })

  const updateLead = (domain: string, patch: Partial<LeadConfig>): void => {
    updateTeam({
      leads: settings().team.leads.map((lead) =>
        lead.domain === domain ? { ...lead, ...patch } : lead
      ),
    })
  }

  const addWorkerName = (): void => {
    const name = newWorkerName().trim()
    if (!name || settings().team.workerNames.includes(name)) return
    updateTeam({ workerNames: [...settings().team.workerNames, name] })
    setNewWorkerName('')
  }

  const removeWorkerName = (name: string): void => {
    updateTeam({ workerNames: settings().team.workerNames.filter((entry) => entry !== name) })
  }

  return (
    <div class="flex flex-col" style={{ gap: SETTINGS_CARD_GAP }}>
      <SettingsCard
        icon={Crown}
        title="Director"
        description="Control how HQ plans work, speaks to you, and supervises the team."
      >
        <div class="grid grid-cols-1 lg:grid-cols-2 gap-4">
          <Field label="Director Model">
            <select
              value={hqSettings().directorModel}
              class={INPUT_CLASS}
              onChange={(event) =>
                void updateSettings({ directorModel: event.currentTarget.value })
              }
            >
              <For each={availableModelOptions()}>
                {(entry) => <option value={entry.id}>{entry.label}</option>}
              </For>
            </select>
          </Field>
          <Field label="Communication Tone">
            <div class="flex gap-2">
              <ToneButton
                label="Technical"
                description="Detailed engineering language for builders"
                active={hqSettings().tonePreference === 'technical'}
                onClick={() => void updateSettings({ tonePreference: 'technical' })}
              />
              <ToneButton
                label="Simple"
                description="Plain language for quick stakeholder updates"
                active={hqSettings().tonePreference === 'simple'}
                onClick={() => void updateSettings({ tonePreference: 'simple' })}
              />
            </div>
          </Field>
        </div>
      </SettingsCard>

      <SettingsCard
        icon={Layers}
        title="Runtime Routing"
        description="Set fallback models and execution defaults that HQ uses when an individual agent does not have its own override."
      >
        <div class="grid grid-cols-1 lg:grid-cols-3 gap-3">
          <Field label="Default Lead Model">
            <select
              value={settings().team.defaultLeadModel}
              class={INPUT_CLASS}
              onChange={(event) => updateTeam({ defaultLeadModel: event.currentTarget.value })}
            >
              <For each={availableModelOptions()}>
                {(entry) => <option value={entry.id}>{entry.label}</option>}
              </For>
            </select>
          </Field>
          <Field label="Default Worker Model">
            <select
              value={settings().team.defaultWorkerModel}
              class={INPUT_CLASS}
              onChange={(event) => updateTeam({ defaultWorkerModel: event.currentTarget.value })}
            >
              <For each={availableModelOptions()}>
                {(entry) => <option value={entry.id}>{entry.label}</option>}
              </For>
            </select>
          </Field>
          <Field label="Default Scout Model">
            <select
              value={settings().team.defaultScoutModel}
              class={INPUT_CLASS}
              onChange={(event) => updateTeam({ defaultScoutModel: event.currentTarget.value })}
            >
              <For each={availableModelOptions()}>
                {(entry) => <option value={entry.id}>{entry.label}</option>}
              </For>
            </select>
          </Field>
        </div>
      </SettingsCard>

      <SettingsCard
        icon={UserCog}
        title="Lead Execution"
        description="Choose which lead domains are active and how much parallel execution each one can use."
      >
        <div class="space-y-2">
          <For each={settings().team.leads}>
            {(lead) => (
              <div
                class="flex items-center gap-4 justify-between rounded-lg px-3 py-2"
                style={{
                  'background-color': 'rgba(255,255,255,0.03)',
                  border: '1px solid var(--border-subtle)',
                }}
              >
                <div class="min-w-0">
                  <div class="text-xs font-semibold" style={{ color: 'var(--text-primary)' }}>
                    {domainLabels[lead.domain] ?? lead.domain} Lead
                  </div>
                  <div class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                    {lead.maxWorkers} concurrent worker slots
                  </div>
                </div>
                <div class="flex items-center gap-3">
                  <input
                    type="range"
                    min={1}
                    max={6}
                    step={1}
                    value={lead.maxWorkers}
                    class="w-24 accent-[var(--accent)]"
                    onInput={(event) =>
                      updateLead(lead.domain, { maxWorkers: Number(event.currentTarget.value) })
                    }
                  />
                  <ToggleSwitch
                    enabled={lead.enabled}
                    onToggle={() => updateLead(lead.domain, { enabled: !lead.enabled })}
                  />
                </div>
              </div>
            )}
          </For>
        </div>
      </SettingsCard>

      <SettingsCard
        icon={Wrench}
        title="Worker Pool"
        description="These names are used when HQ spawns workers during execution."
      >
        <div class="flex flex-wrap gap-2">
          <For each={settings().team.workerNames}>
            {(name) => (
              <span
                class="inline-flex items-center gap-1 px-2.5 py-1 rounded-full text-[11px]"
                style={{
                  'background-color': 'rgba(255,255,255,0.04)',
                  color: 'var(--text-secondary)',
                  border: '1px solid var(--border-subtle)',
                }}
              >
                {name}
                <button
                  type="button"
                  aria-label={`Remove ${name}`}
                  onClick={() => removeWorkerName(name)}
                >
                  <Trash2 size={12} />
                </button>
              </span>
            )}
          </For>
        </div>
        <div class="flex gap-2 mt-3">
          <input
            type="text"
            value={newWorkerName()}
            placeholder="Add worker name..."
            class={INPUT_CLASS}
            onInput={(event) => setNewWorkerName(event.currentTarget.value)}
            onKeyDown={(event) => {
              if (event.key === 'Enter') addWorkerName()
            }}
          />
          <button
            type="button"
            class="px-3 py-1.5 rounded-md text-xs font-semibold inline-flex items-center gap-1.5"
            style={{ 'background-color': 'var(--accent)', color: 'white' }}
            onClick={addWorkerName}
            disabled={!newWorkerName().trim()}
          >
            <Plus size={14} />
            Add
          </button>
        </div>
      </SettingsCard>

      <SettingsCard
        icon={Bot}
        title="HQ Agents"
        description="A dedicated HQ-only view of the roles that matter during planning and execution. Edit overrides here instead of hunting through generic agent presets."
      >
        <div
          class="flex min-h-0 -mx-6 -mb-6 rounded-b-[var(--radius-xl)] overflow-hidden"
          style={{ height: '520px' }}
        >
          <div
            class="w-[36%] min-w-[260px] min-h-0 border-r overflow-y-auto"
            style={{
              'border-color': 'var(--border-subtle)',
              'background-color': 'rgba(255,255,255,0.02)',
              'overscroll-behavior': 'contain',
              'scrollbar-gutter': 'stable',
            }}
          >
            <For each={hqAgents()}>
              {(agent) => {
                const runtimeRole = roleForAgent(agent)
                const isSelected = () => selectedAgentId() === agent.id
                return (
                  <button
                    type="button"
                    class="w-full flex items-start gap-3 px-4 py-3 text-left transition-colors"
                    style={{
                      'background-color': isSelected() ? 'var(--accent-subtle)' : 'transparent',
                      'border-bottom': '1px solid var(--border-subtle)',
                    }}
                    onClick={() => setSelectedAgentId(agent.id)}
                  >
                    <div class="mt-0.5">
                      <agent.icon class="w-4 h-4" />
                    </div>
                    <div class="min-w-0 flex-1">
                      <div class="flex items-center gap-2">
                        <span
                          class="text-xs font-semibold"
                          style={{ color: 'var(--text-primary)' }}
                        >
                          {agent.name}
                        </span>
                        <span
                          class="px-1.5 py-0.5 rounded-full text-[9px] font-semibold uppercase"
                          style={{
                            color:
                              runtimeRole === 'direct' ? 'var(--accent)' : 'var(--text-secondary)',
                            'background-color':
                              runtimeRole === 'direct'
                                ? 'rgba(99,102,241,0.12)'
                                : 'rgba(255,255,255,0.05)',
                          }}
                        >
                          {runtimeRole === 'direct' ? 'Direct' : 'Support'}
                        </span>
                      </div>
                      <div class="text-[11px] mt-1" style={{ color: 'var(--text-muted)' }}>
                        {agent.description}
                      </div>
                    </div>
                  </button>
                )
              }}
            </For>
          </div>
          <div class="flex-1 min-w-0 min-h-0 overflow-hidden">
            <HqAgentEditor
              agent={selectedAgent()}
              providers={settings().providers}
              onUpdateAgent={updateAgent}
            />
          </div>
        </div>
      </SettingsCard>

      <SettingsCard
        icon={ShieldCheck}
        title="Review and Visibility"
        description="Control QA review behavior and whether HQ shows PAYG cost telemetry in the UI."
      >
        <div class="space-y-3">
          <div class="flex items-center justify-between gap-4">
            <div>
              <div class="text-xs font-semibold" style={{ color: 'var(--text-primary)' }}>
                Auto Review
              </div>
              <div class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                Automatically add QA review after each phase.
              </div>
            </div>
            <ToggleSwitch
              enabled={hqSettings().autoReview}
              onToggle={() => void updateSettings({ autoReview: !hqSettings().autoReview })}
            />
          </div>
          <div class="flex items-center justify-between gap-4">
            <div>
              <div class="text-xs font-semibold" style={{ color: 'var(--text-primary)' }}>
                Cost Visibility
              </div>
              <div class="text-[11px]" style={{ color: 'var(--text-muted)' }}>
                Show exact PAYG spend when providers report it. Subscription providers stay
                excluded.
              </div>
            </div>
            <ToggleSwitch
              enabled={hqSettings().showCosts}
              onToggle={() => void updateSettings({ showCosts: !hqSettings().showCosts })}
            />
          </div>
        </div>
      </SettingsCard>
    </div>
  )
}

export { HqTab }
