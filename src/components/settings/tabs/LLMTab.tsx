/**
 * LLM / Generation Settings Tab
 *
 * Matches the Pencil design: cards with icon headers for Generation,
 * Secondary Model, Agent Limits, Custom Instructions, and Context Compaction.
 * Uses #111114 card surfaces, #ffffff08 borders, and Geist typography.
 */

import { Cpu, FileText, Minimize2, SlidersHorizontal, Tag, Timer } from 'lucide-solid'
import { type Component, createMemo, For, Show } from 'solid-js'
import { getCompactionModelOptions } from '../../../services/context-compaction'
import { useSettings } from '../../../stores/settings'
import { Toggle } from '../../ui/Toggle'
import { MODEL_PAIRS, SliderRow, WEAK_MODEL_OPTIONS } from './llm/llm-config'
import { ModelAliasesSection } from './model-aliases-section'

/* ------------------------------------------------------------------ */
/*  Shared components matching Pencil design tokens                   */
/* ------------------------------------------------------------------ */

const CardHeader: Component<{
  icon: Component<{ class?: string; style?: Record<string, string> }>
  title: string
  description: string
}> = (props) => (
  <div class="flex items-center" style={{ gap: '10px' }}>
    <props.icon style={{ width: '16px', height: '16px', color: '#C8C8CC', 'flex-shrink': '0' }} />
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

const Card: Component<{ children: import('solid-js').JSX.Element }> = (props) => (
  <div
    style={{
      background: '#111114',
      'border-radius': '12px',
      border: '1px solid #ffffff08',
      padding: '20px',
      display: 'flex',
      'flex-direction': 'column',
      gap: '16px',
    }}
  >
    {props.children}
  </div>
)

const CardSmallGap: Component<{ children: import('solid-js').JSX.Element }> = (props) => (
  <div
    style={{
      background: '#111114',
      'border-radius': '12px',
      border: '1px solid #ffffff08',
      padding: '20px',
      display: 'flex',
      'flex-direction': 'column',
      gap: '12px',
    }}
  >
    {props.children}
  </div>
)

export const LLMTab: Component = () => {
  const { settings, updateSettings, updateGeneration, updateAgentLimits } = useSettings()
  const compactionModelOptions = createMemo(() => getCompactionModelOptions(settings()))

  // Model pairs kept for potential auto-suggest feature
  void MODEL_PAIRS

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
        LLM
      </h1>

      {/* Generation Card */}
      <Card>
        <CardHeader
          icon={SlidersHorizontal}
          title="Generation"
          description="Token limits and sampling parameters"
        />
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
      </Card>

      {/* Secondary Model Card */}
      <CardSmallGap>
        <CardHeader
          icon={Cpu}
          title="Secondary Model"
          description="Cheaper model for planning, review, and summaries"
        />
        <div
          class="flex items-center justify-between"
          style={{
            'border-radius': '8px',
            background: '#ffffff08',
            border: '1px solid #ffffff0a',
            padding: '8px 12px',
            cursor: 'pointer',
          }}
        >
          <select
            value={settings().generation.weakModel}
            onChange={(e) => updateGeneration({ weakModel: e.currentTarget.value })}
            style={{
              width: '100%',
              background: 'transparent',
              border: 'none',
              outline: 'none',
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              color: '#F5F5F7',
              cursor: 'pointer',
              '-webkit-appearance': 'none',
            }}
          >
            <For each={WEAK_MODEL_OPTIONS}>
              {(opt) => <option value={opt.value}>{opt.label}</option>}
            </For>
          </select>
          <svg
            width="14"
            height="14"
            viewBox="0 0 24 24"
            fill="none"
            stroke="#48484A"
            stroke-width="2"
            stroke-linecap="round"
            stroke-linejoin="round"
            aria-hidden="true"
            style={{ 'flex-shrink': '0', 'margin-left': '8px' }}
          >
            <path d="m6 9 6 6 6-6" />
          </svg>
        </div>
      </CardSmallGap>

      {/* Agent Limits Card */}
      <Card>
        <CardHeader
          icon={Timer}
          title="Agent Limits"
          description="Maximum turns and time per agent run"
        />
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
      </Card>

      {/* Custom Instructions Card */}
      <CardSmallGap>
        <CardHeader
          icon={FileText}
          title="Custom Instructions"
          description="Prepended as a system message to every request"
        />
        <div
          style={{
            'border-radius': '8px',
            background: '#ffffff08',
            border: '1px solid #ffffff0a',
            padding: '8px 12px',
            height: '80px',
            overflow: 'auto',
          }}
        >
          <textarea
            value={settings().generation.customInstructions}
            onInput={(e) => updateGeneration({ customInstructions: e.currentTarget.value })}
            placeholder="Always respond in TypeScript. Prefer functional patterns..."
            style={{
              width: '100%',
              height: '100%',
              background: 'transparent',
              border: 'none',
              outline: 'none',
              resize: 'none',
              'font-family': 'Geist Mono, monospace',
              'font-size': '12px',
              color: '#48484A',
              'line-height': '1.5',
            }}
          />
        </div>
      </CardSmallGap>

      {/* Context Compaction Card */}
      <CardSmallGap>
        <CardHeader
          icon={Minimize2}
          title="Context Compaction"
          description="Automatic conversation compression"
        />
        <div class="flex items-center justify-between">
          <div style={{ display: 'flex', 'flex-direction': 'column', gap: '2px' }}>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '13px',
                color: '#C8C8CC',
              }}
            >
              Auto-compact conversation
            </span>
            <span
              style={{
                'font-family': 'Geist, sans-serif',
                'font-size': '12px',
                color: '#48484A',
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
              background: '#ffffff08',
              border: '1px solid #ffffff0a',
              padding: '8px 12px',
              cursor: 'pointer',
            }}
          >
            <div
              style={{
                display: 'flex',
                'flex-direction': 'column',
                gap: '2px',
                'margin-right': '12px',
              }}
            >
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '13px',
                  color: '#C8C8CC',
                }}
              >
                Compaction model
              </span>
              <span
                style={{
                  'font-family': 'Geist, sans-serif',
                  'font-size': '12px',
                  color: '#48484A',
                }}
              >
                Use the current chat model or a cheaper summarizer
              </span>
            </div>
            <select
              value={settings().generation.compactionModel}
              onChange={(e) => updateGeneration({ compactionModel: e.currentTarget.value })}
              style={{
                width: '100%',
                background: 'transparent',
                border: 'none',
                outline: 'none',
                'font-family': 'Geist Mono, monospace',
                'font-size': '12px',
                color: '#F5F5F7',
                cursor: 'pointer',
                '-webkit-appearance': 'none',
              }}
            >
              <For each={compactionModelOptions()}>
                {(opt) => <option value={opt.value}>{opt.label}</option>}
              </For>
            </select>
          </div>
        </Show>
      </CardSmallGap>

      {/* Model Aliases Card */}
      <CardSmallGap>
        <CardHeader
          icon={Tag as Component<{ class?: string; style?: Record<string, string> }>}
          title="Model Aliases"
          description="Short names for frequently used models"
        />
        <ModelAliasesSection />
      </CardSmallGap>
    </div>
  )
}
