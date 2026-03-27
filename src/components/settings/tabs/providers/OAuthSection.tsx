/**
 * OAuth Section — for provider card expanded view
 *
 * Handles OAuth sign-in button, connected state indicator, and disconnect.
 */

import { Loader2, LogIn, LogOut } from 'lucide-solid'
import { type Component, Show } from 'solid-js'

interface OAuthSectionProps {
  isConnected: boolean
  isLoading: boolean
  error: string | null
  buttonLabel: string
  onConnect: () => void
  onDisconnect: () => void
}

export const OAuthSection: Component<OAuthSectionProps> = (props) => {
  return (
    <div class="pt-3">
      <Show
        when={props.isConnected}
        fallback={
          <button
            type="button"
            onClick={props.onConnect}
            disabled={props.isLoading}
            class="flex items-center gap-2 px-2.5 py-1.5 text-[var(--settings-text-button)] text-[var(--text-secondary)] hover:text-[var(--accent)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors w-full disabled:opacity-50 disabled:cursor-not-allowed"
          >
            <Show when={props.isLoading} fallback={<LogIn class="w-3 h-3" />}>
              <Loader2 class="w-3 h-3 animate-spin" />
            </Show>
            <span>{props.isLoading ? 'Waiting for authorization...' : props.buttonLabel}</span>
          </button>
        }
      >
        <div class="flex items-center gap-2">
          <div class="flex items-center gap-1.5 flex-1 px-2.5 py-1.5 text-[var(--settings-text-button)] text-[var(--success)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)]">
            <span class="w-1.5 h-1.5 rounded-full bg-[var(--success)]" />
            <span>Connected via OAuth</span>
          </div>
          <button
            type="button"
            onClick={() => props.onDisconnect()}
            class="px-2 py-1.5 text-[var(--settings-text-button)] text-[var(--text-muted)] hover:text-[var(--error)] bg-[var(--surface-sunken)] border border-[var(--border-subtle)] rounded-[var(--radius-md)] transition-colors"
            title="Disconnect OAuth"
          >
            <LogOut class="w-3 h-3" />
          </button>
        </div>
      </Show>
      <Show when={props.error}>
        <p class="text-[var(--settings-text-badge)] text-[var(--error)] px-1 mt-1">{props.error}</p>
      </Show>
      <div class="flex items-center gap-2 px-1 mt-2">
        <div class="flex-1 h-px bg-[var(--border-subtle)]" />
        <span class="text-[var(--settings-text-caption)] text-[var(--text-muted)] uppercase">
          or API key
        </span>
        <div class="flex-1 h-px bg-[var(--border-subtle)]" />
      </div>
    </div>
  )
}
