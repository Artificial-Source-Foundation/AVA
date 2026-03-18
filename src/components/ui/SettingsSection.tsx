/**
 * SettingsSection Component
 *
 * Section wrapper for settings panels with icon + title header.
 */

import { type Component, type JSX, Show } from 'solid-js'

export interface SettingsSectionProps {
  /** Section title */
  title: string
  /** Optional icon component (rendered at 16px, accent color) */
  icon?: Component
  /** Optional description below title */
  description?: string
  /** Section content */
  children: JSX.Element
  /** Additional CSS classes */
  class?: string
}

export const SettingsSection: Component<SettingsSectionProps> = (props) => {
  return (
    <div class={`flex flex-col gap-3 ${props.class ?? ''}`}>
      <div class="flex flex-col gap-1">
        <div class="flex items-center gap-2">
          <Show when={props.icon}>
            {(_icon) => {
              const IconComp = props.icon!
              return (
                <span class="text-[var(--accent)] flex-shrink-0">
                  <IconComp />
                </span>
              )
            }}
          </Show>
          <h3 class="text-[14px] font-semibold text-[var(--text-primary)]">{props.title}</h3>
        </div>
        <Show when={props.description}>
          <p class="text-[11px] text-[var(--text-muted)] leading-relaxed">{props.description}</p>
        </Show>
      </div>
      <div class="flex flex-col gap-3">{props.children}</div>
    </div>
  )
}
