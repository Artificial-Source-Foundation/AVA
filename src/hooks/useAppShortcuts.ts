/**
 * App Shortcut Registration Hook
 *
 * Registers all global keyboard shortcut actions and sets up
 * the shortcut listener. Returns a cleanup function.
 */

import { onCleanup } from 'solid-js'
import { useNotification } from '../contexts/notification'
import { useLayout } from '../stores/layout'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useShortcuts } from '../stores/shortcuts'

export function registerAppShortcuts(
  setExportDialogOpen: (v: boolean) => void,
  setCheckpointDialogOpen: (v: boolean) => void,
  setProjectHubVisible: (v: boolean) => void
): void {
  const {
    toggleSidebar,
    toggleSettings,
    toggleBottomPanel,
    toggleModelBrowser,
    toggleChatSearch,
    toggleSessionSwitcher,
    toggleQuickModelPicker,
    toggleExpandedEditor,
    bottomPanelTab,
    switchBottomPanelTab,
    bottomPanelVisible,
  } = useLayout()
  const { currentProject } = useProject()
  const { messages, undoFileChange, redoFileChange, createNewSession } = useSession()
  const { registerAction, setupShortcutListener } = useShortcuts()
  const { info } = useNotification()

  registerAction('toggle-sidebar', toggleSidebar)
  registerAction('toggle-settings', toggleSettings)
  registerAction('toggle-bottom-panel', toggleBottomPanel)
  registerAction('model-browser', toggleModelBrowser)
  registerAction('quick-model-picker', toggleQuickModelPicker)
  registerAction('session-switcher', toggleSessionSwitcher)
  registerAction('search-chat', toggleChatSearch)
  registerAction('expanded-editor', toggleExpandedEditor)
  registerAction('toggle-terminal', () => {
    if (bottomPanelVisible() && bottomPanelTab() === 'terminal') {
      toggleBottomPanel()
    } else {
      switchBottomPanelTab('terminal')
    }
  })
  registerAction('export-chat', () => {
    const msgs = messages()
    if (msgs.length === 0) return
    setExportDialogOpen(true)
  })
  registerAction('undo-file-change', async () => {
    const filePath = await undoFileChange()
    if (filePath) {
      const name = filePath.split('/').pop() || filePath
      info('Undone', `Reverted ${name}`)
    }
  })
  registerAction('redo-file-change', async () => {
    const filePath = await redoFileChange()
    if (filePath) {
      const name = filePath.split('/').pop() || filePath
      info('Redone', `Re-applied change to ${name}`)
    }
  })
  registerAction('stash-prompt', () => {
    window.dispatchEvent(new CustomEvent('ava:stash-prompt'))
  })
  registerAction('restore-prompt', () => {
    window.dispatchEvent(new CustomEvent('ava:restore-prompt'))
  })
  registerAction('save-checkpoint', () => {
    if (messages().length === 0) return
    setCheckpointDialogOpen(true)
  })
  registerAction('new-chat', async () => {
    if (!currentProject()) {
      setProjectHubVisible(true)
      return
    }
    await createNewSession()
    setProjectHubVisible(false)
  })

  const cleanupShortcuts = setupShortcutListener()
  onCleanup(cleanupShortcuts)
}
