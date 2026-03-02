/**
 * HTTP router — node:http server with ACP-compatible REST endpoints.
 *
 * Routes:
 *   POST   /api/v1/run           — Start agent run
 *   GET    /api/v1/run/:id/stream — SSE event stream
 *   POST   /api/v1/run/:id/steer  — Steer running agent
 *   GET    /api/v1/run/:id/status — Get run status
 *   DELETE /api/v1/run/:id        — Abort run
 *   GET    /api/v1/health         — Health check
 */

import type { IncomingMessage, ServerResponse } from 'node:http'
import { validateRequest } from './auth.js'
import type { SessionRouter } from './session-router.js'
import type { RunRequest, ServerConfig, SteerRequest } from './types.js'

type RouteHandler = (
  req: IncomingMessage,
  res: ServerResponse,
  params: Record<string, string>
) => Promise<void>

interface Route {
  method: string
  pattern: RegExp
  paramNames: string[]
  handler: RouteHandler
}

export class HttpRouter {
  private routes: Route[] = []

  constructor(
    private sessionRouter: SessionRouter,
    private config: ServerConfig
  ) {
    this.registerRoutes()
  }

  private registerRoutes(): void {
    this.route('GET', '/api/v1/health', this.handleHealth.bind(this))
    this.route('POST', '/api/v1/run', this.handleStartRun.bind(this))
    this.route('GET', '/api/v1/run/:id/stream', this.handleStream.bind(this))
    this.route('POST', '/api/v1/run/:id/steer', this.handleSteer.bind(this))
    this.route('GET', '/api/v1/run/:id/status', this.handleStatus.bind(this))
    this.route('DELETE', '/api/v1/run/:id', this.handleAbort.bind(this))
  }

  private route(method: string, path: string, handler: RouteHandler): void {
    const paramNames: string[] = []
    const patternStr = path.replace(/:(\w+)/g, (_match, name) => {
      paramNames.push(name)
      return '([^/]+)'
    })
    this.routes.push({
      method,
      pattern: new RegExp(`^${patternStr}$`),
      paramNames,
      handler,
    })
  }

  /** Main request handler — use as http.createServer callback. */
  async handle(req: IncomingMessage, res: ServerResponse): Promise<void> {
    // CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*')
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, DELETE, OPTIONS')
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization')

    if (req.method === 'OPTIONS') {
      res.writeHead(204)
      res.end()
      return
    }

    // Health check is unauthenticated
    const url = req.url ?? '/'
    const isHealth = url === '/api/v1/health'

    // Authenticate all non-health routes
    if (!isHealth) {
      const valid = await validateRequest(req, this.config.tokenFile)
      if (!valid) {
        this.json(res, 401, { error: 'Unauthorized' })
        return
      }
    }

    // Route matching
    for (const route of this.routes) {
      if (req.method !== route.method) continue
      const match = url.match(route.pattern)
      if (!match) continue

      const params: Record<string, string> = {}
      route.paramNames.forEach((name, i) => {
        params[name] = match[i + 1] ?? ''
      })

      try {
        await route.handler(req, res, params)
      } catch (err) {
        this.json(res, 500, { error: String(err) })
      }
      return
    }

    this.json(res, 404, { error: 'Not found' })
  }

  // ─── Handlers ────────────────────────────────────────────────────────

  private async handleHealth(_req: IncomingMessage, res: ServerResponse): Promise<void> {
    this.json(res, 200, {
      status: 'ok',
      activeRuns: this.sessionRouter.activeCount,
      timestamp: Date.now(),
    })
  }

  private async handleStartRun(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const body = await this.readBody<RunRequest>(req)
    if (!body?.goal) {
      this.json(res, 400, { error: 'Missing required field: goal' })
      return
    }

    const runId = await this.sessionRouter.startRun(body)
    this.json(res, 201, { runId, status: 'started' })
  }

  private async handleStream(
    _req: IncomingMessage,
    res: ServerResponse,
    params: Record<string, string>
  ): Promise<void> {
    const runId = params.id ?? ''
    const status = this.sessionRouter.getStatus(runId)
    if (!status) {
      this.json(res, 404, { error: 'Run not found' })
      return
    }

    // SSE headers
    res.writeHead(200, {
      'Content-Type': 'text/event-stream',
      'Cache-Control': 'no-cache',
      Connection: 'keep-alive',
    })

    const unsub = this.sessionRouter.subscribe(runId, (event) => {
      res.write(`data: ${JSON.stringify(event)}\n\n`)
      if (event.type === 'done') {
        res.end()
      }
    })

    if (!unsub) {
      this.json(res, 404, { error: 'Run not found' })
      return
    }

    // Clean up on client disconnect
    _req.on('close', () => unsub())
  }

  private async handleSteer(
    req: IncomingMessage,
    res: ServerResponse,
    params: Record<string, string>
  ): Promise<void> {
    const body = await this.readBody<SteerRequest>(req)
    if (!body?.message) {
      this.json(res, 400, { error: 'Missing required field: message' })
      return
    }

    const result = this.sessionRouter.steer(params.id ?? '', body.message)
    this.json(res, 200, result)
  }

  private async handleStatus(
    _req: IncomingMessage,
    res: ServerResponse,
    params: Record<string, string>
  ): Promise<void> {
    const status = this.sessionRouter.getStatus(params.id ?? '')
    if (!status) {
      this.json(res, 404, { error: 'Run not found' })
      return
    }
    this.json(res, 200, status)
  }

  private async handleAbort(
    _req: IncomingMessage,
    res: ServerResponse,
    params: Record<string, string>
  ): Promise<void> {
    const aborted = this.sessionRouter.abort(params.id ?? '')
    if (!aborted) {
      this.json(res, 404, { error: 'Run not found or not running' })
      return
    }
    this.json(res, 200, { aborted: true })
  }

  // ─── Helpers ─────────────────────────────────────────────────────────

  private json(res: ServerResponse, status: number, data: unknown): void {
    res.writeHead(status, { 'Content-Type': 'application/json' })
    res.end(JSON.stringify(data))
  }

  private readBody<T>(req: IncomingMessage): Promise<T | null> {
    return new Promise((resolve) => {
      const chunks: Buffer[] = []
      req.on('data', (chunk: Buffer) => chunks.push(chunk))
      req.on('end', () => {
        try {
          const body = Buffer.concat(chunks).toString('utf-8')
          resolve(JSON.parse(body) as T)
        } catch {
          resolve(null)
        }
      })
      req.on('error', () => resolve(null))
    })
  }
}
