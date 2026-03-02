/**
 * Sharing extension — registers a /share command for session sharing.
 *
 * Posts session data to a configured endpoint URL. Returns a stub
 * message when no endpoint is configured.
 */

import type { Disposable, ExtensionAPI, SlashCommand } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI): Disposable {
  const shareCommand: SlashCommand = {
    name: 'share',
    description: 'Share the current session via a configured endpoint',

    async execute(_args: string, ctx) {
      let endpoint: string | undefined
      try {
        const settings = api.getSettings<{ endpoint?: string }>('sharing')
        endpoint = settings.endpoint
      } catch {
        // Settings category not registered — no endpoint configured
      }

      if (!endpoint) {
        return 'No sharing endpoint configured. Set sharing.endpoint in settings to enable session sharing.'
      }

      try {
        const response = await fetch(endpoint, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ sessionId: ctx.sessionId, timestamp: Date.now() }),
        })

        if (!response.ok) {
          return `Sharing failed: server returned ${String(response.status)}`
        }

        const data = (await response.json()) as { url?: string }
        const shareUrl = data.url ?? endpoint

        api.emit('session:shared', { sessionId: ctx.sessionId, url: shareUrl })
        return `Session shared: ${shareUrl}`
      } catch (err) {
        const message = err instanceof Error ? err.message : 'Unknown error'
        return `Sharing failed: ${message}`
      }
    },
  }

  const cmdDisposable = api.registerCommand(shareCommand)

  api.log.debug('Sharing extension activated')

  return {
    dispose() {
      cmdDisposable.dispose()
    },
  }
}
