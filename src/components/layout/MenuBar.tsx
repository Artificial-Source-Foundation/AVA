/**
 * Menu Bar Component
 *
 * Desktop-style menu bar (File, View, Help) integrated into the custom titlebar.
 * Provides discoverability of common actions like session management,
 * panel toggles, and settings access.
 */

import { open } from '@tauri-apps/plugin-dialog'
import { type Component, createSignal, For, onCleanup, Show } from 'solid-js'
import { logError } from '../../services/logger'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'

type MenuId = 'file' | 'view' | 'help'

interface MenuItem {
  label: string
  shortcut?: string
  action?: () => void
  separator?: boolean
  disabled?: boolean
}

export const MenuBar: Component = () => {
  const { createNewSession, loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()
  const { openProjectHub, toggleSidebar, toggleBottomPanel, toggleRightPanel, openSettings } =
    useLayout()
  const { openDirectory } = useProject()

  const [openMenu, setOpenMenu] = createSignal<MenuId | null>(null)
  let menuBarRef: HTMLDivElement | undefined

  const handleOpenProject = async () => {
    let selected: string | string[] | null = null
    try {
      selected = await open({
        directory: true,
        title: 'Select Project Folder',
      })
    } catch {
      /* ignore in non-Tauri */
      return
    }
    if (!selected || typeof selected !== 'string') return

    try {
      await openDirectory(selected)
    } catch (error) {
      logError('MenuBar', 'Failed to open directory', error)
      return
    }

    try {
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
    } catch (error) {
      logError('MenuBar', 'Session restore failed after open', error)
    }
  }

  const handleNewWindow = async () => {
    try {
      const { WebviewWindow } = await import('@tauri-apps/api/webviewWindow')
      new WebviewWindow(`ava-${Date.now()}`, {
        url: '/',
        title: 'AVA',
        width: 1200,
        height: 800,
        minWidth: 640,
        minHeight: 480,
        decorations: false,
      })
    } catch {
      /* ignore in non-Tauri */
    }
  }

  const handleCloseWindow = async () => {
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().close()
    } catch {
      /* ignore in non-Tauri */
    }
  }

  const openCommandPalette = () => {
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'k', ctrlKey: true, bubbles: true }))
  }

  const menus: Record<MenuId, MenuItem[]> = {
    file: [
      { label: 'New Session', shortcut: 'Ctrl+N', action: () => createNewSession() },
      { label: 'New Window', shortcut: 'Ctrl+Shift+N', action: handleNewWindow },
      { separator: true, label: '' },
      { label: 'Open Project...', action: handleOpenProject },
      { label: 'Switch Project', action: openProjectHub },
      { separator: true, label: '' },
      { label: 'Close Window', action: handleCloseWindow },
    ],
    view: [
      { label: 'Toggle Sidebar', shortcut: 'Ctrl+S', action: toggleSidebar },
      { label: 'Toggle Memory', shortcut: 'Ctrl+J', action: toggleBottomPanel },
      { label: 'Toggle Activity', action: toggleRightPanel },
      { separator: true, label: '' },
      { label: 'Command Palette', shortcut: 'Ctrl+K', action: openCommandPalette },
    ],
    help: [
      { label: 'Keyboard Shortcuts', action: openSettings },
      { separator: true, label: '' },
      { label: 'Settings', shortcut: 'Ctrl+,', action: openSettings },
      { label: 'About AVA', disabled: true },
    ],
  }

  const toggleMenu = (id: MenuId) => {
    setOpenMenu((prev) => (prev === id ? null : id))
  }

  const handleItemClick = (item: MenuItem) => {
    if (item.disabled || item.separator) return
    setOpenMenu(null)
    item.action?.()
  }

  const handleMouseDown = (e: MouseEvent) => {
    if (openMenu() && menuBarRef && !menuBarRef.contains(e.target as Node)) {
      setOpenMenu(null)
    }
  }

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Escape' && openMenu()) {
      setOpenMenu(null)
    }
  }

  document.addEventListener('mousedown', handleMouseDown)
  document.addEventListener('keydown', handleKeyDown)

  onCleanup(() => {
    document.removeEventListener('mousedown', handleMouseDown)
    document.removeEventListener('keydown', handleKeyDown)
  })

  const menuLabels: { id: MenuId; label: string }[] = [
    { id: 'file', label: 'File' },
    { id: 'view', label: 'View' },
    { id: 'help', label: 'Help' },
  ]

  return (
    <div ref={menuBarRef} data-menubar class="flex items-center gap-0.5 relative">
      <For each={menuLabels}>
        {(menu) => (
          <div class="relative">
            <button
              type="button"
              class={`text-[var(--text-xs)] font-medium px-2 py-1 rounded-[var(--radius-sm)] transition-colors ${
                openMenu() === menu.id
                  ? 'text-[var(--text-primary)] bg-[var(--alpha-white-8)]'
                  : 'text-[var(--text-muted)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-8)]'
              }`}
              onClick={() => toggleMenu(menu.id)}
              onMouseEnter={() => openMenu() && setOpenMenu(menu.id)}
            >
              {menu.label}
            </button>

            <Show when={openMenu() === menu.id}>
              <div class="absolute top-full left-0 mt-1 min-w-[200px] py-1 bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-lg)] shadow-lg z-[var(--z-popover)]">
                <For each={menus[menu.id]}>
                  {(item) => (
                    <Show
                      when={!item.separator}
                      fallback={<div class="h-px bg-[var(--border-subtle)] mx-2 my-1" />}
                    >
                      <button
                        type="button"
                        class={`w-full flex items-center justify-between px-3 py-1.5 text-[var(--text-sm)] transition-colors ${
                          item.disabled
                            ? 'text-[var(--text-muted)] opacity-40 cursor-not-allowed'
                            : 'text-[var(--text-secondary)] hover:text-[var(--text-primary)] hover:bg-[var(--alpha-white-5)]'
                        }`}
                        disabled={item.disabled}
                        onClick={() => handleItemClick(item)}
                      >
                        <span>{item.label}</span>
                        <Show when={item.shortcut}>
                          <span class="text-[var(--text-2xs)] text-[var(--text-muted)] ml-4">
                            {item.shortcut}
                          </span>
                        </Show>
                      </button>
                    </Show>
                  )}
                </For>
              </div>
            </Show>
          </div>
        )}
      </For>
    </div>
  )
}
