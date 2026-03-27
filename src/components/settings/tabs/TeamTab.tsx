/**
 * Team Settings Tab
 *
 * Configure HQ multi-agent team: enable/disable team mode,
 * model selection for Director/Lead/Worker/Scout roles,
 * worker name pool, and per-lead domain configuration.
 */

import { Crown, Plus, Trash2, UserCog, Users, Wrench } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import type { LeadConfig } from '../../../stores/settings'
import { useSettings } from '../../../stores/settings'
import { ToggleRow } from '../../ui/ToggleRow'
import { SettingsCard } from '../SettingsCard'
import { SETTINGS_CARD_GAP } from '../settings-constants'

// ── Helpers ──────────────────────────────────────────────────────────────────

/** Collect all model IDs from connected providers */
function useAvailableModels(): () => { id: string; label: string }[] {
  const { settings } = useSettings()
  return () => {
    const models: { id: string; label: string }[] = [
      { id: '', label: 'Auto (strongest available)' },
    ]
    for (const p of settings().providers) {
      if (!p.enabled && p.status !== 'connected') continue
      for (const m of p.models) {
        models.push({ id: m.id, label: `${m.name || m.id}` })
      }
    }
    return models
  }
}

const domainLabels: Record<string, string> = {
  backend: 'Backend',
  frontend: 'Frontend',
  qa: 'QA',
  research: 'Research',
  devops: 'DevOps',
  debug: 'Debug',
  fullstack: 'Full Stack',
}

// ── Sub-components ───────────────────────────────────────────────────────────

const ModelSelect: Component<{
  label: string
  description?: string
  value: string
  onChange: (v: string) => void
  models: { id: string; label: string }[]
}> = (props) => (
  <div class="flex items-center justify-between py-2 gap-4">
    <div class="flex flex-col min-w-0">
      <span class="text-[var(--settings-text-label)] text-[var(--gray-10)] leading-tight">
        {props.label}
      </span>
      <Show when={props.description}>
        <span class="text-[var(--settings-text-description)] text-[var(--gray-7)] leading-tight mt-0.5">
          {props.description}
        </span>
      </Show>
    </div>
    <select
      value={props.value}
      onChange={(e) => props.onChange(e.currentTarget.value)}
      class="min-w-[200px] max-w-[280px] px-3 py-1.5 rounded-[var(--radius-md)] bg-[var(--gray-2)] border border-[var(--gray-5)] text-[var(--settings-text-description)] text-[var(--text-primary)] outline-none focus:border-[var(--accent)] transition-colors"
    >
      <For each={props.models}>{(m) => <option value={m.id}>{m.label}</option>}</For>
    </select>
  </div>
)

const LeadRow: Component<{
  lead: LeadConfig
  models: { id: string; label: string }[]
  onUpdate: (patch: Partial<LeadConfig>) => void
}> = (props) => {
  const [expanded, setExpanded] = createSignal(false)

  return (
    <div class="rounded-[var(--radius-md)] border border-[var(--gray-5)] bg-[var(--gray-2)] overflow-hidden">
      {/* Header row */}
      <div class="flex items-center gap-3 px-4 py-3">
        <button
          type="button"
          onClick={() => setExpanded((v) => !v)}
          class="flex-1 flex items-center gap-3 text-left"
        >
          <span
            class="text-[var(--settings-text-label)] font-medium"
            classList={{
              'text-[var(--text-primary)]': props.lead.enabled,
              'text-[var(--gray-7)]': !props.lead.enabled,
            }}
          >
            {domainLabels[props.lead.domain] ?? props.lead.domain} Lead
          </span>
          <Show when={props.lead.model}>
            <span class="text-[var(--settings-text-input)] text-[var(--gray-7)] px-1.5 py-0.5 rounded bg-[var(--gray-4)]">
              {props.lead.model}
            </span>
          </Show>
        </button>
        <span class="text-[var(--settings-text-input)] text-[var(--gray-7)]">
          {props.lead.maxWorkers}w
        </span>
        <label class="relative inline-flex items-center cursor-pointer">
          <input
            type="checkbox"
            checked={props.lead.enabled}
            onChange={(e) => props.onUpdate({ enabled: e.currentTarget.checked })}
            class="sr-only peer"
          />
          <div class="w-9 h-5 rounded-full bg-[var(--gray-5)] peer-checked:bg-[var(--accent)] after:content-[''] after:absolute after:top-0.5 after:left-0.5 after:bg-white after:rounded-full after:h-4 after:w-4 after:transition-transform peer-checked:after:translate-x-4 transition-colors" />
        </label>
      </div>

      {/* Expanded details */}
      <Show when={expanded()}>
        <div class="px-4 pb-4 pt-1 space-y-3 border-t border-[var(--gray-4)]">
          <ModelSelect
            label="Model override"
            description="Leave empty to use the default lead model"
            value={props.lead.model}
            onChange={(v) => props.onUpdate({ model: v })}
            models={props.models}
          />

          <div class="flex items-center justify-between py-2 gap-4">
            <div class="flex flex-col min-w-0">
              <span class="text-[var(--settings-text-label)] text-[var(--gray-10)] leading-tight">
                Max workers
              </span>
              <span class="text-[var(--settings-text-description)] text-[var(--gray-7)] leading-tight mt-0.5">
                Maximum concurrent workers for this lead
              </span>
            </div>
            <div class="flex items-center gap-2">
              <input
                type="range"
                min={1}
                max={6}
                step={1}
                value={props.lead.maxWorkers}
                onInput={(e) => props.onUpdate({ maxWorkers: Number(e.currentTarget.value) })}
                class="w-24 accent-[var(--accent)]"
              />
              <span class="text-[var(--settings-text-description)] font-mono text-[var(--gray-8)] w-6 text-right">
                {props.lead.maxWorkers}
              </span>
            </div>
          </div>

          <div class="space-y-1.5">
            <span class="text-[var(--settings-text-label)] text-[var(--gray-10)] leading-tight">
              Custom prompt
            </span>
            <textarea
              value={props.lead.customPrompt}
              onInput={(e) => props.onUpdate({ customPrompt: e.currentTarget.value })}
              placeholder="Additional system prompt for this lead..."
              rows={3}
              class="w-full px-3 py-2 rounded-[var(--radius-md)] bg-[var(--gray-2)] border border-[var(--gray-5)] text-[var(--settings-text-description)] text-[var(--text-primary)] placeholder:text-[var(--gray-6)] outline-none focus:border-[var(--accent)] resize-y transition-colors"
            />
          </div>
        </div>
      </Show>
    </div>
  )
}

// ── Main Tab ─────────────────────────────────────────────────────────────────

export const TeamTab: Component = () => {
  const { settings, updateTeam } = useSettings()
  const models = useAvailableModels()
  const [newName, setNewName] = createSignal('')

  const team = () => settings().team

  const updateLead = (domain: string, patch: Partial<LeadConfig>): void => {
    const updated = team().leads.map((l) => (l.domain === domain ? { ...l, ...patch } : l))
    updateTeam({ leads: updated })
  }

  const addWorkerName = (): void => {
    const name = newName().trim()
    if (!name || team().workerNames.includes(name)) return
    updateTeam({ workerNames: [...team().workerNames, name] })
    setNewName('')
  }

  const removeWorkerName = (name: string): void => {
    updateTeam({ workerNames: team().workerNames.filter((n) => n !== name) })
  }

  return (
    <div class="grid grid-cols-1" style={{ gap: SETTINGS_CARD_GAP }}>
      {/* Enable team mode */}
      <SettingsCard
        icon={Users}
        title="Team Mode"
        description="Enable HQ multi-agent orchestration"
      >
        <ToggleRow
          label="Enable Team Mode"
          description="Use Director, Leads, and Workers for complex tasks"
          checked={team().enabled}
          onChange={(v) => updateTeam({ enabled: v })}
        />
      </SettingsCard>

      {/* Model routing */}
      <Show when={team().enabled}>
        <SettingsCard
          icon={Crown}
          title="Model Routing"
          description="Assign models to each role in the hierarchy"
        >
          <ModelSelect
            label="Director"
            description="Strongest model for planning and coordination"
            value={team().defaultDirectorModel}
            onChange={(v) => updateTeam({ defaultDirectorModel: v })}
            models={models()}
          />
          <ModelSelect
            label="Lead"
            description="Strong model for task management"
            value={team().defaultLeadModel}
            onChange={(v) => updateTeam({ defaultLeadModel: v })}
            models={models()}
          />
          <ModelSelect
            label="Worker"
            description="Mid-tier model for task execution"
            value={team().defaultWorkerModel}
            onChange={(v) => updateTeam({ defaultWorkerModel: v })}
            models={models()}
          />
          <ModelSelect
            label="Scout"
            description="Cheapest model for codebase analysis"
            value={team().defaultScoutModel}
            onChange={(v) => updateTeam({ defaultScoutModel: v })}
            models={models()}
          />
        </SettingsCard>

        {/* Lead configuration */}
        <SettingsCard
          icon={UserCog}
          title="Leads"
          description="Configure domain leads, their models, and worker limits"
        >
          <div class="space-y-2">
            <For each={team().leads}>
              {(lead) => (
                <LeadRow
                  lead={lead}
                  models={models()}
                  onUpdate={(patch) => updateLead(lead.domain, patch)}
                />
              )}
            </For>
          </div>
        </SettingsCard>

        {/* Worker names */}
        <SettingsCard
          icon={Wrench}
          title="Worker Names"
          description="Name pool for spawned workers"
        >
          <div class="flex flex-wrap gap-2">
            <For each={team().workerNames}>
              {(name) => (
                <span class="inline-flex items-center gap-1 px-2.5 py-1 rounded-full bg-[var(--gray-4)] text-[var(--settings-text-description)] text-[var(--text-secondary)] border border-[var(--gray-5)]">
                  {name}
                  <button
                    type="button"
                    onClick={() => removeWorkerName(name)}
                    class="p-0.5 rounded-full hover:bg-[var(--gray-6)] text-[var(--gray-7)] hover:text-[var(--text-primary)] transition-colors"
                    aria-label={`Remove ${name}`}
                  >
                    <Trash2 class="w-3 h-3" />
                  </button>
                </span>
              )}
            </For>
          </div>
          <div class="flex gap-2 mt-2">
            <input
              type="text"
              value={newName()}
              onInput={(e) => setNewName(e.currentTarget.value)}
              onKeyDown={(e) => {
                if (e.key === 'Enter') addWorkerName()
              }}
              placeholder="Add a name..."
              class="flex-1 px-3 py-1.5 rounded-[var(--radius-md)] bg-[var(--gray-2)] border border-[var(--gray-5)] text-[var(--settings-text-description)] text-[var(--text-primary)] placeholder:text-[var(--gray-6)] outline-none focus:border-[var(--accent)] transition-colors"
            />
            <button
              type="button"
              onClick={addWorkerName}
              disabled={!newName().trim()}
              class="px-3 py-1.5 rounded-[var(--radius-md)] bg-[var(--accent)] text-white text-[var(--settings-text-description)] font-medium disabled:opacity-40 hover:opacity-90 transition-opacity flex items-center gap-1.5"
            >
              <Plus class="w-3.5 h-3.5" />
              Add
            </button>
          </div>
        </SettingsCard>
      </Show>
    </div>
  )
}
