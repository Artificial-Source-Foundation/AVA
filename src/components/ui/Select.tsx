/**
 * Select Component
 *
 * Dropdown select with custom rendering.
 * Built with Kobalte for accessibility.
 */

import { Select as KobalteSelect } from '@kobalte/core/select'
import { Check, ChevronDown } from 'lucide-solid'
import { type Component, Show, splitProps } from 'solid-js'

export interface SelectOption {
  value: string
  label: string
  description?: string
  disabled?: boolean
}

export interface SelectProps {
  /** Select label */
  label?: string
  /** Description/helper text */
  description?: string
  /** Error message */
  error?: string
  /** Placeholder text */
  placeholder?: string
  /** Options */
  options: SelectOption[]
  /** Selected value */
  value?: string
  /** Value change handler */
  onChange?: (value: string) => void
  /** Disabled state */
  disabled?: boolean
  /** Required field */
  required?: boolean
  /** Select size */
  size?: 'sm' | 'md' | 'lg'
  /** Additional CSS classes */
  class?: string
  /** Name attribute */
  name?: string
}

export const Select: Component<SelectProps> = (props) => {
  const [local, others] = splitProps(props, [
    'label',
    'description',
    'error',
    'placeholder',
    'options',
    'value',
    'onChange',
    'disabled',
    'required',
    'size',
    'class',
    'name',
  ])

  const size = () => local.size ?? 'md'

  const sizeStyles = {
    sm: 'h-8 px-3 text-sm',
    md: 'h-10 px-3 text-sm',
    lg: 'h-12 px-4 text-base',
  }

  const handleChange = (option: SelectOption | null) => {
    if (option && local.onChange) {
      local.onChange(option.value)
    }
  }

  const selectedOption = () => local.options.find((o) => o.value === local.value)

  return (
    <KobalteSelect<SelectOption>
      value={selectedOption()}
      onChange={handleChange}
      options={local.options}
      optionValue="value"
      optionTextValue="label"
      optionDisabled="disabled"
      disabled={local.disabled}
      validationState={local.error ? 'invalid' : 'valid'}
      placeholder={local.placeholder}
      itemComponent={(itemProps) => (
        <KobalteSelect.Item
          item={itemProps.item}
          class="
            flex items-center justify-between
            px-3 py-2
            text-sm
            text-[var(--text-primary)]
            rounded-[var(--radius-md)]
            cursor-pointer
            outline-none
            transition-colors duration-[var(--duration-fast)]
            hover:bg-[var(--surface-raised)]
            focus:bg-[var(--surface-raised)]
            data-[highlighted]:bg-[var(--surface-raised)]
            data-[disabled]:opacity-50 data-[disabled]:cursor-not-allowed
          "
        >
          <div class="flex flex-col">
            <KobalteSelect.ItemLabel>{itemProps.item.rawValue.label}</KobalteSelect.ItemLabel>
            <Show when={itemProps.item.rawValue.description}>
              <span class="text-xs text-[var(--text-tertiary)]">
                {itemProps.item.rawValue.description}
              </span>
            </Show>
          </div>
          <KobalteSelect.ItemIndicator class="ml-2">
            <Check class="h-4 w-4 text-[var(--accent)]" />
          </KobalteSelect.ItemIndicator>
        </KobalteSelect.Item>
      )}
      class={`flex flex-col gap-1.5 ${local.class ?? ''}`}
      {...others}
    >
      <Show when={local.label}>
        <KobalteSelect.Label class="text-sm font-medium text-[var(--text-primary)]">
          {local.label}
          <Show when={local.required}>
            <span class="text-[var(--error)] ml-1">*</span>
          </Show>
        </KobalteSelect.Label>
      </Show>

      <KobalteSelect.HiddenSelect name={local.name} />

      <KobalteSelect.Trigger
        class={`
          flex items-center justify-between
          w-full
          bg-[var(--input-background)]
          border border-[var(--input-border)]
          rounded-[var(--radius-lg)]
          text-[var(--text-primary)]
          transition-colors duration-[var(--duration-fast)] ease-[var(--ease-out)]
          hover:border-[var(--input-border-hover)]
          focus-glow
          disabled:opacity-50 disabled:cursor-not-allowed
          data-[placeholder-shown]:text-[var(--input-placeholder)]
          ${sizeStyles[size()]}
          ${local.error ? 'border-[var(--error)]' : ''}
        `}
      >
        <KobalteSelect.Value<SelectOption>>
          {(state) => state.selectedOption()?.label}
        </KobalteSelect.Value>
        <KobalteSelect.Icon class="ml-2 text-[var(--text-tertiary)]">
          <ChevronDown class="h-4 w-4" />
        </KobalteSelect.Icon>
      </KobalteSelect.Trigger>

      <KobalteSelect.Portal>
        <KobalteSelect.Content
          class="
            z-[var(--z-dropdown)]
            overflow-hidden
            glass
            rounded-[var(--radius-lg)]
            shadow-lg
            animate-dropdown-in
          "
        >
          <KobalteSelect.Listbox class="p-1 max-h-60 overflow-auto" />
        </KobalteSelect.Content>
      </KobalteSelect.Portal>

      <Show when={local.description && !local.error}>
        <KobalteSelect.Description class="text-xs text-[var(--text-tertiary)]">
          {local.description}
        </KobalteSelect.Description>
      </Show>

      <Show when={local.error}>
        <KobalteSelect.ErrorMessage class="text-xs text-[var(--error)]">
          {local.error}
        </KobalteSelect.ErrorMessage>
      </Show>
    </KobalteSelect>
  )
}
