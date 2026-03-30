/**
 * HQ Settings Tab
 *
 * Matches the Pencil design: three cards (Director, Lead Execution, Review)
 * with icon headers, proper token styling, and toggle switches.
 */

import { Crown, ShieldCheck, UserCog } from 'lucide-solid'
import { type Component, createMemo, For, onMount } from 'solid-js'
import type { LLMProviderConfig } from '../../../config/defaults/provider-defaults'
import { useHq } from '../../../stores/hq'
import { useSession } from '../../../stores/session'
import { type LeadConfig, useSettings } from '../../../stores/settings'
import { ModelPickerField } from '../../dialogs/model-browser/ModelPickerField'
import { buildModelSpec } from '../../dialogs/model-browser/model-browser-helpers'
import { Toggle } from '../../ui/Toggle'

/* ------------------------------------------------------------------ */
/*  Shared design components                                          */
/* ------------------------------------------------------------------ */

const Card: Component<{ children: import('solid-js').JSX.Element; gap?: string }> = (props) => (
  <div
    style={{
      background: '#111114',
      'border-radius': '12px',
      border: '1px solid #ffffff08',
      padding: '20px',
      display: 'flex',
      'flex-direction': 'column',
      gap: props.gap ?? '16px',
    }}
  >
    {props.children}
  </div>
)

const CardHeader: Component<{
  icon: Component<{ class?: string; style?: Record<string, string> }>
  title: string
  description: string
  iconColor?: string
}> = (props) => (
  <div class="flex items-center" style={{ gap: '10px' }}>
    <props.icon
      style={{
        width: '16px',
        height: '16px',
        color: props.iconColor ?? '#C8C8CC',
        'flex-shrink': '0',
      }}
    />
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
      <span
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '14px',
          'font-weight': '500',
          color: '#F5F5F7',
        }}
      >
        {props.title}
      </span>
      <span
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '12px',
          color: '#48484A',
        }}
      >
        {props.description}
      </span>
    </div>
  </div>
)

const FieldLabel: Component<{ label: string }> = (props) => (
  <span
    style={{
      'font-family': 'Geist, sans-serif',
      'font-size': '11px',
      'font-weight': '600',
      color: '#C8C8CC',
    }}
  >
    {props.label}
  </span>
)

const ToneButton: Component<{
  label: string
  description: string
  active: boolean
  onClick: () => void
}> = (props) => (
  <button
    type="button"
    class="flex-1 flex flex-col text-left transition-colors"
    style={{
      gap: '4px',
      padding: '12px',
      'border-radius': '8px',
      background: props.active ? '#0A84FF18' : '#ffffff04',
      border: `1px solid ${props.active ? '#0A84FF40' : '#ffffff0a'}`,
      cursor: 'pointer',
    }}
    onClick={() => props.onClick()}
  >
    <span
      style={{
        'font-family': 'Geist, sans-serif',
        'font-size': '12px',
        'font-weight': '600',
        color: props.active ? '#F5F5F7' : '#C8C8CC',
      }}
    >
      {props.label}
    </span>
    <span
      style={{
        'font-family': 'Geist, sans-serif',
        'font-size': '11px',
        color: '#48484A',
        'line-height': '1.4',
      }}
    >
      {props.description}
    </span>
  </button>
)

const domainLabels: Record<string, string> = {
  backend: 'Backend',
  frontend: 'Frontend',
  qa: 'QA',
  research: 'Research',
  devops: 'DevOps',
  debug: 'Debug',
  fullstack: 'Full Stack',
}

function isAvailableProvider(provider: LLMProviderConfig): boolean {
  return provider.enabled || provider.status === 'connected'
}

const HqTab: Component = () => {
  const { settings, updateTeam } = useSettings()
  const { selectedModel, selectedProvider } = useSession()
  const { hqSettings, refreshAll, updateSettings } = useHq()

  const availableProviders = createMemo(() => settings().providers.filter(isAvailableProvider))
  const lastUsedModelSpec = createMemo(() =>
    selectedModel() ? buildModelSpec(selectedModel(), selectedProvider()) : ''
  )
  const directorFallbackModel = createMemo(
    () => settings().team.defaultDirectorModel || lastUsedModelSpec()
  )

  onMount(() => {
    void refreshAll()
  })

  const updateLead = (domain: string, patch: Partial<LeadConfig>): void => {
    updateTeam({
      leads: settings().team.leads.map((lead) =>
        lead.domain === domain ? { ...lead, ...patch } : lead
      ),
    })
  }

  return (
    <div style={{ display: 'flex', 'flex-direction': 'column', gap: '24px' }}>
      {/* Page title */}
      <h1
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '22px',
          'font-weight': '600',
          color: '#F5F5F7',
        }}
      >
        HQ
      </h1>

      {/* Director Card */}
      <Card>
        <CardHeader
          icon={Crown}
          title="Director"
          description="Control how HQ plans work, speaks to you, and supervises the team"
          iconColor="#F5A623"
        />
        <div class="flex" style={{ gap: '16px' }}>
          {/* Director Model */}
          <div class="flex-1" style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
            <FieldLabel label="Director Model" />
            <ModelPickerField
              value={() => hqSettings().directorModel}
              providers={availableProviders}
              fallbackValue={directorFallbackModel}
              autoLabel="Auto (strongest available)"
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
              onSelect={(modelId, providerId) =>
                void updateSettings({ directorModel: buildModelSpec(modelId, providerId) })
              }
              onClear={() => void updateSettings({ directorModel: '' })}
            />
          </div>
          {/* Communication Tone */}
          <div class="flex-1" style={{ display: 'flex', 'flex-direction': 'column', gap: '4px' }}>
            <FieldLabel label="Communication Tone" />
            <div class="flex" style={{ gap: '8px' }}>
              <ToneButton
                label="Technical"
                description="Detailed engineering language"
                active={hqSettings().tonePreference === 'technical'}
                onClick={() => void updateSettings({ tonePreference: 'technical' })}
              />
              <ToneButton
                label="Simple"
                description="Plain language for stakeholders"
                active={hqSettings().tonePreference === 'simple'}
                onClick={() => void updateSettings({ tonePreference: 'simple' })}
              />
            </div>
          </div>
        </div>
      </Card>

      {/* Lead Execution Card */}
      <Card gap="12px">
        <CardHeader
          icon={UserCog}
          title="Lead Execution"
          description="Active domains and parallel execution limits"
        />
        <For each={settings().team.leads}>
          {(lead) => (
            <div
              class="flex items-center justify-between"
              style={{
                'border-radius': '8px',
                background: '#ffffff04',
                border: '1px solid #ffffff0a',
                padding: '8px 12px',
              }}
            >
              <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
                <span
                  style={{
                    'font-family': 'Geist, sans-serif',
                    'font-size': '12px',
                    'font-weight': '600',
                    color: '#F5F5F7',
                  }}
                >
                  {domainLabels[lead.domain] ?? lead.domain} Lead
                </span>
                <span
                  style={{
                    'font-family': 'Geist, sans-serif',
                    'font-size': '11px',
                    color: '#48484A',
                  }}
                >
                  {lead.maxWorkers} concurrent worker slots
                </span>
              </div>
              <div class="flex items-center" style={{ gap: '12px' }}>
                <Toggle
                  checked={lead.enabled}
                  onChange={() => updateLead(lead.domain, { enabled: !lead.enabled })}
                />
              </div>
            </div>
          )}
        </For>
      </Card>

      {/* Review and Visibility Card */}
      <Card gap="12px">
        <CardHeader
          icon={ShieldCheck}
          title="Review and Visibility"
          description="QA review behavior and cost telemetry"
        />
        {/* Auto Review */}
        <div class="flex items-center justify-between">
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                color: '#C8C8CC',
              }}
            >
              Auto Review
            </span>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
              }}
            >
              Automatically add QA review after each phase
            </span>
          </div>
          <Toggle
            checked={hqSettings().autoReview}
            onChange={() => void updateSettings({ autoReview: !hqSettings().autoReview })}
          />
        </div>
        {/* Cost Visibility */}
        <div class="flex items-center justify-between">
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                color: '#C8C8CC',
              }}
            >
              Cost Visibility
            </span>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
              }}
            >
              Show exact PAYG spend when providers report it
            </span>
          </div>
          <Toggle
            checked={hqSettings().showCosts}
            onChange={() => void updateSettings({ showCosts: !hqSettings().showCosts })}
          />
        </div>
      </Card>
    </div>
  )
}

export { HqTab }
