/**
 * LLM / Generation Settings Tab
 *
 * Uses shared settings components and theme tokens for consistency.
 * Replaced hardcoded hex colors with semantic CSS variables.
 */

import { Cpu, FileText, Minimize2, SlidersHorizontal, Tag, Timer } from 'lucide-solid'
import { type Component, createMemo, Show } from 'solid-js'
import { getCompactionModelOptions } from '../../../services/context-compaction'
import { useSettings } from '../../../stores/settings'
import { Toggle } from '../../ui/Toggle'
import {
  SettingsCard,
  SettingsLabelValue,
  SettingsPageTitle,
  SettingsSelect,
  SettingsTabContainer,
  SettingsTextarea,
} from '../shared-settings-components'
import { MODEL_PAIRS, WEAK_MODEL_OPTIONS } from './llm/llm-config'
import { ModelAliasesSection } from './model-aliases-section'

/* ------------------------------------------------------------------ */
/*  Shared slider row component using theme tokens                    */
/* ------------------------------------------------------------------ */

const SliderRow: Component<{
  label: string
  value: number
  min: number
  max: number
  step: number
  format?: (v: number) => string
  onChange: (v: number) => void
}> = (props) => {
  const display = () => (props.format ? props.format(props.value) : String(props.value))
  return (
    <SettingsLabelValue
      label={props.label}
      value={display()}
      rightContent={
        <input
          type="range"
          min={props.min}
          max={props.max}
          step={props.step}
          value={props.value}
          onInput={(e) => props.onChange(Number(e.currentTarget.value))}
          class="settings-slider"
          style={{ width: '140px' }}
        />
      }
    />
  )
}

/* ------------------------------------------------------------------ */
/*  Main Component                                                    */
/* ------------------------------------------------------------------ */

export const LLMTab: Component = () => {
  const { settings, updateSettings, updateGeneration, updateAgentLimits } = useSettings()
  const compactionModelOptions = createMemo(() => getCompactionModelOptions(settings()))

  // Model pairs kept for potential auto-suggest feature
  void MODEL_PAIRS

  return (
    <SettingsTabContainer>
      <SettingsPageTitle>LLM</SettingsPageTitle>

      {/* Generation Card */}
      <SettingsCard
        icon={SlidersHorizontal}
        title="Generation"
        description="Token limits and sampling parameters"
      >
        <SliderRow
          label="Max Tokens"
          value={settings().generation.maxTokens}
          min={256}
          max={32000}
          step={256}
          format={(v) => v.toLocaleString()}
          onChange={(v) => updateGeneration({ maxTokens: v })}
        />
        <SliderRow
          label="Temperature"
          value={settings().generation.temperature}
          min={0}
          max={2}
          step={0.1}
          format={(v) => v.toFixed(1)}
          onChange={(v) => updateGeneration({ temperature: v })}
        />
        <SliderRow
          label="Top P"
          value={settings().generation.topP}
          min={0}
          max={1}
          step={0.05}
          format={(v) => v.toFixed(2)}
          onChange={(v) => updateGeneration({ topP: v })}
        />
      </SettingsCard>

      {/* Secondary Model Card */}
      <SettingsCard
        icon={Cpu}
        title="Secondary Model"
        description="Cheaper model for planning, review, and summaries"
        compact
      >
        <SettingsSelect
          value={settings().generation.weakModel}
          onChange={(v) => updateGeneration({ weakModel: v })}
          options={WEAK_MODEL_OPTIONS}
          ariaLabel="Secondary model"
        />
      </SettingsCard>

      {/* Agent Limits Card */}
      <SettingsCard
        icon={Timer}
        title="Agent Limits"
        description="Maximum turns and time per agent run"
      >
        <SliderRow
          label="Max Turns"
          value={settings().agentLimits.agentMaxTurns}
          min={1}
          max={100}
          step={1}
          onChange={(v) => updateAgentLimits({ agentMaxTurns: v })}
        />
        <SliderRow
          label="Max Time"
          value={settings().agentLimits.agentMaxTimeMinutes}
          min={1}
          max={60}
          step={1}
          format={(v) => `${v}m`}
          onChange={(v) => updateAgentLimits({ agentMaxTimeMinutes: v })}
        />
      </SettingsCard>

      {/* Custom Instructions Card */}
      <SettingsCard
        icon={FileText}
        title="Custom Instructions"
        description="Prepended as a system message to every request"
        compact
      >
        <SettingsTextarea
          value={settings().generation.customInstructions}
          onInput={(v) => updateGeneration({ customInstructions: v })}
          placeholder="Always respond in TypeScript. Prefer functional patterns..."
          rows={4}
          ariaLabel="Custom instructions"
        />
      </SettingsCard>

      {/* Context Compaction Card */}
      <SettingsCard
        icon={Minimize2}
        title="Context Compaction"
        description="Automatic conversation compression"
        compact
      >
        <div class="flex items-center justify-between">
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'var(--font-sans)',
                'font-size': '13px',
                color: 'var(--text-secondary)',
              }}
            >
              Auto-compact conversation
            </span>
            <span
              style={{
                'font-family': 'var(--font-sans)',
                'font-size': '12px',
                color: 'var(--text-muted)',
              }}
            >
              Compress old messages when context window usage is high
            </span>
          </div>
          <Toggle
            checked={settings().generation.autoCompact}
            onChange={(v) => updateGeneration({ autoCompact: v })}
          />
        </div>
        <Show when={settings().generation.autoCompact}>
          <SliderRow
            label="Compaction threshold"
            value={settings().generation.compactionThreshold}
            min={60}
            max={95}
            step={5}
            format={(v) => `${v}%`}
            onChange={(v) =>
              updateSettings({
                generation: { ...settings().generation, compactionThreshold: v },
              })
            }
          />
          <div
            class="flex items-center justify-between"
            style={{
              'border-radius': '8px',
              background: 'var(--surface-sunken)',
              border: '1px solid var(--border-subtle)',
              padding: '12px',
              gap: '12px',
            }}
          >
            <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
              <span
                style={{
                  'font-family': 'var(--font-sans)',
                  'font-size': '13px',
                  color: 'var(--text-secondary)',
                }}
              >
                Compaction model
              </span>
              <span
                style={{
                  'font-family': 'var(--font-sans)',
                  'font-size': '12px',
                  color: 'var(--text-muted)',
                }}
              >
                Use the current chat model or a cheaper summarizer
              </span>
            </div>
            <SettingsSelect
              value={settings().generation.compactionModel}
              onChange={(v) => updateGeneration({ compactionModel: v })}
              options={compactionModelOptions()}
              ariaLabel="Compaction model"
            />
          </div>
        </Show>
      </SettingsCard>

      {/* Model Aliases Card */}
      <SettingsCard
        icon={Tag as Component<{ class?: string; style?: Record<string, string> }>}
        title="Model Aliases"
        description="Short names for frequently used models"
        compact
      >
        <ModelAliasesSection />
      </SettingsCard>
    </SettingsTabContainer>
  )
}
