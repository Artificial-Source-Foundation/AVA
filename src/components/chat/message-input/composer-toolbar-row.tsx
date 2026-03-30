import type { Component, JSX } from 'solid-js'

export const ComposerToolbarRow: Component<{
  left: JSX.Element
  right?: JSX.Element
}> = (props) => (
  <div class="flex items-center justify-between gap-2 text-[var(--text-xs)] text-[var(--text-tertiary)] select-none min-w-0 overflow-x-auto flex-wrap">
    <div class="flex items-center gap-1 min-w-0">{props.left}</div>
    <div class="flex items-center gap-2 min-w-0">{props.right}</div>
  </div>
)

export const ComposerToolbarDivider: Component = () => (
  <span class="h-4 w-px shrink-0 bg-[var(--border-default)]" />
)
