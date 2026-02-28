/**
 * LLM Settings Tab
 *
 * Generation parameters (maxTokens, temperature, topP),
 * weak model for secondary tasks, agent limits, and custom instructions.
 */

import { type Component, createMemo, For, Show } from 'solid-js'
import { useSettings } from '../../../stores/settings'

// ============================================================================
// Model pair presets — auto-suggest cheap model for common primary models
// ============================================================================

const MODEL_PAIRS: Record<string, string> = {
  'claude-opus-4': 'claude-haiku-4-20250514',
  'claude-sonnet-4': 'claude-haiku-4-20250514',
  'claude-sonnet-4-20250514': 'claude-haiku-4-20250514',
  'claude-3-opus': 'claude-3-haiku-20240307',
  'claude-3-sonnet': 'claude-3-haiku-20240307',
  'gpt-4o': 'gpt-4o-mini',
  'gpt-4-turbo': 'gpt-4o-mini',
  'gpt-4': 'gpt-4o-mini',
  'gemini-2.0-flash': 'gemini-2.0-flash-lite',
  'gemini-2.5-pro': 'gemini-2.0-flash',
  'deepseek-chat': 'deepseek-chat',
  'deepseek-reasoner': 'deepseek-chat',
}

/** Well-known cheap/fast models for the secondary model dropdown */
const WEAK_MODEL_OPTIONS = [
  { value: '', label: 'Same as primary (no separate model)' },
  { value: 'claude-haiku-4-20250514', label: 'Claude Haiku 4' },
  { value: 'claude-3-haiku-20240307', label: 'Claude 3 Haiku' },
  { value: 'gpt-4o-mini', label: 'GPT-4o Mini' },
  { value: 'gemini-2.0-flash-lite', label: 'Gemini 2.0 Flash Lite' },
  { value: 'gemini-2.0-flash', label: 'Gemini 2.0 Flash' },
  { value: 'deepseek-chat', label: 'DeepSeek Chat' },
  { value: 'mistral-small-latest', label: 'Mistral Small' },
  { value: 'llama-3.3-70b-versatile', label: 'Llama 3.3 70B (Groq)' },
]

/** Editor model presets — mid-tier models good for file edits (faster than architect) */
const EDITOR_MODEL_OPTIONS = [
  { value: '', label: 'Same as primary (no separate model)' },
  { value: 'claude-sonnet-4-20250514', label: 'Claude Sonnet 4' },
  { value: 'claude-haiku-4-20250514', label: 'Claude Haiku 4' },
  { value: 'gpt-4o-mini', label: 'GPT-4o Mini' },
  { value: 'gpt-4o', label: 'GPT-4o' },
  { value: 'gemini-2.0-flash', label: 'Gemini 2.0 Flash' },
  { value: 'deepseek-chat', label: 'DeepSeek Chat' },
  { value: 'mistral-small-latest', label: 'Mistral Small' },
]

/** Auto-suggest editor model based on primary (architect) model */
const EDITOR_PAIRS: Record<string, string> = {
  'claude-opus-4': 'claude-sonnet-4-20250514',
  'claude-sonnet-4': 'claude-haiku-4-20250514',
  'claude-sonnet-4-20250514': 'claude-haiku-4-20250514',
  'gpt-4o': 'gpt-4o-mini',
  'gpt-4-turbo': 'gpt-4o-mini',
  o1: 'gpt-4o',
  o3: 'gpt-4o',
  'gemini-2.5-pro': 'gemini-2.0-flash',
  'deepseek-reasoner': 'deepseek-chat',
}

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
    </div>
  )
}
