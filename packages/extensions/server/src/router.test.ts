import type { IncomingMessage, ServerResponse } from 'node:http'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { HttpRouter } from './router.js'
import type { SessionRouter } from './session-router.js'
import type { ServerConfig } from './types.js'

// Mock auth module
vi.mock('./auth.js', () => ({
  validateRequest: vi.fn().mockResolvedValue(true),
}))

function createMockRequest(method: string, url: string, body?: unknown): IncomingMessage {
  const chunks: Buffer[] = body ? [Buffer.from(JSON.stringify(body))] : []
  const req = {
    method,
    url,
    headers: { authorization: 'Bearer test' },
    on: vi.fn((event: string, handler: (...args: unknown[]) => void) => {
      if (event === 'data') {
        for (const chunk of chunks) handler(chunk)
      }
      if (event === 'end') handler()
      return req
    }),
  } as unknown as IncomingMessage
  return req
}

function createMockResponse(): ServerResponse & {
  _status: number
  _body: string
  _headers: Record<string, string>
} {
  const res = {
    _status: 0,
    _body: '',
    _headers: {} as Record<string, string>,
    writeHead: vi.fn(function (
      this: { _status: number; _headers: Record<string, string> },
      status: number,
      headers?: Record<string, string>
    ) {
      this._status = status
      if (headers) Object.assign(this._headers, headers)
    }),
    end: vi.fn(function (this: { _body: string }, data?: string) {
      if (data) this._body = data
    }),
    write: vi.fn(),
    setHeader: vi.fn(),
  } as unknown as ServerResponse & {
    _status: number
    _body: string
    _headers: Record<string, string>
  }
  return res
}

function createMockSessionRouter(): SessionRouter {
  return {
    startRun: vi.fn().mockResolvedValue('run-123'),
    getStatus: vi.fn().mockReturnValue({
      runId: 'run-123',
      status: 'running',
      startedAt: Date.now(),
    }),
    subscribe: vi.fn().mockReturnValue(() => {}),
    steer: vi.fn().mockReturnValue({ accepted: true }),
    abort: vi.fn().mockReturnValue(true),
    activeCount: 0,
    cleanup: vi.fn(),
    dispose: vi.fn(),
  } as unknown as SessionRouter
}

describe('HttpRouter', () => {
  let router: HttpRouter
  let sessionRouter: SessionRouter
  const config: ServerConfig = { port: 3100, host: '127.0.0.1', tokenFile: '/tmp/tokens.json' }

  beforeEach(() => {
    sessionRouter = createMockSessionRouter()
    router = new HttpRouter(sessionRouter, config)
  })

  describe('GET /api/v1/health', () => {
    it('returns 200 with status ok', async () => {
      const req = createMockRequest('GET', '/api/v1/health')
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(200)
      const body = JSON.parse(res._body)
      expect(body.status).toBe('ok')
    })
  })

  describe('POST /api/v1/run', () => {
    it('starts a new run', async () => {
      const req = createMockRequest('POST', '/api/v1/run', { goal: 'test goal' })
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(201)
      const body = JSON.parse(res._body)
      expect(body.runId).toBe('run-123')
      expect(body.status).toBe('started')
    })

    it('returns 400 without goal', async () => {
      const req = createMockRequest('POST', '/api/v1/run', {})
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(400)
    })
  })

  describe('GET /api/v1/run/:id/status', () => {
    it('returns run status', async () => {
      const req = createMockRequest('GET', '/api/v1/run/run-123/status')
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(200)
      const body = JSON.parse(res._body)
      expect(body.runId).toBe('run-123')
    })

    it('returns 404 for unknown run', async () => {
      vi.mocked(sessionRouter.getStatus).mockReturnValue(null)
      const req = createMockRequest('GET', '/api/v1/run/unknown/status')
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(404)
    })
  })

  describe('POST /api/v1/run/:id/steer', () => {
    it('steers a running agent', async () => {
      const req = createMockRequest('POST', '/api/v1/run/run-123/steer', { message: 'do X' })
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(200)
      const body = JSON.parse(res._body)
      expect(body.accepted).toBe(true)
    })

    it('returns 400 without message', async () => {
      const req = createMockRequest('POST', '/api/v1/run/run-123/steer', {})
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(400)
    })
  })

  describe('DELETE /api/v1/run/:id', () => {
    it('aborts a running agent', async () => {
      const req = createMockRequest('DELETE', '/api/v1/run/run-123')
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(200)
      const body = JSON.parse(res._body)
      expect(body.aborted).toBe(true)
    })
  })

  describe('OPTIONS', () => {
    it('returns 204 for CORS preflight', async () => {
      const req = createMockRequest('OPTIONS', '/api/v1/run')
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(204)
    })
  })

  describe('404', () => {
    it('returns 404 for unknown routes', async () => {
      const req = createMockRequest('GET', '/api/v1/unknown')
      const res = createMockResponse()
      await router.handle(req, res)
      expect(res._status).toBe(404)
    })
  })
})
