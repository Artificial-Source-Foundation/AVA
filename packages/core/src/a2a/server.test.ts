/**
 * A2A Server Tests
 *
 * Integration tests for the HTTP server using real HTTP requests.
 */

import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { A2AServer, resetA2AServer } from './server.js'
import type { TaskEventListener, TaskExecutor } from './task.js'
import type { A2AEvent } from './types.js'
import { A2A_PROTOCOL_VERSION, DEFAULT_AGENT_VERSION } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

/** Create a mock executor that completes immediately */
function createMockExecutor(delay = 0): TaskExecutor {
  return {
    execute: vi.fn(
      async (_goal: string, _cwd: string, _signal: AbortSignal, onEvent: TaskEventListener) => {
        if (delay > 0) {
          await new Promise((r) => setTimeout(r, delay))
        }
        // Emit a text content event
        const event: A2AEvent = {
          kind: 'status-update',
          taskId: 'internal',
          contextId: 'internal',
          final: false,
          status: {
            state: 'working',
            message: {
              role: 'agent',
              parts: [{ type: 'text', text: 'Processing your request...' }],
            },
            timestamp: new Date().toISOString(),
          },
        }
        onEvent(event)
      }
    ),
  }
}

async function fetchJson(
  url: string,
  options?: RequestInit
): Promise<{ status: number; body: Record<string, unknown> }> {
  const response = await fetch(url, options)
  const body = (await response.json()) as Record<string, unknown>
  return { status: response.status, body }
}

async function fetchSSE(
  url: string,
  options?: RequestInit
): Promise<{ status: number; events: string[] }> {
  const response = await fetch(url, options)
  const text = await response.text()
  const events = text.split('\n\n').filter(Boolean)
  return { status: response.status, events }
}

// ============================================================================
// Tests
// ============================================================================

describe('A2AServer', () => {
  let server: A2AServer
  let baseUrl: string

  beforeEach(async () => {
    resetA2AServer()
    server = new A2AServer({ port: 0, host: '127.0.0.1' }) // Port 0 = random
    server.setExecutor(createMockExecutor())
    await server.start()

    const addr = server.getAddress()!
    baseUrl = `http://${addr.host}:${addr.port}`
  })

  afterEach(async () => {
    await server.stop()
  })

  describe('agent card', () => {
    it('should serve agent card at well-known URL', async () => {
      const { status, body } = await fetchJson(`${baseUrl}/.well-known/agent.json`)

      expect(status).toBe(200)
      expect(body.name).toBe('AVA')
      expect(body.protocolVersion).toBe(A2A_PROTOCOL_VERSION)
      expect(body.version).toBe(DEFAULT_AGENT_VERSION)
    })

    it('should not require auth for agent card', async () => {
      await server.stop()
      server = new A2AServer({ port: 0, host: '127.0.0.1', authToken: 'secret' })
      server.setExecutor(createMockExecutor())
      await server.start()

      const addr = server.getAddress()!
      const url = `http://${addr.host}:${addr.port}`

      const { status, body } = await fetchJson(`${url}/.well-known/agent.json`)
      expect(status).toBe(200)
      expect(body.name).toBe('AVA')
    })

    it('should include capabilities', async () => {
      const { body } = await fetchJson(`${baseUrl}/.well-known/agent.json`)
      const caps = body.capabilities as Record<string, unknown>

      expect(caps.streaming).toBe(true)
      expect(caps.pushNotifications).toBe(false)
      expect(caps.stateTransitionHistory).toBe(true)
    })
  })

  describe('POST /messages', () => {
    it('should create and execute a task', async () => {
      const { status, body } = await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: {
            role: 'user',
            parts: [{ type: 'text', text: 'Hello, AVA' }],
          },
        }),
      })

      expect(status).toBe(200)
      const task = body.task as Record<string, unknown>
      expect(task.id).toBeTruthy()
      expect(task.status).toBeDefined()
    })

    it('should reject missing message', async () => {
      const { status, body } = await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({}),
      })

      expect(status).toBe(400)
      expect(body.error).toContain('Missing message')
    })

    it('should continue existing task when taskId provided', async () => {
      // Create first task
      const { body: first } = await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: { role: 'user', parts: [{ type: 'text', text: 'First' }] },
        }),
      })

      const taskId = (first.task as Record<string, unknown>).id as string

      // Continue with same taskId
      const { body: second } = await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          taskId,
          message: { role: 'user', parts: [{ type: 'text', text: 'Second' }] },
        }),
      })

      const secondTask = second.task as Record<string, unknown>
      expect(secondTask.id).toBe(taskId)
    })
  })

  describe('POST /messages/stream', () => {
    it('should stream SSE events', async () => {
      const { status, events } = await fetchSSE(`${baseUrl}/messages/stream`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: { role: 'user', parts: [{ type: 'text', text: 'Stream test' }] },
        }),
      })

      expect(status).toBe(200)
      expect(events.length).toBeGreaterThan(0)

      // Should have at least a status-update event
      const hasStatusUpdate = events.some((e) => e.includes('status-update'))
      expect(hasStatusUpdate).toBe(true)
    })

    it('should reject missing message', async () => {
      const response = await fetch(`${baseUrl}/messages/stream`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({}),
      })

      expect(response.status).toBe(400)
    })
  })

  describe('GET /tasks', () => {
    it('should list all tasks', async () => {
      // Create two tasks
      await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: { role: 'user', parts: [{ type: 'text', text: 'Task 1' }] },
        }),
      })

      await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: { role: 'user', parts: [{ type: 'text', text: 'Task 2' }] },
        }),
      })

      const { status, body } = await fetchJson(`${baseUrl}/tasks`)
      expect(status).toBe(200)

      const tasks = body.tasks as unknown[]
      expect(tasks.length).toBe(2)
    })

    it('should return empty array when no tasks', async () => {
      const { body } = await fetchJson(`${baseUrl}/tasks`)
      expect((body.tasks as unknown[]).length).toBe(0)
    })
  })

  describe('GET /tasks/:id', () => {
    it('should get a specific task', async () => {
      // Create a task
      const { body: created } = await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: { role: 'user', parts: [{ type: 'text', text: 'Get me' }] },
        }),
      })

      const taskId = (created.task as Record<string, unknown>).id as string

      const { status, body } = await fetchJson(`${baseUrl}/tasks/${taskId}`)
      expect(status).toBe(200)
      expect((body.task as Record<string, unknown>).id).toBe(taskId)
    })

    it('should return 404 for unknown task', async () => {
      const { status } = await fetchJson(`${baseUrl}/tasks/nonexistent`)
      expect(status).toBe(404)
    })
  })

  describe('POST /tasks/:id/cancel', () => {
    it('should cancel a task', async () => {
      // Create a task (completes immediately with mock executor)
      const { body: created } = await fetchJson(`${baseUrl}/messages`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          message: { role: 'user', parts: [{ type: 'text', text: 'Cancel me' }] },
        }),
      })

      const taskId = (created.task as Record<string, unknown>).id as string

      const { status, body } = await fetchJson(`${baseUrl}/tasks/${taskId}/cancel`, {
        method: 'POST',
      })

      expect(status).toBe(200)
      expect(body.task).toBeDefined()
    })

    it('should return 404 for unknown task', async () => {
      const { status } = await fetchJson(`${baseUrl}/tasks/nonexistent/cancel`, {
        method: 'POST',
      })
      expect(status).toBe(404)
    })
  })

  describe('authentication', () => {
    let authServer: A2AServer
    let authUrl: string

    beforeEach(async () => {
      authServer = new A2AServer({
        port: 0,
        host: '127.0.0.1',
        authToken: 'test-secret',
      })
      authServer.setExecutor(createMockExecutor())
      await authServer.start()

      const addr = authServer.getAddress()!
      authUrl = `http://${addr.host}:${addr.port}`
    })

    afterEach(async () => {
      await authServer.stop()
    })

    it('should reject unauthenticated requests', async () => {
      const { status } = await fetchJson(`${authUrl}/tasks`)
      expect(status).toBe(401)
    })

    it('should accept authenticated requests', async () => {
      const { status } = await fetchJson(`${authUrl}/tasks`, {
        headers: { Authorization: 'Bearer test-secret' },
      })
      expect(status).toBe(200)
    })

    it('should reject wrong token', async () => {
      const { status } = await fetchJson(`${authUrl}/tasks`, {
        headers: { Authorization: 'Bearer wrong-token' },
      })
      expect(status).toBe(401)
    })
  })

  describe('CORS', () => {
    it('should handle OPTIONS preflight', async () => {
      const response = await fetch(`${baseUrl}/messages`, {
        method: 'OPTIONS',
      })
      expect(response.status).toBe(204)
    })
  })

  describe('404', () => {
    it('should return 404 for unknown routes', async () => {
      const { status } = await fetchJson(`${baseUrl}/nonexistent`)
      expect(status).toBe(404)
    })
  })

  describe('lifecycle', () => {
    it('should prevent double start', async () => {
      await expect(server.start()).rejects.toThrow('already running')
    })

    it('should get address after start', () => {
      const addr = server.getAddress()
      expect(addr).toBeDefined()
      expect(addr!.port).toBeGreaterThan(0)
    })

    it('should return null address when stopped', async () => {
      await server.stop()
      expect(server.getAddress()).toBeNull()
    })

    it('should handle double stop gracefully', async () => {
      await server.stop()
      await server.stop() // Should not throw
    })
  })
})
