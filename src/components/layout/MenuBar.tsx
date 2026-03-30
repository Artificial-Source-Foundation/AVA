/**
 * Menu Bar Component
 *
 * Desktop-style menu bar (File, View, Help) integrated into the custom titlebar.
 * Provides discoverability of common actions like session management,
 * panel toggles, and settings access.
 */

import { getVersion } from '@tauri-apps/api/app'
import { open } from '@tauri-apps/plugin-dialog'
import { type Component, createSignal, For, onCleanup, onMount, Show } from 'solid-js'
import { logError } from '../../services/logger'
import { useLayout } from '../../stores/layout'
import { useProject } from '../../stores/project'
import { useSession } from '../../stores/session'
import { Dialog } from '../ui/Dialog'

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
  const [aboutOpen, setAboutOpen] = createSignal(false)
  const [appVersion, setAppVersion] = createSignal('unknown')
  let menuBarRef: HTMLDivElement | undefined

  const handleAbout = () => {
    void getVersion()
      .then((v) => setAppVersion(v))
      .catch(() => setAppVersion('unknown'))
    setAboutOpen(true)
  }

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

  const handleExportChat = () => {
    document.dispatchEvent(
      new KeyboardEvent('keydown', { key: 'E', ctrlKey: true, shiftKey: true, bubbles: true })
    )
  }

  const menus: Record<MenuId, MenuItem[]> = {
    file: [
      { label: 'New Session', shortcut: 'Ctrl+N', action: () => createNewSession() },
      { label: 'New Window', shortcut: 'Ctrl+Shift+N', action: handleNewWindow },
      { separator: true, label: '' },
      { label: 'Open Project...', action: handleOpenProject },
      { label: 'Switch Project', action: openProjectHub },
      { separator: true, label: '' },
      { label: 'Export Chat...', shortcut: 'Ctrl+Shift+E', action: handleExportChat },
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
      { label: 'About AVA', action: handleAbout },
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

  onMount(() => {
    document.addEventListener('mousedown', handleMouseDown)
    document.addEventListener('keydown', handleKeyDown)
  })

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
    <div ref={menuBarRef} data-menubar class="flex items-center relative">
      <For each={menuLabels}>
        {(menu) => (
          <div class="relative">
            <button
              type="button"
              class="menubar-item"
              classList={{ 'menubar-item--active': openMenu() === menu.id }}
              onClick={() => toggleMenu(menu.id)}
              onMouseEnter={() => openMenu() && setOpenMenu(menu.id)}
              aria-haspopup="menu"
              aria-expanded={openMenu() === menu.id}
              aria-controls={`menubar-${menu.id}`}
            >
              {menu.label}
            </button>

            <Show when={openMenu() === menu.id}>
              <div
                id={`menubar-${menu.id}`}
                class="menubar-dropdown animate-dropdown-in"
                role="menu"
              >
                <For each={menus[menu.id]}>
                  {(item) => (
                    <Show when={!item.separator} fallback={<div class="menubar-dropdown-sep" />}>
                      <button
                        type="button"
                        class="menubar-dropdown-item"
                        classList={{ 'menubar-dropdown-item--disabled': item.disabled }}
                        disabled={item.disabled}
                        onClick={() => handleItemClick(item)}
                        role="menuitem"
                      >
                        <span>{item.label}</span>
                        <Show when={item.shortcut}>
                          <span class="menubar-dropdown-shortcut">{item.shortcut}</span>
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
      <Dialog
        open={aboutOpen()}
        onOpenChange={setAboutOpen}
        title="About AVA"
        size="sm"
        showCloseButton
      >
        <div class="flex flex-col items-center gap-3 py-4 text-center">
          <div
            class="w-12 h-12 rounded-xl bg-[var(--accent)] flex items-center justify-center"
            style={{ 'box-shadow': '0 0 12px rgba(139, 92, 246, 0.2)' }}
          >
            <span class="text-white text-xl font-bold">A</span>
          </div>
          <div>
            <p class="text-base font-semibold text-[var(--text-primary)]">AVA</p>
            <p class="text-xs text-[var(--text-secondary)] mt-0.5">v{appVersion()}</p>
          </div>
          <p class="text-xs text-[var(--text-muted)] max-w-[280px] leading-relaxed">
            AI-powered coding assistant with multi-agent orchestration, 21 LLM providers, and a full
            developer toolkit.
          </p>
          <a
            href="https://github.com/ASF-GROUP/AVA"
            target="_blank"
            rel="noopener noreferrer"
            class="text-xs text-[var(--accent)] hover:underline mt-1"
          >
            github.com/ASF-GROUP/AVA
          </a>
        </div>
      </Dialog>
    </div>
  )
}
