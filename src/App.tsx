/**
 * App Component
 * Root component with initialization logic
 *
 * Note: Preview mode is handled in index.tsx to avoid loading Node.js dependencies
 * Access the design system preview at: http://localhost:1420/?preview=true
 */

import { isTauri } from '@tauri-apps/api/core'
import { createEffect, createSignal, on, onCleanup, onMount, Show } from 'solid-js'
import { CommandPalette, createDefaultCommands } from './components/CommandPalette'
import { QuickModelPicker } from './components/chat/QuickModelPicker'
import { SessionSwitcher } from './components/chat/SessionSwitcher'
import {
  ChangelogDialog,
  markChangelogSeen,
  shouldShowChangelog,
} from './components/dialogs/ChangelogDialog'
import { CheckpointDialog } from './components/dialogs/CheckpointDialog'
import { ExportOptionsDialog } from './components/dialogs/ExportOptionsDialog'
import type { OnboardingData } from './components/dialogs/OnboardingDialog'
import { OnboardingScreen } from './components/dialogs/OnboardingDialog'
import { UpdateDialog } from './components/dialogs/UpdateDialog'
import { WorkflowDialog } from './components/dialogs/WorkflowDialog'
import { AppShell } from './components/layout'
import { ProjectHub } from './components/projects'
import { SplashScreen } from './components/SplashScreen'
import { validateEnv } from './config/env'
import { useNotification } from './contexts/notification'
import { type ExportOptions, exportConversation } from './lib/export-conversation'
import { checkForUpdate, downloadAndInstallUpdate, type UpdateInfo } from './services/auto-updater'
import { initCoreBridge } from './services/core-bridge'
import { initDatabase } from './services/database'
import { initDeepLinks } from './services/deep-link'
import {
  installConsoleCapture,
  setDevConsoleLogLevel,
  setLogDirectory,
} from './services/dev-console'
import { getLogDirectory, initLogger, logError, logInfo, setLogLevel } from './services/logger'
import { syncModelsCatalog } from './services/providers/models-dev-catalog'
import { initSettingsFS } from './services/settings-fs'
import { type ScheduledWorkflow, startScheduler } from './services/workflow-scheduler'
import { useLayout } from './stores/layout'
import { useProject } from './stores/project'
import { useSession } from './stores/session'
import {
  applyAppearance,
  detectEnvApiKeys,
  envKeysDetected,
  hydrateSettingsFromFS,
  pushSettingsToCore,
  refreshAllProviderModels,
  setupSystemThemeListener,
  syncAllApiKeys,
  syncCredentialsToDisk,
  useSettings,
} from './stores/settings'
import { useShortcuts } from './stores/shortcuts'
import { useWorkflows } from './stores/workflows'

const SPLASH_MIN_MS = 800
const BUILT_IN_EXTENSION_COUNT = 20
const BUILT_IN_TOOL_COUNT = 39

function App() {
  const [isInitializing, setIsInitializing] = createSignal(true)
  const [initError, setInitError] = createSignal<string | null>(null)
  const [notTauri, setNotTauri] = createSignal(false)
  const [splashStatus, setSplashStatus] = createSignal('')

  const {
    toggleSidebar,
    toggleSettings,
    toggleBottomPanel,
    toggleModelBrowser,
    toggleChatSearch,
    toggleSessionSwitcher,
    sessionSwitcherOpen,
    setSessionSwitcherOpen,
    toggleQuickModelPicker,
    quickModelPickerOpen,
    setQuickModelPickerOpen,
    toggleExpandedEditor,
    bottomPanelTab,
    switchBottomPanelTab,
    bottomPanelVisible,
    projectHubVisible,
    setProjectHubVisible,
  } = useLayout()
  const { initializeProjects, currentProject } = useProject()
  const {
    loadSessionsForCurrentProject,
    restoreForCurrentProject,
    createNewSession,
    messages,
    currentSession,
    undoFileChange,
    redoFileChange,
    createCheckpoint,
  } = useSession()
  const { settings, updateSettings, updateProvider, isToolAutoApproved } = useSettings()
  const { registerAction, setupShortcutListener } = useShortcuts()
  const { info } = useNotification()
  const { loadWorkflows, getScheduledWorkflows, markWorkflowRun, workflows } = useWorkflows()
  const [workflowDialogOpen, setWorkflowDialogOpen] = createSignal(false)
  const [checkpointDialogOpen, setCheckpointDialogOpen] = createSignal(false)
  const [exportDialogOpen, setExportDialogOpen] = createSignal(false)
  const [updateDialogOpen, setUpdateDialogOpen] = createSignal(false)
  const [updateInfo, setUpdateInfo] = createSignal<UpdateInfo | null>(null)
  const [changelogOpen, setChangelogOpen] = createSignal(false)

  // Show toast when env API keys are detected (fires after init completes)
  createEffect(
    on(
      () => [isInitializing(), envKeysDetected()] as const,
      ([initializing, result]) => {
        if (initializing || !result || result.count === 0) return
        const names = result.providers.map((p) => p.charAt(0).toUpperCase() + p.slice(1))
        info(
          `Found ${result.count} API key${result.count > 1 ? 's' : ''} in environment: ${names.join(', ')}`
        )
      }
    )
  )

  createEffect(() => {
    const level = settings().logLevel
    setLogLevel(level)
    setDevConsoleLogLevel(level)
  })

  // Show toast when auto-compaction triggers
  onMount(() => {
    const handleCompacted = (e: Event) => {
      const { removed, tokensSaved } = (e as CustomEvent).detail
      info(
        'Context compacted',
        `Removed ${removed} message${removed !== 1 ? 's' : ''}, saved ~${Math.round(tokensSaved / 1000)}k tokens`
      )
    }
    window.addEventListener('ava:compacted', handleCompacted)
    onCleanup(() => window.removeEventListener('ava:compacted', handleCompacted))
  })

  // Auto-show changelog after update
  onMount(() => {
    if (shouldShowChangelog()) {
      setChangelogOpen(true)
    }
    const handleOpenChangelog = () => setChangelogOpen(true)
    window.addEventListener('ava:open-changelog', handleOpenChangelog)
    onCleanup(() => window.removeEventListener('ava:open-changelog', handleOpenChangelog))
  })

  // Auto-check for updates on startup + manual trigger via custom event
  onMount(() => {
    const doCheck = async () => {
      const result = await checkForUpdate()
      setUpdateInfo(result)
      if (result.available) {
        setUpdateDialogOpen(true)
      }
    }
    // Delay auto-check so it doesn't slow down startup
    const timer = setTimeout(() => void doCheck(), 5_000)
    const handleCheckUpdate = () => void doCheck()
    window.addEventListener('ava:check-update', handleCheckUpdate)
    onCleanup(() => {
      clearTimeout(timer)
      window.removeEventListener('ava:check-update', handleCheckUpdate)
    })
  })

  onMount(async () => {
    // Apply appearance settings (mode, accent, scale, font) to DOM immediately
    applyAppearance()

    // Always capture console output for debugging — uses $APPDATA/ava/logs/
    installConsoleCapture()

    // Listen for OS theme changes when mode is 'system'
    const cleanupTheme = setupSystemThemeListener()
    onCleanup(cleanupTheme)

    // Register shortcut actions and install global listener
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
      // Handled via CustomEvent so MessageInput can read its local input signal
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

    // Guard: AVA requires the Tauri runtime
    if (!isTauri()) {
      setNotTauri(true)
      setIsInitializing(false)
      return
    }

    // Show window early so the splash screen is visible during initialization.
    // The splash covers the full viewport, so there's no white flash.
    try {
      const { getCurrentWindow } = await import('@tauri-apps/api/window')
      await getCurrentWindow().show()
    } catch {
      /* ignore in non-Tauri */
    }

    const splashStart = Date.now()

    try {
      // Clear corrupted resizable panel sizes from localStorage
      // (caused size.endsWith crash in @corvu/resizable)
      for (const key of ['ava-sidebar-sizes', 'ava-bottom-sizes']) {
        try {
          const raw = localStorage.getItem(key)
          if (raw) {
            const parsed = JSON.parse(raw)
            if (
              !Array.isArray(parsed) ||
              parsed.length !== 2 ||
              !parsed.every((v: unknown) => typeof v === 'number' && v > 0)
            ) {
              localStorage.removeItem(key)
            }
          }
        } catch {
          localStorage.removeItem(key)
        }
      }

      setSplashStatus('Starting logger...')
      await initLogger({
        version: 'AVA v2.0.0',
        platform: navigator.platform?.toLowerCase() ?? 'unknown',
        runtime: 'tauri',
        extensions: BUILT_IN_EXTENSION_COUNT,
        tools: BUILT_IN_TOOL_COUNT,
      })
      // Point dev-console file output to the same $APPDATA/ava/logs/ directory
      const logDir = getLogDirectory()
      if (logDir) setLogDirectory(logDir)
      logInfo('App', 'Initializing AVA...')

      validateEnv()

      setSplashStatus('Initializing platform...')
      await initSettingsFS()
      await hydrateSettingsFromFS()
      setLogLevel(settings().logLevel)
      setDevConsoleLogLevel(settings().logLevel)
      syncAllApiKeys()
      await detectEnvApiKeys()
      syncModelsCatalog().catch(() => {}) // Non-blocking — fallback to cache/hardcoded
      refreshAllProviderModels()

      setSplashStatus('Initializing core engine...')
      const cleanupCore = await initCoreBridge({
        contextLimit: 200_000,
        autoApprovalChecker: isToolAutoApproved,
      })
      onCleanup(cleanupCore)
      pushSettingsToCore()
      // Sync all localStorage credentials to ~/.ava/credentials.json for CLI sharing
      syncCredentialsToDisk()

      setSplashStatus('Loading database...')
      await initDatabase()

      setSplashStatus('Loading projects...')
      await initializeProjects()

      setSplashStatus('Loading plugins...')
      try {
        const { loadInstalledPlugins } = await import('./services/extension-loader')
        const { createExtensionAPI } = await import('../packages/core-v2/src/extensions/api.js')
        const { getMessageBus } = await import('../packages/core-v2/src/bus/index.js')
        const { getCoreSessionManager } = await import('./services/core-bridge')
        const sessionMgr = getCoreSessionManager()!
        const pluginResult = await loadInstalledPlugins((name) =>
          createExtensionAPI(name, getMessageBus(), sessionMgr)
        )
        onCleanup(pluginResult.cleanup)
      } catch (err) {
        console.warn('[App] Plugin loading skipped:', err)
      }

      // Initialize deep link handler (ava:// protocol)
      const deepLinkHandle = initDeepLinks()
      onCleanup(() => deepLinkHandle.dispose())

      setSplashStatus('Restoring project session...')
      if (currentProject()) {
        setProjectHubVisible(false)
        await loadSessionsForCurrentProject()
        await restoreForCurrentProject()
        await loadWorkflows(currentProject()?.id)

        // Start workflow scheduler for cron-based workflows
        const scheduled = getScheduledWorkflows()
        if (scheduled.length > 0) {
          const entries: ScheduledWorkflow[] = scheduled.map((w) => ({
            id: w.id,
            cron: w.schedule!,
            lastRun: w.lastRun,
          }))
          const scheduler = startScheduler(entries, (id) => {
            const wf = workflows().find((w) => w.id === id)
            if (wf) {
              markWorkflowRun(id)
              logInfo('Scheduler', `Triggering workflow: ${wf.name}`)
              window.dispatchEvent(
                new CustomEvent('ava:set-input', { detail: { text: wf.prompt } })
              )
            }
          })
          onCleanup(scheduler.stop)
        }
      } else {
        setProjectHubVisible(true)
      }
    } catch (err) {
      logError('App', 'Failed to initialize', err instanceof Error ? err.stack : String(err))
      const errorMsg = err instanceof Error ? `${err.message}\n\nStack: ${err.stack}` : String(err)
      setInitError(errorMsg)
    } finally {
      // Ensure splash shows for at least SPLASH_MIN_MS so it doesn't just flash
      const elapsed = Date.now() - splashStart
      const remaining = SPLASH_MIN_MS - elapsed
      if (remaining > 0) {
        await new Promise((r) => setTimeout(r, remaining))
      }
      setIsInitializing(false)
    }
  })

  const handleOnboardingComplete = (data: OnboardingData) => {
    updateSettings({
      onboardingComplete: true,
      theme: data.theme,
      mode: data.mode,
    })
    if (data.anthropicKey) {
      updateProvider('anthropic', { apiKey: data.anthropicKey, status: 'connected', enabled: true })
    }
    if (data.openrouterKey) {
      updateProvider('openrouter', {
        apiKey: data.openrouterKey,
        status: 'connected',
        enabled: true,
      })
    }
  }

  const handleOnboardingSkip = () => {
    updateSettings({ onboardingComplete: true })
  }

  return (
    <Show
      when={!notTauri()}
      fallback={
        <div class="flex h-screen items-center justify-center bg-[var(--background)]">
          <div class="text-center max-w-md p-6">
            <div class="text-[var(--text-muted)] text-6xl mb-4">&#9670;</div>
            <h1 class="text-xl font-bold text-[var(--text-primary)] mb-2">
              Tauri Runtime Required
            </h1>
            <p class="text-[var(--text-secondary)] mb-4 text-sm leading-relaxed">
              AVA is a desktop app that requires the Tauri runtime. Run{' '}
              <code class="px-1.5 py-0.5 bg-[var(--surface-raised)] border border-[var(--border-default)] rounded text-xs font-mono">
                npm run tauri dev
              </code>{' '}
              and use the native window that opens.
            </p>
          </div>
        </div>
      }
    >
      <SplashScreen visible={isInitializing()} status={splashStatus()} />
      <Show when={!isInitializing()}>
        <Show
          when={!initError()}
          fallback={
            <div class="flex h-screen items-center justify-center bg-[var(--background)]">
              <div class="text-center max-w-md p-6">
                <div class="text-[var(--error)] text-6xl mb-4">!</div>
                <h1 class="text-xl font-bold text-[var(--text-primary)] mb-2">
                  Initialization Error
                </h1>
                <pre class="text-[var(--text-secondary)] mb-4 text-left text-xs whitespace-pre-wrap max-w-lg overflow-auto max-h-64 bg-[var(--surface-raised)] p-3 rounded">
                  {initError()}
                </pre>
                <button
                  type="button"
                  onClick={() => window.location.reload()}
                  class="px-4 py-2 bg-[var(--accent)] text-white rounded-[var(--radius-lg)] hover:bg-[var(--accent-hover)] transition-colors"
                >
                  Retry
                </button>
              </div>
            </div>
          }
        >
          <Show
            when={settings().onboardingComplete}
            fallback={
              <OnboardingScreen
                onComplete={handleOnboardingComplete}
                onSkip={handleOnboardingSkip}
              />
            }
          >
            <Show when={!projectHubVisible()} fallback={<ProjectHub />}>
              <AppShell />
              <QuickModelPicker
                open={quickModelPickerOpen()}
                onClose={() => setQuickModelPickerOpen(false)}
              />
              <SessionSwitcher
                open={sessionSwitcherOpen()}
                onClose={() => setSessionSwitcherOpen(false)}
              />
              <CommandPalette
                commands={createDefaultCommands({
                  newChat: async () => {
                    if (!currentProject()) {
                      setProjectHubVisible(true)
                      return
                    }

                    await createNewSession()
                    setProjectHubVisible(false)
                  },
                  exportChat: () => {
                    const msgs = messages()
                    if (msgs.length === 0) return
                    setExportDialogOpen(true)
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
                    window.dispatchEvent(
                      new CustomEvent('ava:set-input', { detail: { text: prompt } })
                    )
                  },
                  openSettings: toggleSettings,
                  saveWorkflow: () => {
                    if (messages().length === 0) return
                    setWorkflowDialogOpen(true)
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
                    setCheckpointDialogOpen(true)
                  },
                })}
              />
              <WorkflowDialog
                open={workflowDialogOpen()}
                onClose={() => setWorkflowDialogOpen(false)}
              />
              <CheckpointDialog
                open={checkpointDialogOpen()}
                onClose={() => setCheckpointDialogOpen(false)}
                onSave={async (desc) => {
                  const id = await createCheckpoint(desc)
                  if (id) info('Checkpoint saved', desc)
                }}
              />
              <ExportOptionsDialog
                open={exportDialogOpen()}
                onClose={() => setExportDialogOpen(false)}
                onExport={(opts: ExportOptions) => {
                  exportConversation(messages(), currentSession()?.name, opts)
                }}
              />
              <ChangelogDialog
                open={changelogOpen()}
                onClose={() => {
                  setChangelogOpen(false)
                  markChangelogSeen()
                }}
              />
              <UpdateDialog
                open={updateDialogOpen()}
                updateInfo={updateInfo()}
                onClose={() => setUpdateDialogOpen(false)}
                onInstall={downloadAndInstallUpdate}
              />
            </Show>
          </Show>
        </Show>
      </Show>
    </Show>
  )
}

export default App
