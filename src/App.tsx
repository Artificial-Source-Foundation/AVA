/**
 * App Component
 * Root component with initialization logic
 *
 * Note: Preview mode is handled in index.tsx to avoid loading Node.js dependencies
 * Access the design system preview at: http://localhost:1420/?preview=true
 */

import { createEffect, createSignal, on, onCleanup, onMount, Show } from 'solid-js'
import { AppDialogs } from './components/AppDialogs'
import { shouldShowChangelog } from './components/dialogs/ChangelogDialog'
import type { OnboardingData } from './components/dialogs/OnboardingDialog'
import { OnboardingScreen } from './components/dialogs/OnboardingDialog'
import { AppShell } from './components/layout'
import { ProjectHub } from './components/project-hub/ProjectHub'
import { SplashScreen } from './components/SplashScreen'
import { useNotification } from './contexts/notification'
import { runAppInit } from './hooks/useAppInit'
import { registerAppShortcuts } from './hooks/useAppShortcuts'
import { checkForUpdate, downloadAndInstallUpdate, type UpdateInfo } from './services/auto-updater'
import { setDevConsoleLogLevel } from './services/dev-console'
import { setLogLevel } from './services/logger'
import { useLayout } from './stores/layout'
import {
  applyAppearance,
  envKeysDetected,
  setupSystemThemeListener,
  useSettings,
} from './stores/settings'

function App() {
  const [isInitializing, setIsInitializing] = createSignal(true)
  const [initError, setInitError] = createSignal<string | null>(null)
  const [splashStatus, setSplashStatus] = createSignal('')

  const { projectHubVisible, setProjectHubVisible } = useLayout()
  const { settings, updateSettings, updateProvider, updateAppearance } = useSettings()
  const { info } = useNotification()

  const [workflowDialogOpen, setWorkflowDialogOpen] = createSignal(false)
  const [checkpointDialogOpen, setCheckpointDialogOpen] = createSignal(false)
  const [exportDialogOpen, setExportDialogOpen] = createSignal(false)
  const [updateDialogOpen, setUpdateDialogOpen] = createSignal(false)
  const [toolListDialogOpen, setToolListDialogOpen] = createSignal(false)
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

  // Handle "Apply to file" from code blocks — write code content to file via Tauri
  onMount(() => {
    const handleApplyCode = async (e: Event) => {
      const { filePath, content } = (e as CustomEvent<{ filePath: string; content: string }>).detail
      if (!filePath || !content) return
      try {
        const { invoke } = await import('@tauri-apps/api/core')
        await invoke('write_file', { path: filePath, content })
        info('Applied', `Wrote to ${filePath}`)
      } catch {
        // write_file command may not be registered; fall back to a no-op notification
        info('Apply', `Code for ${filePath} — paste manually or use the write tool`)
      }
    }
    window.addEventListener('ava:apply-code', handleApplyCode)
    onCleanup(() => window.removeEventListener('ava:apply-code', handleApplyCode))
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
    const timer = setTimeout(() => void doCheck(), 5_000)
    const handleCheckUpdate = () => void doCheck()
    window.addEventListener('ava:check-update', handleCheckUpdate)
    onCleanup(() => {
      clearTimeout(timer)
      window.removeEventListener('ava:check-update', handleCheckUpdate)
    })
  })

  onMount(async () => {
    applyAppearance()
    const cleanupTheme = setupSystemThemeListener()
    onCleanup(cleanupTheme)

    registerAppShortcuts(setExportDialogOpen, setCheckpointDialogOpen, setProjectHubVisible)

    const result = await runAppInit(setSplashStatus, setProjectHubVisible)
    if (result.error) setInitError(result.error)
    setIsInitializing(false)
  })

  const handleOnboardingComplete = (data: OnboardingData) => {
    updateSettings({ onboardingComplete: true, theme: data.theme, mode: data.mode })

    // Apply appearance choices from the new onboarding flow
    if (data.accentColor || data.darkStyle || data.borderRadius) {
      const patch: Record<string, string> = {}
      if (data.accentColor) patch.accentColor = data.accentColor
      if (data.darkStyle) patch.darkStyle = data.darkStyle
      if (data.borderRadius) patch.borderRadius = data.borderRadius
      updateAppearance(patch)
    }

    // Connect providers with API keys
    if (data.providerKeys) {
      for (const [id, key] of Object.entries(data.providerKeys)) {
        if (key) {
          updateProvider(id, { apiKey: key, status: 'connected', enabled: true })
        }
      }
    }

    // Legacy key support
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

  return (
    <>
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
                onSkip={() => updateSettings({ onboardingComplete: true })}
              />
            }
          >
            <Show when={!projectHubVisible()} fallback={<ProjectHub />}>
              <AppShell />
              <AppDialogs
                workflowDialogOpen={workflowDialogOpen()}
                setWorkflowDialogOpen={setWorkflowDialogOpen}
                checkpointDialogOpen={checkpointDialogOpen()}
                setCheckpointDialogOpen={setCheckpointDialogOpen}
                exportDialogOpen={exportDialogOpen()}
                setExportDialogOpen={setExportDialogOpen}
                changelogOpen={changelogOpen()}
                setChangelogOpen={setChangelogOpen}
                updateDialogOpen={updateDialogOpen()}
                setUpdateDialogOpen={setUpdateDialogOpen}
                toolListDialogOpen={toolListDialogOpen()}
                setToolListDialogOpen={setToolListDialogOpen}
                updateInfo={updateInfo()}
                onInstallUpdate={downloadAndInstallUpdate}
                setProjectHubVisible={setProjectHubVisible}
              />
            </Show>
          </Show>
        </Show>
      </Show>
    </>
  )
}

export default App
