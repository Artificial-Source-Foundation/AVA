/**
 * LLM Settings Tab
 *
 * Generation parameters (maxTokens, temperature, topP),
 * weak model for secondary tasks, agent limits, and custom instructions.
 */

import { type Component, createMemo, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'
import {
  EDITOR_MODEL_OPTIONS,
  EDITOR_PAIRS,
  MODEL_PAIRS,
  SectionHeader,
  SliderRow,
  WEAK_MODEL_OPTIONS,
} from './llm/llm-config'
import { ModelAliasesSection } from './model-aliases-section'

export const LLMTab: Component = () => {
  const { settings, updateSettings, updateGeneration, updateAgentLimits } = useSettings()

  // Find the active provider's default model for pair suggestion
  const activeModel = createMemo(() => {
    const active = settings().providers.find(
      (p) => p.enabled && (p.apiKey || p.status === 'connected')
    )
    return active?.defaultModel ?? ''
  })

  // Suggest a weak model based on the active primary model
  const suggestedWeak = createMemo(() => {
    const model = activeModel()
    if (!model) return null
    if (MODEL_PAIRS[model]) return MODEL_PAIRS[model]
    for (const [prefix, weak] of Object.entries(MODEL_PAIRS)) {
      if (model.startsWith(prefix)) return weak
    }
    return null
  })

  // Suggest an editor model based on the active primary (architect) model
  const suggestedEditor = createMemo(() => {
    const model = activeModel()
    if (!model) return null
    if (EDITOR_PAIRS[model]) return EDITOR_PAIRS[model]
    for (const [prefix, editor] of Object.entries(EDITOR_PAIRS)) {
      if (model.startsWith(prefix)) return editor
    }
    return null
  })

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

      {/* Weak Model */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Secondary Model" />
        <p class="text-[10px] text-[var(--text-muted)] mb-2">
          Cheaper model for planning, code review, and summaries. Saves 50-80% on secondary tasks.
        </p>
        <div class="flex items-center gap-2">
          <select
            value={settings().generation.weakModel}
            onChange={(e) => updateGeneration({ weakModel: e.currentTarget.value })}
            class="
              flex-1 px-2 py-1.5
              bg-[var(--input-background)] text-[var(--text-primary)]
              border border-[var(--input-border)] rounded-[var(--radius-md)]
              text-xs focus:outline-none focus:border-[var(--input-border-focus)]
            "
          >
            <For each={WEAK_MODEL_OPTIONS}>
              {(opt) => <option value={opt.value}>{opt.label}</option>}
            </For>
          </select>
        </div>
        <Show when={suggestedWeak() && !settings().generation.weakModel}>
          <button
            type="button"
            onClick={() => updateGeneration({ weakModel: suggestedWeak()! })}
            class="
              mt-1.5 px-2 py-1
              text-[10px] text-[var(--accent)]
              border border-[var(--accent-border)] rounded-[var(--radius-sm)]
              hover:bg-[var(--accent-subtle)] transition-colors
            "
          >
            Auto-pair:{' '}
            {WEAK_MODEL_OPTIONS.find((o) => o.value === suggestedWeak())?.label ?? suggestedWeak()}
          </button>
        </Show>
        <Show when={settings().generation.weakModel}>
          <p class="text-[10px] text-[var(--text-muted)] mt-1">
            Used for: task planning, code review, context summaries
          </p>
        </Show>
      </div>

      {/* Editor Model (Architect/Editor Split) */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Editor Model" />
        <p class="text-[10px] text-[var(--text-muted)] mb-2">
          Cheaper model for Junior Devs executing file edits. The primary model acts as the
          architect (planning), this model handles execution.
        </p>
        <div class="flex items-center gap-2">
          <select
            value={settings().generation.editorModel}
            onChange={(e) => updateGeneration({ editorModel: e.currentTarget.value })}
            class="
              flex-1 px-2 py-1.5
              bg-[var(--input-background)] text-[var(--text-primary)]
              border border-[var(--input-border)] rounded-[var(--radius-md)]
              text-xs focus:outline-none focus:border-[var(--input-border-focus)]
            "
          >
            <For each={EDITOR_MODEL_OPTIONS}>
              {(opt) => <option value={opt.value}>{opt.label}</option>}
            </For>
          </select>
        </div>
        <Show when={suggestedEditor() && !settings().generation.editorModel}>
          <button
            type="button"
            onClick={() => updateGeneration({ editorModel: suggestedEditor()! })}
            class="
              mt-1.5 px-2 py-1
              text-[10px] text-[var(--accent)]
              border border-[var(--accent-border)] rounded-[var(--radius-sm)]
              hover:bg-[var(--accent-subtle)] transition-colors
            "
          >
            Auto-pair:{' '}
            {EDITOR_MODEL_OPTIONS.find((o) => o.value === suggestedEditor())?.label ??
              suggestedEditor()}
          </button>
        </Show>
        <Show when={settings().generation.editorModel}>
          <p class="text-[10px] text-[var(--text-muted)] mt-1">
            Used for: file edits, code generation, tool execution by Junior Devs
          </p>
        </Show>
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

      {/* Compaction Threshold */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Context Compaction" />
        <SliderRow
          label="Compaction threshold"
          value={settings().generation.compactionThreshold}
          min={50}
          max={95}
          step={5}
          format={(v) => `${v}%`}
          onChange={(v) =>
            updateSettings({
              generation: { ...settings().generation, compactionThreshold: v },
            })
          }
        />
        <p class="text-[10px] text-[var(--text-muted)] mt-1">
          Auto-compact conversation when context reaches this percentage of the token limit.
        </p>
      </div>

      {/* Model Aliases */}
      <div class="pt-2 border-t border-[var(--border-subtle)]">
        <SectionHeader title="Model Aliases" />
        <ModelAliasesSection />
      </div>
    </div>
  )
}
