/**
 * Input Component
 *
 * Text input with label, description, and error states.
 * Built with Kobalte for accessibility.
 */

import { TextField } from '@kobalte/core/text-field'
import { type Component, type JSX, Show, splitProps } from 'solid-js'

export interface InputProps {
  /** Input label */
  label?: string
  /** Input description/helper text */
  description?: string
  /** Error message */
  error?: string
  /** Placeholder text */
  placeholder?: string
  /** Input value */
  value?: string
  /** Value change handler */
  onValueChange?: (value: string) => void
  /** Input type */
  type?: 'text' | 'email' | 'password' | 'number' | 'search' | 'tel' | 'url'
  /** Disabled state */
  disabled?: boolean
  /** Required field */
  required?: boolean
  /** Input size */
  size?: 'sm' | 'md' | 'lg'
  /** Additional CSS classes */
  class?: string
  /** Icon to show before input */
  icon?: JSX.Element
  /** Icon to show after input */
  iconRight?: JSX.Element
  /** Input name attribute */
  name?: string
  /** Autofocus */
  autofocus?: boolean
}

export const Input: Component<InputProps> = (props) => {
  const [local, others] = splitProps(props, [
    'label',
    'description',
    'error',
    'placeholder',
    'value',
    'onValueChange',
    'type',
    'disabled',
    'required',
    'size',
    'class',
    'icon',
    'iconRight',
    'name',
    'autofocus',
  ])

  const size = () => local.size ?? 'md'

  const sizeStyles = {
    sm: 'h-8 px-3 text-sm',
    md: 'h-10 px-3 text-sm',
    lg: 'h-12 px-4 text-base',
  }

  const iconPaddingLeft = {
    sm: 'pl-8',
    md: 'pl-10',
    lg: 'pl-12',
  }

  const iconPaddingRight = {
    sm: 'pr-8',
    md: 'pr-10',
    lg: 'pr-12',
  }

  return (
    <TextField
      value={local.value}
      onChange={local.onValueChange}
      disabled={local.disabled}
      required={local.required}
      validationState={local.error ? 'invalid' : 'valid'}
      class={`flex flex-col gap-1.5 ${local.class ?? ''}`}
      {...others}
    >
      <Show when={local.label}>
        <TextField.Label class="text-sm font-medium text-[var(--text-primary)]">
          {local.label}
          <Show when={local.required}>
            <span class="text-[var(--error)] ml-1">*</span>
          </Show>
        </TextField.Label>
      </Show>

      <div class="relative">
        <Show when={local.icon}>
          <div class="absolute left-3 top-1/2 -translate-y-1/2 text-[var(--text-tertiary)]">
            {local.icon}
          </div>
        </Show>

        <TextField.Input
          type={local.type ?? 'text'}
          name={local.name}
          placeholder={local.placeholder}
          autofocus={local.autofocus}
          class={`
            w-full
            bg-[var(--input-background)]
            border border-[var(--input-border)]
            rounded-[var(--radius-lg)]
            text-[var(--text-primary)]
            placeholder:text-[var(--input-placeholder)]
            transition-all duration-[var(--duration-fast)] ease-[var(--ease-out)]
            hover:border-[var(--input-border-hover)]
            focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
            disabled:opacity-50 disabled:cursor-not-allowed
            ${sizeStyles[size()]}
            ${local.icon ? iconPaddingLeft[size()] : ''}
            ${local.iconRight ? iconPaddingRight[size()] : ''}
            ${local.error ? 'border-[var(--error)] focus:border-[var(--error)] focus:ring-[var(--error-subtle)]' : ''}
          `}
        />

        <Show when={local.iconRight}>
          <div class="absolute right-3 top-1/2 -translate-y-1/2 text-[var(--text-tertiary)]">
            {local.iconRight}
          </div>
        </Show>
      </div>

      <Show when={local.description && !local.error}>
        <TextField.Description class="text-xs text-[var(--text-tertiary)]">
          {local.description}
        </TextField.Description>
      </Show>

      <Show when={local.error}>
        <TextField.ErrorMessage class="text-xs text-[var(--error)]">
          {local.error}
        </TextField.ErrorMessage>
      </Show>
    </TextField>
  )
}

/**
 * Textarea Component
 */
export interface TextareaProps {
  /** Textarea label */
  label?: string
  /** Description/helper text */
  description?: string
  /** Error message */
  error?: string
  /** Placeholder text */
  placeholder?: string
  /** Textarea value */
  value?: string
  /** Value change handler */
  onValueChange?: (value: string) => void
  /** Disabled state */
  disabled?: boolean
  /** Required field */
  required?: boolean
  /** Number of rows */
  rows?: number
  /** Additional CSS classes */
  class?: string
  /** Textarea name attribute */
  name?: string
  /** Auto-resize based on content */
  autoResize?: boolean
}

export const Textarea: Component<TextareaProps> = (props) => {
  const [local, others] = splitProps(props, [
    'label',
    'description',
    'error',
    'placeholder',
    'value',
    'onValueChange',
    'disabled',
    'required',
    'rows',
    'class',
    'name',
    'autoResize',
  ])

  return (
    <TextField
      value={local.value}
      onChange={local.onValueChange}
      disabled={local.disabled}
      required={local.required}
      validationState={local.error ? 'invalid' : 'valid'}
      class={`flex flex-col gap-1.5 ${local.class ?? ''}`}
      {...others}
    >
      <Show when={local.label}>
        <TextField.Label class="text-sm font-medium text-[var(--text-primary)]">
          {local.label}
          <Show when={local.required}>
            <span class="text-[var(--error)] ml-1">*</span>
          </Show>
        </TextField.Label>
      </Show>

      <TextField.TextArea
        name={local.name}
        placeholder={local.placeholder}
        rows={local.rows ?? 4}
        class={`
          w-full
          bg-[var(--input-background)]
          border border-[var(--input-border)]
          rounded-[var(--radius-lg)]
          text-[var(--text-primary)]
          placeholder:text-[var(--input-placeholder)]
          p-3 text-sm
          resize-y min-h-[80px]
          transition-all duration-[var(--duration-fast)] ease-[var(--ease-out)]
          hover:border-[var(--input-border-hover)]
          focus:outline-none focus:border-[var(--input-border-focus)] focus:ring-2 focus:ring-[var(--accent-subtle)]
          disabled:opacity-50 disabled:cursor-not-allowed
          ${local.error ? 'border-[var(--error)] focus:border-[var(--error)] focus:ring-[var(--error-subtle)]' : ''}
        `}
      />

      <Show when={local.description && !local.error}>
        <TextField.Description class="text-xs text-[var(--text-tertiary)]">
          {local.description}
        </TextField.Description>
      </Show>

      <Show when={local.error}>
        <TextField.ErrorMessage class="text-xs text-[var(--error)]">
          {local.error}
        </TextField.ErrorMessage>
      </Show>
    </TextField>
  )
}
