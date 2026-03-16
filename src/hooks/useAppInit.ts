/**
 * App Initialization Hook
 *
 * Handles all startup logic: logger, settings, core bridge,
 * database, projects, plugins, deep links, and session restore.
 */

import { isTauri } from '@tauri-apps/api/core'
import { onCleanup } from 'solid-js'
import { validateEnv } from '../config/env'
import { initCoreBridge } from '../services/core-bridge'
import { initDatabase } from '../services/database'
import { initDeepLinks } from '../services/deep-link'
import {
  installConsoleCapture,
  setDevConsoleLogLevel,
  setLogDirectory,
} from '../services/dev-console'
import { getLogDirectory, initLogger, logError, logInfo, setLogLevel } from '../services/logger'
import { syncModelsCatalog } from '../services/providers/models-dev-catalog'
import { initSettingsFS } from '../services/settings-fs'
import { type ScheduledWorkflow, startScheduler } from '../services/workflow-scheduler'
import { useProject } from '../stores/project'
import { useSession } from '../stores/session'
import {
  detectEnvApiKeys,
  hydrateSettingsFromFS,
  pushSettingsToCore,
  refreshAllProviderModels,
  syncAllApiKeys,
  syncCredentialsToDisk,
  useSettings,
} from '../stores/settings'
import { useWorkflows } from '../stores/workflows'

const SPLASH_MIN_MS = 800
const BUILT_IN_EXTENSION_COUNT = 20
const BUILT_IN_TOOL_COUNT = 39

export interface AppInitResult {
  error: string | null
  notTauri: boolean
}

export async function runAppInit(
  setSplashStatus: (status: string) => void,
  setProjectHubVisible: (visible: boolean) => void
): Promise<AppInitResult> {
  const { settings } = useSettings()
  const { initializeProjects, currentProject } = useProject()
  const { loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()
  const { loadWorkflows, getScheduledWorkflows, markWorkflowRun, workflows } = useWorkflows()

  // Always capture console output for debugging
  installConsoleCapture()

  // Guard: AVA requires the Tauri runtime
  if (!isTauri()) {
    return { error: null, notTauri: true }
  }

  // Show window early so the splash screen is visible during initialization.
  try {
    const { getCurrentWindow } = await import('@tauri-apps/api/window')
    await getCurrentWindow().show()
  } catch {
    /* ignore in non-Tauri */
  }

  const splashStart = Date.now()

  try {
    // Clear corrupted resizable panel sizes from localStorage
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
    syncModelsCatalog().catch(() => {})
    refreshAllProviderModels()

    setSplashStatus('Initializing core engine...')
    const cleanupCore = await initCoreBridge({
      contextLimit: 200_000,
    })
    onCleanup(cleanupCore)
    pushSettingsToCore()
    syncCredentialsToDisk()

    setSplashStatus('Loading database...')
    await initDatabase()

    setSplashStatus('Loading projects...')
    await initializeProjects()

    setSplashStatus('Loading plugins...')
    // Plugin loading is now handled by the Rust backend.
    // The packages/core-v2 layer has been removed.

    const deepLinkHandle = initDeepLinks()
    onCleanup(() => deepLinkHandle.dispose())

    setSplashStatus('Restoring project session...')
    if (currentProject()) {
      setProjectHubVisible(false)
      await loadSessionsForCurrentProject()
      await restoreForCurrentProject()
      await loadWorkflows(currentProject()?.id)

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
            window.dispatchEvent(new CustomEvent('ava:set-input', { detail: { text: wf.prompt } }))
          }
        })
        onCleanup(scheduler.stop)
      }
    } else {
      setProjectHubVisible(true)
    }

    return { error: null, notTauri: false }
  } catch (err) {
    logError('App', 'Failed to initialize', err instanceof Error ? err.stack : String(err))
    const errorMsg = err instanceof Error ? `${err.message}\n\nStack: ${err.stack}` : String(err)
    return { error: errorMsg, notTauri: false }
  } finally {
    const elapsed = Date.now() - splashStart
    const remaining = SPLASH_MIN_MS - elapsed
    if (remaining > 0) {
      await new Promise((r) => setTimeout(r, remaining))
    }
  }
}
