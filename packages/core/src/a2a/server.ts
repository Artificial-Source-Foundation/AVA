/**
 * A2A HTTP Server
 *
 * Exposes Estela as an A2A-compatible agent over HTTP.
 * Uses Node's native http module (no Express dependency).
 *
 * Endpoints:
 * - GET  /.well-known/agent.json     → Agent card discovery
 * - POST /messages                    → Send message (create/continue task)
 * - POST /messages/stream             → Send message with SSE streaming
 * - GET  /tasks/:id                   → Get task status
 * - GET  /tasks                       → List all tasks
 * - POST /tasks/:id/cancel            → Cancel a running task
 */

import { createServer, type IncomingMessage, type Server, type ServerResponse } from 'node:http'
import { createAgentCard } from './agent-card.js'
import { checkAuth } from './auth.js'
import { SSE_HEADERS, SSEWriter, startKeepalive } from './streaming.js'
import { type TaskExecutor, TaskManager } from './task.js'
import type {
  A2AServerConfig,
  CancelTaskResponse,
  GetTaskResponse,
  SendMessageRequest,
  SendMessageResponse,
} from './types.js'
import { DEFAULT_A2A_PORT } from './types.js'

// ============================================================================
// Server
// ============================================================================

/**
 * A2A HTTP Server.
 *
 * Usage:
 * ```ts
 * const server = new A2AServer({ port: 41242 })
 * server.setExecutor(myExecutor)
 * await server.start()
 * // ...
 * await server.stop()
 * ```
 */
export class A2AServer {
  private server: Server | null = null
  private config: A2AServerConfig
  private taskManager: TaskManager

  constructor(config: A2AServerConfig = {}) {
    this.config = config
    this.taskManager = new TaskManager(undefined, config.workingDirectory)
  }

  // ==========================================================================
  // Lifecycle
  // ==========================================================================

  /**
   * Start the HTTP server.
   */
  async start(): Promise<void> {
    if (this.server) {
      throw new Error('Server is already running')
    }

    const port = this.config.port ?? DEFAULT_A2A_PORT
    const host = this.config.host ?? 'localhost'

    this.server = createServer((req, res) => {
      this.handleRequest(req, res).catch((error: Error) => {
        console.error('[A2A] Unhandled error:', error.message)
        if (!res.headersSent) {
          sendJson(res, 500, { error: 'Internal server error' })
        }
      })
    })

    return new Promise<void>((resolve, reject) => {
      this.server!.once('error', reject)
      this.server!.listen(port, host, () => {
        this.server!.removeListener('error', reject)
        resolve()
      })
    })
  }

  /**
   * Stop the HTTP server.
   */
  async stop(): Promise<void> {
    if (!this.server) return

    this.taskManager.reset()

    return new Promise<void>((resolve, reject) => {
      this.server!.close((err) => {
        this.server = null
        if (err) reject(err)
        else resolve()
      })
    })
  }

  /**
   * Get the server's actual listening address.
   */
  getAddress(): { host: string; port: number } | null {
    if (!this.server) return null
    const addr = this.server.address()
    if (!addr || typeof addr === 'string') return null
    return { host: addr.address, port: addr.port }
  }

  /**
   * Set the task executor for handling messages.
   */
  setExecutor(executor: TaskExecutor): void {
    this.taskManager.setExecutor(executor)
  }

  /**
   * Get the task manager (for testing or direct access).
   */
  getTaskManager(): TaskManager {
    return this.taskManager
  }

  // ==========================================================================
  // Request Routing
  // ==========================================================================

  private async handleRequest(req: IncomingMessage, res: ServerResponse): Promise<void> {
    // CORS headers
    res.setHeader('Access-Control-Allow-Origin', '*')
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS')
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type, Authorization')

    // Preflight
    if (req.method === 'OPTIONS') {
      res.writeHead(204)
      res.end()
      return
    }

    const url = parseUrl(req.url ?? '/')
    const method = req.method ?? 'GET'

    // Agent card (no auth required)
    if (method === 'GET' && url.path === '/.well-known/agent.json') {
      return this.handleAgentCard(res)
    }

    // Auth check for all other endpoints
    const auth = checkAuth(req, this.config.authToken)
    if (!auth.authenticated) {
      return sendJson(res, 401, { error: auth.error })
    }

    // Route matching
    if (method === 'POST' && url.path === '/messages') {
      return this.handleSendMessage(req, res)
    }

    if (method === 'POST' && url.path === '/messages/stream') {
      return this.handleStreamMessage(req, res)
    }

    if (method === 'GET' && url.path === '/tasks') {
      return this.handleListTasks(res)
    }

    const taskIdMatch = url.path.match(/^\/tasks\/([^/]+)$/)
    if (taskIdMatch) {
      const taskId = taskIdMatch[1]!
      if (method === 'GET') {
        return this.handleGetTask(res, taskId)
      }
    }

    const cancelMatch = url.path.match(/^\/tasks\/([^/]+)\/cancel$/)
    if (cancelMatch && method === 'POST') {
      return this.handleCancelTask(res, cancelMatch[1]!)
    }

    // 404
    sendJson(res, 404, { error: 'Not found' })
  }

  // ==========================================================================
  // Handlers
  // ==========================================================================

  private handleAgentCard(res: ServerResponse): void {
    const card = createAgentCard(this.config)
    sendJson(res, 200, card)
  }

  private async handleSendMessage(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const body = await readBody<SendMessageRequest>(req)
    if (!body?.message) {
      return sendJson(res, 400, { error: 'Missing message in request body' })
    }

    const task = this.taskManager.getOrCreateTask(body.message, body.taskId, body.contextId)

    // Execute synchronously (non-streaming)
    try {
      const events = this.taskManager.executeTask(task.id)
      // Drain all events (non-streaming just waits for completion)
      for await (const _event of events) {
        // Consume events silently
      }
    } catch {
      // Task execution errors are captured in task state
    }

    const response: SendMessageResponse = {
      task: this.taskManager.getTask(task.id) ?? task,
    }
    sendJson(res, 200, response)
  }

  private async handleStreamMessage(req: IncomingMessage, res: ServerResponse): Promise<void> {
    const body = await readBody<SendMessageRequest>(req)
    if (!body?.message) {
      return sendJson(res, 400, { error: 'Missing message in request body' })
    }

    const task = this.taskManager.getOrCreateTask(body.message, body.taskId, body.contextId)

    // Set SSE headers
    res.writeHead(200, SSE_HEADERS)

    const writer = new SSEWriter(res)
    const stopKeepalive = startKeepalive(writer)

    // Handle client disconnect
    req.on('close', () => {
      stopKeepalive()
      writer.close()
    })

    try {
      const events = this.taskManager.executeTask(task.id)
      for await (const event of events) {
        if (!writer.isOpen()) break
        writer.sendEvent(event)
      }
    } catch (error) {
      if (writer.isOpen()) {
        writer.sendRaw(
          'error',
          JSON.stringify({
            error: error instanceof Error ? error.message : 'Unknown error',
          })
        )
      }
    } finally {
      stopKeepalive()
      writer.close()
    }
  }

  private handleListTasks(res: ServerResponse): void {
    const tasks = this.taskManager.getAllTasks()
    sendJson(res, 200, { tasks })
  }

  private handleGetTask(res: ServerResponse, taskId: string): void {
    const task = this.taskManager.getTask(taskId)
    if (!task) {
      sendJson(res, 404, { error: `Task not found: ${taskId}` })
      return
    }

    const response: GetTaskResponse = { task }
    sendJson(res, 200, response)
  }

  private handleCancelTask(res: ServerResponse, taskId: string): void {
    const task = this.taskManager.cancelTask(taskId)
    if (!task) {
      sendJson(res, 404, { error: `Task not found: ${taskId}` })
      return
    }

    const response: CancelTaskResponse = { task }
    sendJson(res, 200, response)
  }
}

// ============================================================================
// HTTP Utilities
// ============================================================================

/**
 * Send a JSON response.
 */
function sendJson(res: ServerResponse, status: number, body: unknown): void {
  const json = JSON.stringify(body)
  res.writeHead(status, {
    'Content-Type': 'application/json',
    'Content-Length': Buffer.byteLength(json),
  })
  res.end(json)
}

/**
 * Read and parse the request body as JSON.
 */
function readBody<T>(req: IncomingMessage): Promise<T | null> {
  return new Promise((resolve) => {
    const chunks: Buffer[] = []

    req.on('data', (chunk: Buffer) => {
      chunks.push(chunk)
    })

    req.on('end', () => {
      if (chunks.length === 0) {
        resolve(null)
        return
      }

      try {
        const body = Buffer.concat(chunks).toString('utf-8')
        resolve(JSON.parse(body) as T)
      } catch {
        resolve(null)
      }
    })

    req.on('error', () => {
      resolve(null)
    })
  })
}

/**
 * Parse a URL into path and query components.
 */
function parseUrl(raw: string): { path: string; query: URLSearchParams } {
  const qIndex = raw.indexOf('?')
  if (qIndex === -1) {
    return { path: raw, query: new URLSearchParams() }
  }
  return {
    path: raw.slice(0, qIndex),
    query: new URLSearchParams(raw.slice(qIndex + 1)),
  }
}

// ============================================================================
// Singleton
// ============================================================================

let defaultServer: A2AServer | null = null

/**
 * Get the default A2A server instance.
 */
export function getA2AServer(config?: A2AServerConfig): A2AServer {
  if (!defaultServer) {
    defaultServer = new A2AServer(config)
  }
  return defaultServer
}

/**
 * Set the default A2A server (for testing).
 */
export function setA2AServer(server: A2AServer): void {
  defaultServer = server
}

/**
 * Reset the default A2A server.
 */
export function resetA2AServer(): void {
  defaultServer = null
}
