/**
 * MCP OAuth Dialog
 *
 * OAuth consent flow for MCP servers:
 * Shows server name, scopes, authorize/cancel, opens browser for redirect.
 */

import { ExternalLink, Key, Shield, X } from 'lucide-solid'
import { type Component, createSignal, For, Show } from 'solid-js'
import { startOAuthFlow } from '../../services/mcp-oauth'

interface MCPOAuthDialogProps {
  open: boolean
  serverName: string
  authUrl: string
  clientId: string
  scopes: string[]
  redirectUri: string
  onClose: () => void
  onAuthorized: () => void
}

export const MCPOAuthDialog: Component<MCPOAuthDialogProps> = (props) => {
  const [status, setStatus] = createSignal<'consent' | 'waiting' | 'error'>('consent')
  const [error, setError] = createSignal<string | null>(null)

  const handleAuthorize = async () => {
    try {
      setStatus('waiting')
      const { authorizationUrl } = await startOAuthFlow(
        props.serverName,
        props.authUrl,
        props.clientId,
        props.scopes,
        props.redirectUri
      )

      // Open browser for authorization
      try {
        const { open } = await import('@tauri-apps/plugin-shell')
        await open(authorizationUrl)
      } catch {
        window.open(authorizationUrl, '_blank')
      }
    } catch (err) {
      setStatus('error')
      setError(err instanceof Error ? err.message : 'Authorization failed')
    }
  }

  const handleClose = () => {
    setStatus('consent')
    setError(null)
    props.onClose()
  }

  return (
    <Show when={props.open}>
      <div class="fixed inset-0 z-50 flex items-center justify-center bg-black/60">
        <div class="bg-[var(--surface-overlay)] border border-[var(--border-default)] rounded-[var(--radius-xl)] p-6 max-w-md w-full shadow-2xl space-y-4">
          {/* Header */}
          <div class="flex items-center justify-between">
            <div class="flex items-center gap-2">
              <Key class="w-4 h-4 text-[var(--accent)]" />
              <h3 class="text-sm font-semibold text-[var(--text-primary)]">Authorize MCP Server</h3>
            </div>
            <button
              type="button"
              onClick={handleClose}
              class="p-1 text-[var(--text-muted)] hover:text-[var(--text-primary)] transition-colors"
            >
              <X class="w-4 h-4" />
            </button>
          </div>

          <Show when={status() === 'consent'}>
            <div class="space-y-3">
              <p class="text-xs text-[var(--text-secondary)]">
                <strong>{props.serverName}</strong> requires authorization to access your account.
              </p>

              <Show when={props.scopes.length > 0}>
                <div class="space-y-1.5">
                  <div class="flex items-center gap-1.5 text-[11px] text-[var(--text-secondary)]">
                    <Shield class="w-3.5 h-3.5 text-[var(--text-muted)]" />
                    <span>Requested Permissions:</span>
                  </div>
                  <div class="flex flex-wrap gap-1.5">
                    <For each={props.scopes}>
                      {(scope) => (
                        <span class="px-2 py-0.5 text-[10px] rounded-full bg-[var(--surface-raised)] border border-[var(--border-subtle)] text-[var(--text-secondary)]">
                          {scope}
                        </span>
                      )}
                    </For>
                  </div>
                </div>
              </Show>

              <p class="text-[10px] text-[var(--text-muted)]">
                You will be redirected to authorize in your browser. You can revoke access at any
                time.
              </p>

              <div class="flex gap-2 justify-end pt-2">
                <button
                  type="button"
                  onClick={handleClose}
                  class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                >
                  Cancel
                </button>
                <button
                  type="button"
                  onClick={() => void handleAuthorize()}
                  class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors flex items-center gap-1.5"
                >
                  <ExternalLink class="w-3 h-3" />
                  Authorize
                </button>
              </div>
            </div>
          </Show>

          <Show when={status() === 'waiting'}>
            <div class="text-center py-4 space-y-3">
              <div class="w-8 h-8 mx-auto border-2 border-[var(--accent)] border-t-transparent rounded-full animate-spin" />
              <p class="text-sm text-[var(--text-primary)]">Waiting for authorization...</p>
              <p class="text-xs text-[var(--text-muted)]">
                Complete the authorization in your browser, then return here.
              </p>
              <button
                type="button"
                onClick={handleClose}
                class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
              >
                Cancel
              </button>
            </div>
          </Show>

          <Show when={status() === 'error'}>
            <div class="text-center py-4 space-y-3">
              <p class="text-sm text-[var(--error)]">Authorization Failed</p>
              <p class="text-xs text-[var(--text-muted)]">{error()}</p>
              <div class="flex gap-2 justify-center">
                <button
                  type="button"
                  onClick={handleClose}
                  class="px-3 py-1.5 text-xs text-[var(--text-muted)] hover:text-[var(--text-secondary)] transition-colors"
                >
                  Close
                </button>
                <button
                  type="button"
                  onClick={() => {
                    setStatus('consent')
                    setError(null)
                  }}
                  class="px-3 py-1.5 text-xs font-medium bg-[var(--accent)] text-white rounded-[var(--radius-md)] hover:brightness-110 transition-colors"
                >
                  Retry
                </button>
              </div>
            </div>
          </Show>
        </div>
      </div>
    </Show>
  )
}
