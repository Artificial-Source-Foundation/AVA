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
export const WEAK_MODEL_OPTIONS = [
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
export const EDITOR_MODEL_OPTIONS = [
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
export const EDITOR_PAIRS: Record<string, string> = {
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
// Shared helper components
// ============================================================================

export const SectionHeader: Component<{ title: string }> = (props) => (
  <h3 class="text-[10px] font-semibold text-[var(--text-muted)] uppercase tracking-wider mb-2">
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
