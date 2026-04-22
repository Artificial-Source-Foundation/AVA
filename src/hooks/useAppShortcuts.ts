/**
 * App Shortcut Registration Hook
 *
 * Registers all global keyboard shortcut actions and sets up
 * the shortcut listener. Returns a cleanup function.
 */

import { onCleanup } from 'solid-js'
import { cycleReasoningEffort } from '../components/chat/message-input/toolbar-buttons'
import { useNotification } from '../contexts/notification'
import { useLayout } from '../stores/layout'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useSettings } from '../stores/settings'
import { useShortcuts } from '../stores/shortcuts'
import { useAgent } from './useAgent'

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
    openModelBrowser,
    toggleExpandedEditor,
    bottomPanelTab,
    switchBottomPanelTab,
    bottomPanelVisible,
  } = useLayout()
  const { currentProject } = useProject()
  const { messages, undoFileChange, redoFileChange, createNewSession } = useSession()
  const { registerAction, setupShortcutListener } = useShortcuts()
  const { settings, updateSettings } = useSettings()
  const { error: notifyError, info } = useNotification()
  const agent = useAgent()

  registerAction('toggle-sidebar', toggleSidebar)
  registerAction('toggle-settings', toggleSettings)
  registerAction('toggle-bottom-panel', toggleBottomPanel)
  registerAction('model-browser', toggleModelBrowser)
  registerAction('quick-model-picker', openModelBrowser)
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
  registerAction('cycle-thinking', () => {
    const current = settings().generation.reasoningEffort
    const next = cycleReasoningEffort(current)
    updateSettings({
      generation: {
        ...settings().generation,
        reasoningEffort: next,
        thinkingEnabled: next !== 'off',
      },
    })
    info('Thinking', next === 'off' ? 'Reasoning off' : `Reasoning: ${next}`)
  })
  registerAction('copy-last-response', () => {
    const msgs = messages()
    const lastAssistant = [...msgs].reverse().find((m) => m.role === 'assistant')
    if (!lastAssistant) return
    void navigator.clipboard.writeText(lastAssistant.content).then(() => {
      info('Copied', 'Response copied')
    })
  })
  registerAction('command-palette-slash', () => {
    // Dispatch Ctrl+K to open command palette (alias for Ctrl+/)
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'k', ctrlKey: true, bubbles: true }))
  })
  registerAction('voice-toggle', () => {
    window.dispatchEvent(new CustomEvent('ava:voice-toggle'))
  })
  registerAction('mode-cycle', () => {
    if (agent.hasPrimaryAgentProfiles()) {
      void agent
        .cyclePrimaryAgentProfile(1)
        .then((primaryAgentId) => {
          if (primaryAgentId) {
            info('Primary Agent', primaryAgentId)
          }
        })
        .catch((error) => {
          const message = error instanceof Error ? error.message : String(error)
          notifyError('Primary Agent', message)
        })
      return
    }

    agent.togglePlanMode()
    info('Mode', agent.isPlanMode() ? 'Plan mode' : 'Act mode')
  })
  registerAction('mode-cycle-reverse', () => {
    if (agent.hasPrimaryAgentProfiles()) {
      void agent
        .cyclePrimaryAgentProfile(-1)
        .then((primaryAgentId) => {
          if (primaryAgentId) {
            info('Primary Agent', primaryAgentId)
          }
        })
        .catch((error) => {
          const message = error instanceof Error ? error.message : String(error)
          notifyError('Primary Agent', message)
        })
      return
    }

    agent.togglePlanMode()
    info('Mode', agent.isPlanMode() ? 'Plan mode' : 'Act mode')
  })

  const cleanupShortcuts = setupShortcutListener()
  onCleanup(cleanupShortcuts)
}
