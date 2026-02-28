/**
 * Auto-Updater Service
 * Checks for updates via the Tauri updater plugin and installs them.
 * Gracefully degrades when plugins are not installed.
 */

import { isTauri } from '@tauri-apps/api/core'

export interface UpdateInfo {
  available: boolean
  version?: string
  notes?: string
}

/**
 * Check whether a new version is available.
 * Returns `{ available: false }` outside Tauri or on network errors.
 */
export async function checkForUpdate(): Promise<UpdateInfo> {
  if (!isTauri()) {
    return { available: false }
  }

  try {
    // Dynamic import — plugin may not be installed
    // @ts-expect-error Plugin may not be installed
    const updater = await import('@tauri-apps/plugin-updater')
    const update = await updater.check()

    if (!update) {
      return { available: false }
    }

    return {
      available: true,
      version: update.version as string,
      notes: (update.body as string) ?? undefined,
    }
  } catch (err) {
    console.warn('[auto-updater] Failed to check for updates:', err)
    return { available: false }
  }
}

/**
 * Download and install the pending update, then relaunch the app.
 * Throws if no update is available or if the install fails.
 */
export async function downloadAndInstallUpdate(): Promise<void> {
  if (!isTauri()) {
    throw new Error('Updates are only available in the desktop app')
  }

  try {
    // @ts-expect-error Plugin may not be installed
    const updater = await import('@tauri-apps/plugin-updater')
    const update = await updater.check()

    if (!update) {
      throw new Error('No update available')
    }

    let downloaded = 0
    let contentLength = 0

    await update.downloadAndInstall((event: Record<string, unknown>) => {
      const eventType = event.event as string
      const data = event.data as Record<string, unknown>
      switch (eventType) {
        case 'Started':
          contentLength = (data.contentLength as number) ?? 0
          console.log(`[auto-updater] Download started, size: ${contentLength}`)
          break
        case 'Progress':
          downloaded += (data.chunkLength as number) ?? 0
          if (contentLength > 0) {
            const pct = Math.round((downloaded / contentLength) * 100)
            window.dispatchEvent(
              new CustomEvent('ava:update-progress', { detail: { percent: pct } })
            )
          }
          break
        case 'Finished':
          console.log('[auto-updater] Download finished')
          break
      }
    })

    // Attempt to relaunch
    try {
      // @ts-expect-error Plugin may not be installed
      const process = await import('@tauri-apps/plugin-process')
      await process.relaunch()
    } catch {
      console.log('[auto-updater] Relaunch not available, manual restart needed')
    }
  } catch (err) {
    if (err instanceof Error && err.message === 'No update available') {
      throw err
    }
    throw new Error(`Update failed: ${err instanceof Error ? err.message : 'Unknown error'}`)
  }
}
