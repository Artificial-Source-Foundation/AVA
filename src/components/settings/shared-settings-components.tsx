/**
 * Settings Shared Components — Milestone 3 Consistency
 *
 * Reusable building blocks for settings tabs using theme-aware CSS variables.
 * These components replace hardcoded hex colors with semantic theme tokens.
 */

import type { Component, JSX } from 'solid-js'
import { createUniqueId, For, Show } from 'solid-js'

// ============================================================================
// Layout Constants (re-export for convenience)
// ============================================================================

export { SETTINGS_CARD_GAP } from './settings-constants'

// ============================================================================
// Card Components
// ============================================================================

interface SettingsCardProps {
  icon?: Component<{ class?: string }>
  title: string
  description?: string
  children: JSX.Element
  /** Smaller gap for compact cards */
  compact?: boolean
}

/**
 * Theme-aware card component for settings sections.
 * Uses semantic CSS variables for proper light/dark mode support.
 */
export const SettingsCard: Component<SettingsCardProps> = (props) => (
  <div
    style={{
      display: 'flex',
      'flex-direction': 'column',
      gap: props.compact ? '12px' : '16px',
      background: 'var(--surface)',
      border: '1px solid var(--border-subtle)',
      'border-radius': '12px',
      padding: '20px',
    }}
  >
    <div class="flex items-center gap-2.5 min-w-0">
      <Show when={props.icon}>
        {(() => {
          const Icon = props.icon!
          return (
            <span class="shrink-0 w-4 h-4" style={{ color: 'var(--text-secondary)' }}>
              <Icon class="w-4 h-4" />
            </span>
          )
        })()}
      </Show>
      <div class="min-w-0">
        <h3
          style={{
            'font-family': 'var(--font-sans)',
            'font-size': '14px',
            'font-weight': '500',
            color: 'var(--text-primary)',
          }}
        >
          {props.title}
        </h3>
        <Show when={props.description}>
          <p
            style={{
              'font-family': 'var(--font-sans)',
              'font-size': '12px',
              color: 'var(--text-muted)',
              'margin-top': '2px',
            }}
          >
            {props.description}
          </p>
        </Show>
      </div>
    </div>
    {props.children}
  </div>
)

/**
 * Simple card without header - for grouped content blocks
 */
export const SettingsCardSimple: Component<{ children: JSX.Element; compact?: boolean }> = (
  props
) => (
  <div
    style={{
      display: 'flex',
      'flex-direction': 'column',
      gap: props.compact ? '12px' : '16px',
      background: 'var(--surface)',
      border: '1px solid var(--border-subtle)',
      'border-radius': '12px',
      padding: '20px',
    }}
  >
    {props.children}
  </div>
)

// ============================================================================
// Header Components
// ============================================================================

interface SettingsSectionHeaderProps {
  icon?: Component<{ class?: string; style?: Record<string, string> }>
  title: string
  description?: string
}

/**
 * Standard section header with icon + title + description
 * Used at the top of cards or sections
 */
export const SettingsSectionHeader: Component<SettingsSectionHeaderProps> = (props) => (
  <div class="flex items-center gap-2.5 min-w-0">
    <Show when={props.icon}>
      {(() => {
        const Icon = props.icon!
        return <Icon class="w-4 h-4 shrink-0" style={{ color: 'var(--text-secondary)' }} />
      })()}
    </Show>
    <div class="min-w-0">
      <h3
        style={{
          'font-family': 'var(--font-sans)',
          'font-size': '14px',
          'font-weight': '500',
          color: 'var(--text-primary)',
        }}
      >
        {props.title}
      </h3>
      <Show when={props.description}>
        <p
          style={{
            'font-family': 'var(--font-sans)',
            'font-size': '12px',
            color: 'var(--text-muted)',
            'margin-top': '2px',
          }}
        >
          {props.description}
        </p>
      </Show>
    </div>
  </div>
)

/**
 * Page title component for tab headers
 */
export const SettingsPageTitle: Component<{ children: JSX.Element }> = (props) => (
  <h1
    style={{
      'font-family': 'var(--font-sans)',
      'font-size': '22px',
      'font-weight': '600',
      color: 'var(--text-primary)',
    }}
  >
    {props.children}
  </h1>
)

// ============================================================================
// Action Header (with button on the right)
// ============================================================================

interface SettingsActionHeaderProps {
  icon?: Component<{ class?: string; style?: Record<string, string> }>
  title: string
  description?: string
  action: JSX.Element
}

/**
 * Header with an action button on the right side
 * Used for cards that have a primary action (Add, Create, etc.)
 */
export const SettingsActionHeader: Component<SettingsActionHeaderProps> = (props) => (
  <div class="flex items-center justify-between">
    <div class="flex items-center gap-2.5 min-w-0">
      <Show when={props.icon}>
        {(() => {
          const Icon = props.icon!
          return <Icon class="w-4 h-4 shrink-0" style={{ color: 'var(--text-secondary)' }} />
        })()}
      </Show>
      <div class="min-w-0">
        <h3
          style={{
            'font-family': 'var(--font-sans)',
            'font-size': '14px',
            'font-weight': '500',
            color: 'var(--text-primary)',
          }}
        >
          {props.title}
        </h3>
        <Show when={props.description}>
          <p
            style={{
              'font-family': 'var(--font-sans)',
              'font-size': '12px',
              color: 'var(--text-muted)',
              'margin-top': '2px',
            }}
          >
            {props.description}
          </p>
        </Show>
      </div>
    </div>
    {props.action}
  </div>
)

// ============================================================================
// Button Components
// ============================================================================

interface SettingsButtonProps {
  children: JSX.Element
  onClick: () => void
  variant?: 'primary' | 'secondary'
  icon?: Component<{ class?: string; style?: Record<string, string> }>
}

/**
 * Theme-aware button for settings actions
 */
export const SettingsButton: Component<SettingsButtonProps> = (props) => {
  const isPrimary = () => props.variant === 'primary'
  return (
    <button
      type="button"
      onClick={props.onClick}
      class="flex items-center gap-1.5 transition-colors"
      style={{
        padding: '6px 12px',
        'border-radius': '8px',
        border: 'none',
        cursor: 'pointer',
        'font-family': 'var(--font-sans)',
        'font-size': '12px',
        'font-weight': '500',
        background: isPrimary() ? 'var(--accent)' : 'var(--surface-raised)',
        color: isPrimary() ? 'var(--text-on-accent)' : 'var(--text-primary)',
      }}
    >
      <Show when={props.icon}>
        {(() => {
          const Icon = props.icon!
          return <Icon class="w-3 h-3" style={{ color: 'currentColor' }} />
        })()}
      </Show>
      {props.children}
    </button>
  )
}

// ============================================================================
// Input Components
// ============================================================================

interface SettingsInputProps {
  value: string
  onInput: (value: string) => void
  placeholder?: string
  type?: 'text' | 'search'
  icon?: Component<{ class?: string; style?: Record<string, string> }>
  /** Accessible label - creates visible label above input when provided */
  label?: string
  /** ID for label association - auto-generated if not provided */
  id?: string
  /** Aria label for screen readers when visible label is not used */
  ariaLabel?: string
}

/**
 * Theme-aware input with optional icon and accessible labeling
 */
export const SettingsInput: Component<SettingsInputProps> = (props) => {
  const generatedId = createUniqueId()
  const inputId = () => props.id || `settings-input-${generatedId}`
  return (
    <div class="space-y-1.5">
      <Show when={props.label}>
        <label
          for={inputId()}
          style={{
            'font-family': 'var(--font-sans)',
            'font-size': '12px',
            'font-weight': '500',
            color: 'var(--text-secondary)',
            display: 'block',
          }}
        >
          {props.label}
        </label>
      </Show>
      <div
        class="flex items-center gap-2 focus-within:ring-2 focus-within:ring-[var(--accent)] focus-within:ring-offset-1 focus-within:ring-offset-[var(--surface)]"
        style={{
          padding: '8px 12px',
          background: 'var(--surface-sunken)',
          border: '1px solid var(--border-subtle)',
          'border-radius': '8px',
          transition: 'border-color 0.15s, box-shadow 0.15s',
        }}
      >
        <Show when={props.icon}>
          {(() => {
            const Icon = props.icon!
            return <Icon class="w-3 h-3 shrink-0" style={{ color: 'var(--text-muted)' }} />
          })()}
        </Show>
        <input
          id={inputId()}
          type={props.type === 'search' ? 'search' : 'text'}
          value={props.value}
          placeholder={props.placeholder}
          aria-label={props.ariaLabel}
          onInput={(e) => props.onInput(e.currentTarget.value)}
          style={{
            flex: 1,
            background: 'transparent',
            border: 'none',
            outline: 'none',
            'font-family': 'var(--font-sans)',
            'font-size': '12px',
            color: 'var(--text-primary)',
          }}
        />
      </div>
    </div>
  )
}

interface SettingsTextareaProps {
  value: string
  onInput: (value: string) => void
  placeholder?: string
  rows?: number
  /** Accessible label - creates visible label above textarea when provided */
  label?: string
  /** ID for label association - auto-generated if not provided */
  id?: string
  /** Aria label for screen readers when visible label is not used */
  ariaLabel?: string
}

/**
 * Theme-aware textarea container with accessible labeling
 */
export const SettingsTextarea: Component<SettingsTextareaProps> = (props) => {
  const generatedId = createUniqueId()
  const textareaId = () => props.id || `settings-textarea-${generatedId}`
  return (
    <div class="space-y-1.5">
      <Show when={props.label}>
        <label
          for={textareaId()}
          style={{
            'font-family': 'var(--font-sans)',
            'font-size': '12px',
            'font-weight': '500',
            color: 'var(--text-secondary)',
            display: 'block',
          }}
        >
          {props.label}
        </label>
      </Show>
      <div
        class="focus-within:ring-2 focus-within:ring-[var(--accent)] focus-within:ring-offset-1 focus-within:ring-offset-[var(--surface)]"
        style={{
          'border-radius': '8px',
          background: 'var(--surface-sunken)',
          border: '1px solid var(--border-subtle)',
          padding: '8px 12px',
          height: props.rows ? `${props.rows * 20}px` : '80px',
          overflow: 'auto',
          transition: 'border-color 0.15s, box-shadow 0.15s',
        }}
      >
        <textarea
          id={textareaId()}
          value={props.value}
          placeholder={props.placeholder}
          aria-label={props.ariaLabel}
          onInput={(e) => props.onInput(e.currentTarget.value)}
          style={{
            width: '100%',
            height: '100%',
            background: 'transparent',
            border: 'none',
            outline: 'none',
            resize: 'none',
            'font-family': 'var(--font-mono)',
            'font-size': '12px',
            color: 'var(--text-secondary)',
            'line-height': '1.5',
          }}
        />
      </div>
    </div>
  )
}

// ============================================================================
// Select Component
// ============================================================================

interface SettingsSelectProps {
  value: string
  onChange: (value: string) => void
  options: { value: string; label: string }[]
  /** Accessible label - creates visible label above select when provided */
  label?: string
  /** ID for label association - auto-generated if not provided */
  id?: string
  /** Aria label for screen readers when visible label is not used */
  ariaLabel?: string
}

/**
 * Theme-aware select dropdown with accessible labeling
 */
export const SettingsSelect: Component<SettingsSelectProps> = (props) => {
  const generatedId = createUniqueId()
  const selectId = () => props.id || `settings-select-${generatedId}`
  return (
    <div class="space-y-1.5">
      <Show when={props.label}>
        <label
          for={selectId()}
          style={{
            'font-family': 'var(--font-sans)',
            'font-size': '12px',
            'font-weight': '500',
            color: 'var(--text-secondary)',
            display: 'block',
          }}
        >
          {props.label}
        </label>
      </Show>
      <div
        class="flex items-center justify-between focus-within:ring-2 focus-within:ring-[var(--accent)] focus-within:ring-offset-1 focus-within:ring-offset-[var(--surface)]"
        style={{
          'border-radius': '8px',
          background: 'var(--surface-sunken)',
          border: '1px solid var(--border-subtle)',
          padding: '8px 12px',
          cursor: 'pointer',
          transition: 'border-color 0.15s, box-shadow 0.15s',
        }}
      >
        <select
          id={selectId()}
          value={props.value}
          aria-label={props.ariaLabel}
          onChange={(e) => props.onChange(e.currentTarget.value)}
          style={{
            width: '100%',
            background: 'transparent',
            border: 'none',
            outline: 'none',
            'font-family': 'var(--font-mono)',
            'font-size': '12px',
            color: 'var(--text-primary)',
            cursor: 'pointer',
            '-webkit-appearance': 'none',
            appearance: 'none',
          }}
        >
          <For each={props.options}>{(opt) => <option value={opt.value}>{opt.label}</option>}</For>
        </select>
        <svg
          width="14"
          height="14"
          viewBox="0 0 24 24"
          fill="none"
          stroke="currentColor"
          stroke-width="2"
          stroke-linecap="round"
          stroke-linejoin="round"
          aria-hidden="true"
          style={{
            'flex-shrink': '0',
            'margin-left': '8px',
            color: 'var(--text-muted)',
          }}
        >
          <path d="m6 9 6 6 6-6" />
        </svg>
      </div>
    </div>
  )
}

// ============================================================================
// Badge/Status Components
// ============================================================================

/**
 * Status badge with theme-aware colors
 */
export const SettingsStatusBadge: Component<{
  children: JSX.Element
  variant: 'success' | 'default' | 'error'
}> = (props) => {
  const styles = {
    success: {
      background: 'var(--success-subtle)',
      color: 'var(--success)',
    },
    default: {
      background: 'var(--surface-raised)',
      color: 'var(--text-muted)',
    },
    error: {
      background: 'var(--error-subtle)',
      color: 'var(--error)',
    },
  }
  const style = styles[props.variant]
  return (
    <span
      style={{
        padding: '3px 8px',
        'border-radius': '6px',
        'font-family': 'var(--font-sans)',
        'font-size': '10px',
        'font-weight': '500',
        ...style,
      }}
    >
      {props.children}
    </span>
  )
}

// ============================================================================
// Layout Components
// ============================================================================

/**
 * Container for tab content with consistent spacing
 */
export const SettingsTabContainer: Component<{ children: JSX.Element }> = (props) => (
  <div style={{ display: 'flex', 'flex-direction': 'column', gap: '24px' }}>{props.children}</div>
)

/**
 * Row layout for form fields
 */
export const SettingsRow: Component<{
  children: JSX.Element
  justify?: 'between' | 'start'
}> = (props) => (
  <div
    class={`flex items-center ${props.justify === 'between' ? 'justify-between' : ''}`}
    style={{ gap: '12px' }}
  >
    {props.children}
  </div>
)

/**
 * Label + value row for settings items (slider-compatible)
 */
export const SettingsLabelValue: Component<{
  label: string
  value?: string | number
  format?: (v: string | number) => string
  /** Optional right-side content (e.g., slider input) */
  rightContent?: JSX.Element
}> = (props) => (
  <div class="flex items-center justify-between" style={{ padding: '0', gap: '12px' }}>
    <span
      style={{
        'font-family': 'var(--font-sans)',
        'font-size': '13px',
        color: 'var(--text-secondary)',
        'flex-shrink': '0',
      }}
    >
      {props.label}
    </span>
    <Show
      when={props.rightContent}
      fallback={
        <span
          style={{
            'font-family': 'var(--font-mono)',
            'font-size': '12px',
            color: 'var(--text-muted)',
            'min-width': '48px',
            'text-align': 'right',
          }}
        >
          {props.format && props.value !== undefined ? props.format(props.value) : props.value}
        </span>
      }
    >
      <div class="flex items-center" style={{ gap: '8px' }}>
        {props.rightContent}
        <span
          style={{
            'font-family': 'var(--font-mono)',
            'font-size': '12px',
            color: 'var(--text-muted)',
            'min-width': '48px',
            'text-align': 'right',
          }}
        >
          {props.format && props.value !== undefined ? props.format(props.value) : props.value}
        </span>
      </div>
    </Show>
  </div>
)
