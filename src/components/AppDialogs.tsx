/**
 * App Dialogs Component
 *
 * Renders all top-level dialog components (workflow, checkpoint,
 * export, changelog, update) and command palette with its handlers.
 */

import { type Component, Show } from 'solid-js'
import { useNotification } from '../contexts/notification'
import { type ExportOptions, exportConversation } from '../lib/export-conversation'
import type { UpdateInfo } from '../services/auto-updater'
import { useLayout } from '../stores/layout'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import { useWorkflows } from '../stores/workflows'
import { CommandPalette, createDefaultCommands } from './CommandPalette'
import { QuickModelPicker } from './chat/QuickModelPicker'
import { SessionSwitcher } from './chat/SessionSwitcher'
import { ChangelogDialog, markChangelogSeen } from './dialogs/ChangelogDialog'
import { CheckpointDialog } from './dialogs/CheckpointDialog'
import { ExportOptionsDialog } from './dialogs/ExportOptionsDialog'
import { ToolListDialog } from './dialogs/ToolListDialog'
import { UpdateDialog } from './dialogs/UpdateDialog'
import { WorkflowDialog } from './dialogs/WorkflowDialog'

export interface AppDialogsProps {
  workflowDialogOpen: boolean
  setWorkflowDialogOpen: (v: boolean) => void
  checkpointDialogOpen: boolean
  setCheckpointDialogOpen: (v: boolean) => void
  exportDialogOpen: boolean
  setExportDialogOpen: (v: boolean) => void
  changelogOpen: boolean
  setChangelogOpen: (v: boolean) => void
  updateDialogOpen: boolean
  setUpdateDialogOpen: (v: boolean) => void
  toolListDialogOpen: boolean
  setToolListDialogOpen: (v: boolean) => void
  updateInfo: UpdateInfo | null
  onInstallUpdate: () => Promise<void>
  setProjectHubVisible: (v: boolean) => void
}

export const AppDialogs: Component<AppDialogsProps> = (props) => {
  const { toggleSettings } = useLayout()
  const {
    sessionSwitcherOpen,
    setSessionSwitcherOpen,
    quickModelPickerOpen,
    setQuickModelPickerOpen,
  } = useLayout()
  const { currentProject } = useProject()
  const { messages, currentSession, createNewSession, createCheckpoint } = useSession()
  const { loadWorkflows } = useWorkflows()
  const { info } = useNotification()

  return (
    <>
      <Show when={quickModelPickerOpen()}>
        <QuickModelPicker
          open={quickModelPickerOpen()}
          onClose={() => setQuickModelPickerOpen(false)}
        />
      </Show>
      <Show when={sessionSwitcherOpen()}>
        <SessionSwitcher
          open={sessionSwitcherOpen()}
          onClose={() => setSessionSwitcherOpen(false)}
        />
      </Show>
      <CommandPalette
        commands={createDefaultCommands({
          newChat: async () => {
            if (!currentProject()) {
              props.setProjectHubVisible(true)
              return
            }
            await createNewSession()
            props.setProjectHubVisible(false)
          },
          exportChat: () => {
            if (messages().length === 0) return
            props.setExportDialogOpen(true)
          },
          initProject: () => {
            const project = currentProject()
            if (!project) return
            const prompt = [
              'Analyze this project and generate a comprehensive `.ava-instructions` file in the project root.',
              'Include:',
              '- Project overview (what it does, tech stack)',
              '- Architecture and key directories',
              '- Build, test, and lint commands',
              '- Code style conventions (naming, formatting, patterns)',
              '- Important rules and gotchas',
              '',
              `Project directory: ${project.directory}`,
            ].join('\n')
            window.dispatchEvent(new CustomEvent('ava:set-input', { detail: { text: prompt } }))
          },
          openSettings: toggleSettings,
          saveWorkflow: () => {
            if (messages().length === 0) return
            props.setWorkflowDialogOpen(true)
          },
          browseWorkflows: () => {
            loadWorkflows(currentProject()?.id)
          },
          importWorkflows: async () => {
            try {
              const { importFromFile } = useWorkflows()
              const count = await importFromFile()
              info('Imported', `${count} workflow${count !== 1 ? 's' : ''} imported`)
            } catch (err) {
              info('Import failed', err instanceof Error ? err.message : 'Unknown error')
            }
          },
          exportWorkflows: () => {
            const { exportAll } = useWorkflows()
            exportAll()
          },
          openProjectStats: () => {
            window.dispatchEvent(new CustomEvent('ava:open-project-stats'))
          },
          saveCheckpoint: () => {
            if (messages().length === 0) return
            props.setCheckpointDialogOpen(true)
          },
          browseTools: () => {
            props.setToolListDialogOpen(true)
          },
        })}
      />
      <Show when={props.workflowDialogOpen}>
        <WorkflowDialog
          open={props.workflowDialogOpen}
          onClose={() => props.setWorkflowDialogOpen(false)}
        />
      </Show>
      <Show when={props.checkpointDialogOpen}>
        <CheckpointDialog
          open={props.checkpointDialogOpen}
          onClose={() => props.setCheckpointDialogOpen(false)}
          onSave={(desc) => {
            void createCheckpoint(desc).then((id) => {
              if (id) info('Checkpoint saved', desc)
            })
          }}
        />
      </Show>
      <Show when={props.exportDialogOpen}>
        <ExportOptionsDialog
          open={props.exportDialogOpen}
          onClose={() => props.setExportDialogOpen(false)}
          onExport={(opts: ExportOptions) => {
            exportConversation(messages(), currentSession()?.name, opts)
          }}
        />
      </Show>
      <Show when={props.changelogOpen}>
        <ChangelogDialog
          open={props.changelogOpen}
          onClose={() => {
            props.setChangelogOpen(false)
            markChangelogSeen()
          }}
        />
      </Show>
      <Show when={props.updateDialogOpen}>
        <UpdateDialog
          open={props.updateDialogOpen}
          updateInfo={props.updateInfo}
          onClose={() => props.setUpdateDialogOpen(false)}
          onInstall={props.onInstallUpdate}
        />
      </Show>
      <Show when={props.toolListDialogOpen}>
        <ToolListDialog
          open={props.toolListDialogOpen}
          onClose={() => props.setToolListDialogOpen(false)}
        />
      </Show>
    </>
  )
}
