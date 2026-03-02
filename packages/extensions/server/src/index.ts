/**
 * Server extension — HTTP server for remote agent control.
 *
 * Exposes ACP-compatible REST API for starting, streaming, steering,
 * and aborting agent runs. Token-based authentication.
 *
 * Start via: `ava serve` or programmatically via server:start event.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { HttpRouter } from './router.js'
import { SessionRouter } from './session-router.js'
import { DEFAULT_SERVER_CONFIG, type ServerConfig } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  let server: ReturnType<typeof import('node:http')['createServer']> | null = null
  const config: ServerConfig = { ...DEFAULT_SERVER_CONFIG }
  const sessionRouter = new SessionRouter(api)
  const httpRouter = new HttpRouter(sessionRouter, config)

  // Allow other extensions to register custom routes
  const routeHandlers = new Map<string, (req: unknown, res: unknown) => Promise<void>>()

  const routeRegistration = api.on('server:register-route', (data: unknown) => {
    const { path, handler } = data as {
      path: string
      handler: (req: unknown, res: unknown) => Promise<void>
    }
    routeHandlers.set(path, handler)
    api.log.debug(`Server route registered: ${path}`)
  })

  // Start server on event
  const startDisposable = api.on('server:start', async (data: unknown) => {
    const opts = (data ?? {}) as Partial<ServerConfig>
    if (opts.port) config.port = opts.port
    if (opts.host) config.host = opts.host

    if (server) {
      api.log.warn('Server already running')
      return
    }

    const http = await import('node:http')
    server = http.createServer(async (req, res) => {
      // Check custom routes first
      const url = req.url ?? '/'
      for (const [path, handler] of routeHandlers) {
        if (url.startsWith(path)) {
          await handler(req, res)
          return
        }
      }
      await httpRouter.handle(req, res)
    })

    server.listen(config.port, config.host, () => {
      api.log.info(`Server listening on ${config.host}:${config.port}`)
      api.emit('server:started', { port: config.port, host: config.host })
    })
  })

  // Stop server on event
  const stopDisposable = api.on('server:stop', () => {
    if (server) {
      server.close()
      server = null
      api.log.info('Server stopped')
      api.emit('server:stopped', {})
    }
  })

  // Periodic cleanup of old runs
  const cleanupInterval = setInterval(() => {
    sessionRouter.cleanup()
  }, 300_000) // every 5 minutes

  api.log.debug('Server extension activated')

  return {
    dispose() {
      clearInterval(cleanupInterval)
      if (server) {
        server.close()
        server = null
      }
      sessionRouter.dispose()
      startDisposable.dispose()
      stopDisposable.dispose()
      routeRegistration.dispose()
      routeHandlers.clear()
    },
  }
}
