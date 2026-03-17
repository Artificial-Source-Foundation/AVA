/**
 * App Initialization Hook
 *
 * Handles all startup logic: logger, settings, core bridge,
 * database, projects, plugins, deep links, and session restore.
 */

import { isTauri } from '@tauri-apps/api/core'
import { onCleanup } from 'solid-js'
import { validateEnv } from '../config/env'
import { checkApiHealth } from '../lib/api-client'
import { disposeFrontendLogger, initFrontendLogger, log } from '../lib/logger'
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
import { rustBackend } from '../services/rust-bridge'
import { initSettingsFS } from '../services/settings-fs'
import { type ScheduledWorkflow, startScheduler } from '../services/workflow-scheduler'
import { setWebProject, useProject } from '../stores/project'
import { useSession } from '../stores/session'
import {
  detectEnvApiKeys,
  hydrateSettingsFromFS,
  populateModelsFromCatalog,
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
  const { settings, updateSettings } = useSettings()
  const { initializeProjects, currentProject } = useProject()
  const { loadSessionsForCurrentProject, restoreForCurrentProject } = useSession()
  const { loadWorkflows, getScheduledWorkflows, markWorkflowRun, workflows } = useWorkflows()

  // Always capture console output for debugging
  installConsoleCapture()

  // When running outside Tauri (browser mode), use the HTTP API backend.
  if (!isTauri()) {
    return runWebInit(setSplashStatus, setProjectHubVisible)
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
    await initFrontendLogger()
    const logDir = getLogDirectory()
    if (logDir) setLogDirectory(logDir)
    logInfo('App', 'Initializing AVA...')
    log.info('app', 'App initialization started', {
      version: 'AVA v2.0.0',
      platform: navigator.platform,
      runtime: 'tauri',
    })

    validateEnv()

    setSplashStatus('Initializing platform...')
    await initSettingsFS()
    await hydrateSettingsFromFS()
    log.info('app', 'Settings loaded from disk', { logLevel: settings().logLevel })
    setLogLevel(settings().logLevel)
    setDevConsoleLogLevel(settings().logLevel)
    syncAllApiKeys()
    await detectEnvApiKeys()

    // Models.dev is the PRIMARY source for model metadata (pricing, context
    // windows, capabilities). The compiled-in Rust registry is a fallback
    // only used when the catalog fetch fails or returns nothing.
    const catalog = await syncModelsCatalog().catch(() => null)
    const catalogPopulated = catalog ? populateModelsFromCatalog() : 0

    // Fall back to compiled-in Rust backend models only if models.dev
    // returned nothing (network failure with empty cache).
    if (catalogPopulated === 0) {
      try {
        const backendModels = await rustBackend.listModels()
        if (backendModels.length > 0) {
          const currentSettings = settings()
          const updatedProviders = currentSettings.providers.map((p) => {
            const matching = backendModels.filter((m) => m.provider === p.id)
            if (matching.length === 0) return p
            const existingIds = new Set(p.models.map((m) => m.id))
            const newModels = matching
              .filter((m) => !existingIds.has(m.id))
              .map((m) => ({
                id: m.id,
                name: m.name,
                contextWindow: m.contextWindow,
                maxOutput: 0,
                inputCost: m.costInput,
                outputCost: m.costOutput,
                capabilities: [
                  ...(m.toolCall ? ['tool_use' as const] : []),
                  ...(m.vision ? ['vision' as const] : []),
                ],
              }))
            if (newModels.length === 0) return p
            return { ...p, models: [...p.models, ...newModels] }
          })
          updateSettings({ providers: updatedProviders })
        }
      } catch {
        // Non-fatal — fall back to whatever models are already in settings
      }
    }

    // For providers with API keys, also fetch from the provider API
    // (merges live model lists + enriches with catalog data)
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
    log.info('app', 'Database initialized')

    setSplashStatus('Loading projects...')
    await initializeProjects()
    log.info('app', 'Projects loaded')

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

    log.info('app', 'App initialized successfully')
    onCleanup(() => {
      void disposeFrontendLogger()
    })
    return { error: null, notTauri: false }
  } catch (err) {
    log.error('app', 'Failed to initialize', err instanceof Error ? err.stack : String(err))
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

/**
 * Web-mode initialization — runs when the frontend is opened in a regular
 * browser (not inside a Tauri webview). Connects to the HTTP API backend
 * served by `ava serve`.
 */
async function runWebInit(
  setSplashStatus: (status: string) => void,
  setProjectHubVisible: (visible: boolean) => void
): Promise<AppInitResult> {
  const { settings, updateSettings } = useSettings()

  const splashStart = Date.now()

  try {
    setSplashStatus('Starting logger...')
    await initFrontendLogger()
    log.info('app', 'Web mode initialization started')

    // Initialize settings FS with localStorage fallback and hydrate persisted settings
    setSplashStatus('Loading settings...')
    await initSettingsFS()
    await hydrateSettingsFromFS()
    setLogLevel(settings().logLevel)
    setDevConsoleLogLevel(settings().logLevel)

    setSplashStatus('Checking backend...')
    const health = await checkApiHealth()
    if (!health) {
      log.error('app', 'Backend health check failed')
      return {
        error: 'Cannot reach the AVA backend. Make sure `ava serve` is running.',
        notTauri: true,
      }
    }
    log.info('app', 'Backend health check passed')

    setSplashStatus('Loading models...')
    // Models.dev is primary source; compiled-in registry is fallback
    const catalog = await syncModelsCatalog().catch(() => null)
    const catalogPopulated = catalog ? populateModelsFromCatalog() : 0

    if (catalogPopulated === 0) {
      try {
        const backendModels = await rustBackend.listModels()
        if (backendModels.length > 0) {
          const currentSettings = settings()
          const updatedProviders = currentSettings.providers.map((p) => {
            const matching = backendModels.filter((m) => m.provider === p.id)
            if (matching.length === 0 || p.models.length >= matching.length) return p
            const existingIds = new Set(p.models.map((m) => m.id))
            const newModels = matching
              .filter((m) => !existingIds.has(m.id))
              .map((m) => ({
                id: m.id,
                name: m.name,
                contextWindow: m.contextWindow,
                maxOutput: 0,
                inputCost: m.costInput,
                outputCost: m.costOutput,
                capabilities: [
                  ...(m.toolCall ? ['tool_use' as const] : []),
                  ...(m.vision ? ['vision' as const] : []),
                ],
              }))
            if (newModels.length === 0) return p
            return { ...p, models: [...p.models, ...newModels] }
          })
          updateSettings({ providers: updatedProviders })
        }
      } catch {
        // Non-fatal
      }
    }

    setSplashStatus('Loading providers...')
    try {
      const providers = await rustBackend.listProviders()
      if (providers.length > 0) {
        const currentSettings = settings()
        const updatedProviders = currentSettings.providers.map((p) => {
          const backendProvider = providers.find((bp) => bp.name === p.id)
          if (backendProvider) {
            return { ...p, status: 'connected' as const, enabled: true }
          }
          return p
        })
        updateSettings({ providers: updatedProviders })
      }
    } catch {
      // Non-fatal
    }

    // In web mode, create a virtual project from the backend's CWD so the
    // AppShell has directory context without requiring a Tauri file dialog.
    if (health.cwd) {
      setWebProject(health.cwd)
    }

    setSplashStatus('Ready')
    log.info('app', 'Web mode initialized successfully')
    // In web mode, always skip project hub and go straight to the shell
    setProjectHubVisible(false)

    return { error: null, notTauri: true }
  } catch (err) {
    log.error(
      'app',
      'Web mode initialization failed',
      err instanceof Error ? err.stack : String(err)
    )
    const errorMsg = err instanceof Error ? `${err.message}\n\nStack: ${err.stack}` : String(err)
    return { error: errorMsg, notTauri: true }
  } finally {
    const elapsed = Date.now() - splashStart
    const remaining = SPLASH_MIN_MS - elapsed
    if (remaining > 0) {
      await new Promise((r) => setTimeout(r, remaining))
    }
  }
}
