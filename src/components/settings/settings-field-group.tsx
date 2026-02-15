import type { Component, JSXElement } from 'solid-js'

interface FieldGroupProps {
  label: string
  children: JSXElement
}

export const FieldGroup: Component<FieldGroupProps> = (props) => (
  <div>
    <span class="block text-xs font-medium text-[var(--text-muted)] mb-1.5">{props.label}</span>
    {props.children}
  </div>
)
