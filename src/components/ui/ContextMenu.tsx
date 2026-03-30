/**
 * Context Menu Component
 *
 * Reusable right-click context menu matching the Pencil design spec.
 * 200px wide, rounded-8 outer, 30px rows with rounded-6.
 * Positions itself near the cursor. Auto-closes on click outside or Escape.
 */

import { type Component, createSignal, For, onCleanup, onMount, Show } from 'solid-js'
import { Portal } from 'solid-js/web'

export interface ContextMenuItem {
  label: string
  icon?: Component<{ class?: string }>
  action: () => void
  danger?: boolean
  disabled?: boolean
  separator?: boolean
  /** Optional keyboard shortcut badge (e.g. "Ctrl+C") */
  kbd?: string
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
  const [position, setPosition] = createSignal({ x: 0, y: 0 })
  const [isPositioned, setIsPositioned] = createSignal(false)

  onMount(() => {
    const updatePosition = () => {
      if (!menuRef) return

      const rect = menuRef.getBoundingClientRect()
      const nextX = Math.max(8, Math.min(props.x, window.innerWidth - rect.width - 8))
      const nextY = Math.max(8, Math.min(props.y, window.innerHeight - rect.height - 8))

      setPosition({ x: nextX, y: nextY })
      setIsPositioned(true)
    }

    if (menuRef) {
      updatePosition()
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
    window.addEventListener('resize', updatePosition)

    onCleanup(() => {
      document.removeEventListener('mousedown', handleClickOutside)
      document.removeEventListener('keydown', handleEscape)
      window.removeEventListener('resize', updatePosition)
    })
  })

  return (
    <Portal>
      <div
        ref={menuRef}
        class="fixed z-[var(--z-popover)] animate-context-menu"
        style={{
          left: `${position().x}px`,
          top: `${position().y}px`,
          visibility: isPositioned() ? 'visible' : 'hidden',
          width: '200px',
          padding: '6px 4px',
          background: 'var(--dropdown-surface)',
          border: '1px solid var(--dropdown-border)',
          'border-radius': '8px',
          'box-shadow': '0 0 20px var(--alpha-black-40)',
        }}
      >
        <For each={props.items}>
          {(item) => (
            <Show
              when={!item.separator}
              fallback={
                <div style={{ padding: '4px 0' }}>
                  <div
                    style={{
                      height: '1px',
                      background: 'var(--dropdown-border)',
                    }}
                  />
                </div>
              }
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
                class="
                  group/item w-full flex items-center text-left
                  transition-colors duration-[var(--duration-fast)]
                  disabled:opacity-40 disabled:cursor-not-allowed
                "
                classList={{
                  'hover:bg-[var(--gray-3)] hover:text-[var(--gray-12)]':
                    !item.danger && !item.disabled,
                  'hover:bg-[var(--error-subtle)]': !!item.danger && !item.disabled,
                }}
                style={{
                  height: '30px',
                  'border-radius': '6px',
                  padding: '0 10px',
                  gap: '10px',
                  'font-size': '12px',
                  color: item.danger ? 'var(--error)' : 'var(--gray-9)',
                }}
              >
                <Show when={item.icon}>
                  {(Icon) => {
                    const IconComp = Icon()
                    return (
                      <span
                        class="flex-shrink-0 flex items-center justify-center"
                        classList={{
                          'text-[var(--gray-6)] group-hover/item:text-[var(--gray-9)]':
                            !item.danger,
                          'text-[var(--error)]': !!item.danger,
                        }}
                        style={{
                          width: '13px',
                          height: '13px',
                        }}
                      >
                        <IconComp class="w-[13px] h-[13px]" />
                      </span>
                    )
                  }}
                </Show>
                <span class="flex-1 min-w-0 truncate">{item.label}</span>
                <Show when={item.kbd}>
                  <span
                    style={{
                      'font-size': '9px',
                      'font-family': "var(--font-ui-mono, 'Geist Mono', ui-monospace, monospace)",
                      color: 'var(--gray-6)',
                      background: 'var(--dropdown-border)',
                      'border-radius': '4px',
                      padding: '2px 6px',
                      'flex-shrink': '0',
                    }}
                  >
                    {item.kbd}
                  </span>
                </Show>
              </button>
            </Show>
          )}
        </For>
      </div>
    </Portal>
  )
}
