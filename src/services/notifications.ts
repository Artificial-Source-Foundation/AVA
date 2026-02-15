/**
 * Notification Service
 *
 * Desktop notifications + optional sound on task/chat completion.
 * Uses the Web Notification API (available in Tauri webview).
 * Lazily requests permission on first use.
 */

let permissionRequested = false

export interface CompletionNotificationSettings {
  notifyOnCompletion: boolean
  soundOnCompletion: boolean
  soundVolume: number
}

/** Request notification permission if not already granted */
async function ensurePermission(): Promise<boolean> {
  if (!('Notification' in window)) return false
  if (Notification.permission === 'granted') return true
  if (Notification.permission === 'denied') return false

  if (!permissionRequested) {
    permissionRequested = true
    const result = await Notification.requestPermission()
    return result === 'granted'
  }
  return false
}

/** Play a short chime sound at the configured volume */
function playSound(volume: number) {
  try {
    // Generate a simple beep using AudioContext (no external file needed)
    const ctx = new AudioContext()
    const osc = ctx.createOscillator()
    const gain = ctx.createGain()
    osc.connect(gain)
    gain.connect(ctx.destination)

    osc.frequency.value = 880
    osc.type = 'sine'
    gain.gain.value = (volume / 100) * 0.3 // scale 0-100 to 0-0.3

    osc.start()
    // Quick fade out for a pleasant chime
    gain.gain.exponentialRampToValueAtTime(0.001, ctx.currentTime + 0.3)
    osc.stop(ctx.currentTime + 0.3)
  } catch {
    // AudioContext may not be available in all contexts
  }
}

export async function notifyCompletion(
  title: string,
  body: string,
  settings: CompletionNotificationSettings
): Promise<void> {
  if (settings.notifyOnCompletion) {
    // Only notify if the window is not focused (user is tabbed away)
    if (!document.hasFocus()) {
      const allowed = await ensurePermission()
      if (allowed) {
        new Notification(title, { body, icon: '/icon.png' })
      }
    }
  }

  if (settings.soundOnCompletion) {
    playSound(settings.soundVolume)
  }
}
