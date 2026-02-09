/**
 * LLM Settings Tab
 *
 * Generation parameters (maxTokens, temperature, topP),
 * agent limits (maxTurns, maxTimeMinutes), and custom instructions.
 */

import { type Component, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'

// ============================================================================
// Shared helpers (same patterns as AppearanceTab)
// ============================================================================

const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
    {props.title}
  </h3>
)

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
    <div class="flex items-center justify-between py-1.5 gap-3">
      <span class="text-xs text-[var(--text-secondary)] flex-shrink-0">{props.label}</span>
      <div class="flex items-center gap-2 flex-1 justify-end">
        <input
          type="range"
          min={props.min}
          max={props.max}
          step={props.step}
          value={props.value}
          onInput={(e) => props.onChange(Number(e.currentTarget.value))}
          class="w-28 accent-[var(--accent)]"
        />
        <span class="text-[11px] font-mono text-[var(--text-muted)] w-14 text-right">
          {display()}
        </span>
      </div>
    </div>
  )
}

// ============================================================================
// Main Tab
// ============================================================================

export const LLMTab: Component = () => {
  const { settings, updateGeneration, updateAgentLimits } = useSettings()

  return (
    <div class="space-y-5">
      {/* Generation */}
      <div>
        <SectionHeader title="Generation" />
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
      </div>

      {/* Agent Limits */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Agent Limits" />
        <SliderRow
          label="Max Turns"
          value={settings().agentLimits.agentMaxTurns}
          min={1}
          max={100}
          step={1}
          onChange={(v) => updateAgentLimits({ agentMaxTurns: v })}
        />
        <SliderRow
          label="Max Time (min)"
          value={settings().agentLimits.agentMaxTimeMinutes}
          min={1}
          max={60}
          step={1}
          format={(v) => `${v}m`}
          onChange={(v) => updateAgentLimits({ agentMaxTimeMinutes: v })}
        />
      </div>

      {/* Custom Instructions */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Custom Instructions" />
        <p class="text-[10px] text-[var(--text-muted)] mb-2">
          Prepended as a system message to every chat and agent request.
        </p>
        <textarea
          value={settings().generation.customInstructions}
          onInput={(e) => updateGeneration({ customInstructions: e.currentTarget.value })}
          placeholder="e.g. Always respond in TypeScript. Prefer functional patterns..."
          rows={4}
          class="
            w-full px-3 py-2
            bg-[var(--input-background)] text-[var(--text-primary)]
            placeholder-[var(--input-placeholder)]
            border border-[var(--input-border)] rounded-[var(--radius-md)]
            text-xs font-mono resize-none
            focus:outline-none focus:border-[var(--input-border-focus)]
          "
        />
        <Show when={settings().generation.customInstructions.length > 0}>
          <span class="text-[10px] text-[var(--text-muted)] mt-1 block text-right">
            {settings().generation.customInstructions.length} chars
          </span>
        </Show>
      </div>
    </div>
  )
}
