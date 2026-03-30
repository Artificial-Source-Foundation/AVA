/**
 * LLM Tab — configuration data and shared helpers
 *
 * Model pair presets, dropdown options, and reusable form components.
 * Updated to match Pencil design tokens.
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
// Shared helper components — Pencil design tokens
// ============================================================================

export const SectionHeader: Component<{ title: string }> = (props) => (
  <h3
    style={{
      'font-family': 'Geist, sans-serif',
      'font-size': '10px',
      'font-weight': '600',
      color: '#48484A',
      'text-transform': 'uppercase',
      'letter-spacing': '0.05em',
      'margin-bottom': '8px',
    }}
  >
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
    <div class="flex items-center justify-between" style={{ padding: '0', gap: '12px' }}>
      <span
        style={{
          'font-family': 'Geist, sans-serif',
          'font-size': '13px',
          color: '#C8C8CC',
          'flex-shrink': '0',
        }}
      >
        {props.label}
      </span>
      <div class="flex items-center" style={{ gap: '8px' }}>
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
        <span
          style={{
            'font-family': 'Geist Mono, monospace',
            'font-size': '12px',
            color: '#48484A',
            'min-width': '48px',
            'text-align': 'right',
          }}
        >
          {display()}
        </span>
      </div>
    </div>
  )
}
