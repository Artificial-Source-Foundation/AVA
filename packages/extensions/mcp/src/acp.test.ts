import type { ExtensionAPI } from '@ava/core-v2/extensions'
import { describe, expect, it, vi } from 'vitest'
import type {
  ACPRunRequest,
  ACPRunResponse,
  ACPSteerRequest,
  ACPSteerResponse,
  ACPStreamEvent,
} from './acp.js'
import { ACPServer } from './acp.js'

function createMockAPI(): ExtensionAPI {
  return {
    emit: vi.fn(),
    on: vi.fn().mockReturnValue({ dispose: vi.fn() }),
  } as unknown as ExtensionAPI
}

describe('ACPServer', () => {
  it('can be instantiated without API', () => {
    const server = new ACPServer()
    expect(server).toBeInstanceOf(ACPServer)
  })

  it('can be instantiated with API', () => {
    const api = createMockAPI()
    const server = new ACPServer(api)
    expect(server).toBeInstanceOf(ACPServer)
  })

  describe('run()', () => {
    it('throws when no API provided', async () => {
      const server = new ACPServer()
      const request: ACPRunRequest = { goal: 'Fix the login bug' }
      await expect(server.run(request)).rejects.toThrow('ACP: No ExtensionAPI provided')
    })

    it('returns runId when API is provided', async () => {
      const api = createMockAPI()
      const server = new ACPServer(api)
      const request: ACPRunRequest = { goal: 'Fix the login bug' }
      const response = await server.run(request)
      expect(response.runId).toBeTruthy()
      expect(typeof response.runId).toBe('string')
    })

    it('emits server:run event', async () => {
      const api = createMockAPI()
      const server = new ACPServer(api)
      await server.run({ goal: 'test', context: 'ctx', tools: ['bash'] })
      expect(api.emit).toHaveBeenCalledWith(
        'server:run',
        expect.objectContaining({
          goal: 'test',
          context: 'ctx',
          tools: ['bash'],
        })
      )
    })
  })

  describe('stream()', () => {
    it('throws when no API provided', async () => {
      const server = new ACPServer()
      await expect(async () => {
        const gen = server.stream('run-123')
        await gen.next()
      }).rejects.toThrow('ACP: No ExtensionAPI provided')
    })

    it('is an async generator', () => {
      const api = createMockAPI()
      const server = new ACPServer(api)
      const gen = server.stream('run-123')
      expect(typeof gen.next).toBe('function')
      expect(typeof gen.return).toBe('function')
      expect(typeof gen.throw).toBe('function')
    })
  })

  describe('steer()', () => {
    it('throws when no API provided', async () => {
      const server = new ACPServer()
      const request: ACPSteerRequest = { message: 'Focus on error handling' }
      await expect(server.steer('run-123', request)).rejects.toThrow(
        'ACP: No ExtensionAPI provided'
      )
    })

    it('returns accepted when API is provided', async () => {
      const api = createMockAPI()
      const server = new ACPServer(api)
      const result = await server.steer('run-123', { message: 'redirect' })
      expect(result.accepted).toBe(true)
    })

    it('emits agent:steer event', async () => {
      const api = createMockAPI()
      const server = new ACPServer(api)
      await server.steer('run-123', { message: 'redirect' })
      expect(api.emit).toHaveBeenCalledWith('agent:steer', {
        runId: 'run-123',
        message: 'redirect',
      })
    })
  })

  describe('types', () => {
    it('ACPRunRequest has required goal field', () => {
      const request: ACPRunRequest = { goal: 'test' }
      expect(request.goal).toBe('test')
      expect(request.context).toBeUndefined()
      expect(request.tools).toBeUndefined()
    })

    it('ACPRunResponse has runId', () => {
      const response: ACPRunResponse = { runId: 'abc-123' }
      expect(response.runId).toBe('abc-123')
    })

    it('ACPSteerRequest has message', () => {
      const request: ACPSteerRequest = { message: 'change direction' }
      expect(request.message).toBe('change direction')
    })

    it('ACPSteerResponse has accepted', () => {
      const response: ACPSteerResponse = { accepted: true }
      expect(response.accepted).toBe(true)
    })

    it('ACPStreamEvent has type and data', () => {
      const events: ACPStreamEvent[] = [
        { type: 'text', data: { content: 'hello' } },
        { type: 'tool_use', data: { name: 'bash', input: {} } },
        { type: 'tool_result', data: { output: 'done' } },
        { type: 'error', data: { message: 'failed' } },
        { type: 'done', data: {} },
      ]

      expect(events).toHaveLength(5)
      expect(events[0]?.type).toBe('text')
      expect(events[4]?.type).toBe('done')
    })
  })
})
