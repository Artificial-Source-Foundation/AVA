/**
 * Update Dialog
 * Shows update info (version, release notes) with install and dismiss buttons.
 */

import { ArrowDownCircle, Loader2, RefreshCw } from 'lucide-solid'
import { type Component, createSignal, Show } from 'solid-js'
import type { UpdateInfo } from '../../services/auto-updater'

interface UpdateDialogProps {
  open: boolean
  updateInfo: UpdateInfo | null
  onClose: () => void
  onInstall: () => Promise<void>
}

export const UpdateDialog: Component<UpdateDialogProps> = (props) => {
  const [installing, setInstalling] = createSignal(false)
  const [progress, setProgress] = createSignal(0)
  const [error, setError] = createSignal<string | null>(null)

  const handleInstall = async () => {
    setInstalling(true)
    setError(null)

    // Listen for progress events
    const handleProgress = (e: Event) => {
      const { percent } = (e as CustomEvent).detail
      setProgress(percent)
    }
    window.addEventListener('ava:update-progress', handleProgress)

    try {
      await props.onInstall()
    } catch (err) {
      setError(err instanceof Error ? err.message : 'Update failed')
      setInstalling(false)
    } finally {
      window.removeEventListener('ava:update-progress', handleProgress)
    }
  }

  return (
    <Show when={props.open && props.updateInfo?.available}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
          {/* Header */}
          <div class="flex items-center gap-2">
            <ArrowDownCircle class="w-5 h-5 text-[var(--accent)]" />
            <h3 class="text-sm font-semibold text-[var(--text-primary)]">Update Available</h3>
          </div>

          {/* Version */}
          <div class="flex items-center gap-2 px-3 py-2 rounded-[var(--radius-md)] bg-[var(--accent-subtle)] border border-[var(--accent-border)]">
            <span class="text-xs text-[var(--text-secondary)]">New version:</span>
            <span class="text-xs font-semibold text-[var(--accent)]">
              {props.updateInfo?.version}
            </span>
          </div>

          {/* Release Notes */}
          <Show when={props.updateInfo?.notes}>
            <div class="space-y-1">
              <span class="text-[10px] font-medium text-[var(--text-muted)] uppercase tracking-wide">
                Release Notes
              </span>
              <div class="max-h-40 overflow-y-auto px-3 py-2 rounded-[var(--radius-md)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] text-xs text-[var(--text-secondary)] leading-relaxed whitespace-pre-wrap">
                {props.updateInfo?.notes}
              </div>
            </div>
          </Show>

          {/* Progress */}
          <Show when={installing()}>
            <div class="space-y-1">
              <div class="flex items-center gap-2 text-xs text-[var(--text-secondary)]">
                <Loader2 class="w-3.5 h-3.5 animate-spin" />
                <span>Downloading update... {progress()}%</span>
              </div>
              <div class="w-full h-1.5 rounded-full bg-[var(--surface-sunken)] overflow-hidden">
                <div
                  class="h-full bg-[var(--accent)] rounded-full transition-all duration-300"
                  style={{ width: `${progress()}%` }}
                />
              </div>
            </div>
          </Show>

          {/* Error */}
          <Show when={error()}>
            <p class="text-[10px] text-[var(--error)] px-1">{error()}</p>
          </Show>

          {/* Actions */}
          <div class="flex gap-2 justify-end pt-1">
            <button
              type="button"
              onClick={() => props.onClose()}
              disabled={installing()}
              class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors disabled:opacity-50"
            >
              Later
            </button>
            <button
              type="button"
              onClick={handleInstall}
              disabled={installing()}
              class="flex items-center gap-1.5 px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors disabled:opacity-60"
            >
              <Show when={installing()} fallback={<RefreshCw class="w-3 h-3" />}>
                <Loader2 class="w-3 h-3 animate-spin" />
              </Show>
              {installing() ? 'Installing...' : 'Install Update'}
            </button>
          </div>
        </div>
      </div>
    </Show>
  )
}
