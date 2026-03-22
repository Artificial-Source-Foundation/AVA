/**
 * LLM Tab — configuration data and shared helpers
 *
 * Model pair presets, dropdown options, and reusable form components.
 */

import type { Component } from 'solid-js'

// ============================================================================
// Model pair presets — auto-suggest cheap model for common primary models
// ============================================================================

export const MODEL_PAIRS: Record<string, string> = {
  'claude-opus-4-6': 'claude-haiku-4-5',
  'claude-sonnet-4-6': 'claude-haiku-4-5',
  'gpt-5.4': 'gpt-5.4-mini',
  'gpt-5.3-codex': 'gpt-4.1-mini',
  'gpt-4.1': 'gpt-4.1-mini',
  'o4-mini': 'gpt-4.1-mini',
  'gemini-2.5-pro': 'gemini-2.5-flash',
  'deepseek-chat': 'deepseek-chat',
  'deepseek-reasoner': 'deepseek-chat',
}

/** Well-known cheap/fast models for the secondary model dropdown */
export const WEAK_MODEL_OPTIONS = [
  { value: '', label: 'Same as primary (no separate model)' },
  { value: 'claude-haiku-4-5', label: 'Claude Haiku 4.5' },
  { value: 'gpt-5.4-mini', label: 'GPT-5.4 Mini' },
  { value: 'gpt-4.1-mini', label: 'GPT-4.1 Mini' },
  { value: 'gemini-2.5-flash', label: 'Gemini 2.5 Flash' },
  { value: 'deepseek-chat', label: 'DeepSeek Chat' },
  { value: 'mistral-small-latest', label: 'Mistral Small' },
]

/** Editor model presets — mid-tier models good for file edits (faster than architect) */
export const EDITOR_MODEL_OPTIONS = [
  { value: '', label: 'Same as primary (no separate model)' },
  { value: 'claude-sonnet-4-6', label: 'Claude Sonnet 4.6' },
  { value: 'claude-haiku-4-5', label: 'Claude Haiku 4.5' },
  { value: 'gpt-5.4-mini', label: 'GPT-5.4 Mini' },
  { value: 'gpt-4.1', label: 'GPT-4.1' },
  { value: 'gemini-2.5-flash', label: 'Gemini 2.5 Flash' },
  { value: 'deepseek-chat', label: 'DeepSeek Chat' },
  { value: 'mistral-small-latest', label: 'Mistral Small' },
]

/** Auto-suggest editor model based on primary (architect) model */
export const EDITOR_PAIRS: Record<string, string> = {
  'claude-opus-4-6': 'claude-sonnet-4-6',
  'claude-sonnet-4-6': 'claude-haiku-4-5',
  'gpt-5.4': 'gpt-5.4-mini',
  'gpt-5.3-codex': 'gpt-4.1-mini',
  'gpt-4.1': 'gpt-4.1-mini',
  'o4-mini': 'gpt-4.1-mini',
  'gemini-2.5-pro': 'gemini-2.5-flash',
  'deepseek-reasoner': 'deepseek-chat',
}

// ============================================================================
// Shared helper components
// ============================================================================

export const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[var(--settings-text-badge)] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
    {props.title}
  </h3>
)

export const SliderRow: Component<{
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
      <span class="text-[var(--settings-text-label)] text-[var(--text-secondary)] flex-shrink-0">
        {props.label}
      </span>
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
        <span class="text-[var(--settings-text-button)] font-mono text-[var(--text-muted)] w-14 text-right">
          {display()}
        </span>
      </div>
    </div>
  )
}
