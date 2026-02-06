/**
 * Context Menu Component
 *
 * Reusable right-click context menu. Positions itself near the cursor.
 * Auto-closes on click outside or Escape.
 */

import { type Component, For, onCleanup, onMount, Show } from 'solid-js'

export interface ContextMenuItem {
  label: string
  icon?: Component<{ class?: string }>
  action: () => void
  danger?: boolean
  disabled?: boolean
  separator?: boolean
}

interface ContextMenuProps {
  x: number
  y: number
  items: ContextMenuItem[]
  onClose: () => void
}

export const ContextMenu: Component<ContextMenuProps> = (props) => {
  // oxlint-disable-next-line no-unassigned-vars -- SolidJS ref pattern: assigned via ref={} in JSX
  let menuRef: HTMLDivElement | undefined

  onMount(() => {
    // Adjust position if menu would overflow viewport
    if (menuRef) {
      const rect = menuRef.getBoundingClientRect()
      if (rect.right > window.innerWidth) {
        menuRef.style.left = `${props.x - rect.width}px`
      }
      if (rect.bottom > window.innerHeight) {
        menuRef.style.top = `${props.y - rect.height}px`
      }
    }

    const handleClickOutside = (e: MouseEvent) => {
      if (menuRef && !menuRef.contains(e.target as Node)) {
        props.onClose()
      }
    }
    const handleEscape = (e: KeyboardEvent) => {
      if (e.key === 'Escape') props.onClose()
    }

    // Delay to avoid the right-click itself closing the menu
    requestAnimationFrame(() => {
      document.addEventListener('mousedown', handleClickOutside)
      document.addEventListener('keydown', handleEscape)
    })

    onCleanup(() => {
      document.removeEventListener('mousedown', handleClickOutside)
      document.removeEventListener('keydown', handleEscape)
    })
  })

  return (
    <div
      ref={menuRef}
      class="
        fixed z-[var(--z-popover)]
        min-w-[160px]
        bg-[var(--surface-overlay)]
        border border-[var(--border-default)]
        rounded-[var(--radius-lg)]
        shadow-lg
        py-1
        animate-context-menu
      "
      style={{ left: `${props.x}px`, top: `${props.y}px` }}
    >
      <For each={props.items}>
        {(item) => (
          <Show
            when={!item.separator}
            fallback={<div class="h-px bg-[var(--border-subtle)] mx-2 my-1" />}
          >
            <button
              type="button"
              disabled={item.disabled}
              onClick={() => {
                if (!item.disabled) {
                  item.action()
                  props.onClose()
                }
              }}
              class={`
                w-full flex items-center gap-2.5 px-3 py-1.5
                text-xs text-left
                transition-colors duration-[var(--duration-fast)]
                disabled:opacity-40 disabled:cursor-not-allowed
                ${
                  item.danger
                    ? 'text-[var(--error)] hover:bg-[var(--error-subtle)]'
                    : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)]'
                }
              `}
            >
              <Show when={item.icon}>
                {(Icon) => {
                  const IconComp = Icon()
                  return <IconComp class="w-3.5 h-3.5" />
                }}
              </Show>
              {item.label}
            </button>
          </Show>
        )}
      </For>
    </div>
  )
}
