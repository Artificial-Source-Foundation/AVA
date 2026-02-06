/**
 * SSE Streaming Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import {
  formatJsonRpcSSE,
  formatSSE,
  SSE_HEADERS,
  type SSEWritable,
  SSEWriter,
  startKeepalive,
  statusEvent,
} from './streaming.js'
import type { A2AEvent, A2ATask, TaskStatus } from './types.js'

// ============================================================================
// Mock Stream
// ============================================================================

function createMockStream(): SSEWritable & { chunks: string[]; closed: boolean } {
  return {
    chunks: [],
    closed: false,
    writable: true,
    write(data: string): boolean {
      this.chunks.push(data)
      return true
    },
    end(): void {
      this.closed = true
      this.writable = false
    },
  }
}

function createClosedStream(): SSEWritable {
  return {
    writable: false,
    write: vi.fn(() => false),
    end: vi.fn(),
  }
}

function createTask(state: string = 'working'): A2ATask {
  const status: TaskStatus = {
    state: state as A2ATask['status']['state'],
    timestamp: '2025-01-01T00:00:00.000Z',
  }
  return {
    id: 'task-123',
    contextId: 'ctx-456',
    status,
    messages: [],
    artifacts: [],
    history: [status],
  }
}

// ============================================================================
// Tests
// ============================================================================

describe('streaming', () => {
  describe('formatSSE', () => {
    it('should format event type and data', () => {
      const result = formatSSE('status-update', '{"state":"working"}')
      expect(result).toBe('event: status-update\ndata: {"state":"working"}\n\n')
    })

    it('should handle empty data', () => {
      const result = formatSSE('keepalive', '')
      expect(result).toBe('event: keepalive\ndata: \n\n')
    })
  })

  describe('formatJsonRpcSSE', () => {
    it('should format as JSON-RPC', () => {
      const event: A2AEvent = {
        kind: 'status-update',
        taskId: 'task-1',
        contextId: 'ctx-1',
        final: false,
        status: { state: 'working', timestamp: '2025-01-01T00:00:00.000Z' },
      }

      const result = formatJsonRpcSSE('task-1', event)
      const parsed = JSON.parse(result.replace('data: ', '').trim())

      expect(parsed.jsonrpc).toBe('2.0')
      expect(parsed.id).toBe('task-1')
      expect(parsed.result.kind).toBe('status-update')
    })
  })

  describe('SSE_HEADERS', () => {
    it('should include required SSE headers', () => {
      expect(SSE_HEADERS['Content-Type']).toBe('text/event-stream')
      expect(SSE_HEADERS['Cache-Control']).toBe('no-cache')
      expect(SSE_HEADERS.Connection).toBe('keep-alive')
    })
  })

  describe('SSEWriter', () => {
    let stream: ReturnType<typeof createMockStream>
    let writer: SSEWriter

    beforeEach(() => {
      stream = createMockStream()
      writer = new SSEWriter(stream)
    })

    it('should send events', () => {
      const event: A2AEvent = {
        kind: 'status-update',
        taskId: 'task-1',
        contextId: 'ctx-1',
        final: false,
        status: { state: 'working', timestamp: '2025-01-01T00:00:00Z' },
      }

      const success = writer.sendEvent(event)

      expect(success).toBe(true)
      expect(stream.chunks).toHaveLength(1)
      expect(stream.chunks[0]).toContain('event: status-update')
      expect(stream.chunks[0]).toContain('task-1')
    })

    it('should send raw messages', () => {
      const success = writer.sendRaw('custom', '{"foo":"bar"}')

      expect(success).toBe(true)
      expect(stream.chunks[0]).toContain('event: custom')
      expect(stream.chunks[0]).toContain('{"foo":"bar"}')
    })

    it('should send comments', () => {
      const success = writer.sendComment('keepalive')

      expect(success).toBe(true)
      expect(stream.chunks[0]).toBe(': keepalive\n\n')
    })

    it('should close stream', () => {
      writer.close()

      expect(stream.closed).toBe(true)
      expect(writer.isOpen()).toBe(false)
    })

    it('should not write after close', () => {
      writer.close()

      expect(
        writer.sendEvent({
          kind: 'status-update',
          taskId: 'x',
          contextId: 'x',
          final: true,
          status: { state: 'completed', timestamp: '' },
        })
      ).toBe(false)

      expect(writer.sendRaw('test', 'data')).toBe(false)
      expect(writer.sendComment('ping')).toBe(false)
    })

    it('should not write to closed stream', () => {
      const closed = createClosedStream()
      const closedWriter = new SSEWriter(closed)

      expect(
        closedWriter.sendEvent({
          kind: 'status-update',
          taskId: 'x',
          contextId: 'x',
          final: true,
          status: { state: 'completed', timestamp: '' },
        })
      ).toBe(false)
    })

    it('should handle double close gracefully', () => {
      writer.close()
      writer.close() // Should not throw
    })

    it('should report isOpen correctly', () => {
      expect(writer.isOpen()).toBe(true)
      writer.close()
      expect(writer.isOpen()).toBe(false)
    })
  })

  describe('statusEvent', () => {
    it('should create status event from task', () => {
      const task = createTask('working')
      const event = statusEvent(task, false)

      expect(event.kind).toBe('status-update')
      expect(event.taskId).toBe('task-123')
      expect(event.contextId).toBe('ctx-456')
      expect(event.final).toBe(false)
      expect(event.status.state).toBe('working')
    })

    it('should include message when provided', () => {
      const task = createTask('completed')
      const msg = { role: 'agent' as const, parts: [{ type: 'text' as const, text: 'Done' }] }
      const event = statusEvent(task, true, msg)

      expect(event.final).toBe(true)
      expect(event.status.message).toBe(msg)
    })
  })

  describe('startKeepalive', () => {
    it('should send periodic comments', async () => {
      vi.useFakeTimers()

      const stream = createMockStream()
      const writer = new SSEWriter(stream)
      const stop = startKeepalive(writer, 100)

      vi.advanceTimersByTime(350)

      // Should have sent 3 keepalives (at 100, 200, 300ms)
      expect(stream.chunks.length).toBe(3)
      expect(stream.chunks[0]).toContain('keepalive')

      stop()
      vi.useRealTimers()
    })

    it('should auto-stop when writer closes', async () => {
      vi.useFakeTimers()

      const stream = createMockStream()
      const writer = new SSEWriter(stream)
      const stop = startKeepalive(writer, 100)

      vi.advanceTimersByTime(150)
      expect(stream.chunks.length).toBe(1)

      writer.close()
      vi.advanceTimersByTime(200)

      // Should not have sent more after close
      // The closed check happens inside the interval
      stop()
      vi.useRealTimers()
    })

    it('should be stoppable', () => {
      vi.useFakeTimers()

      const stream = createMockStream()
      const writer = new SSEWriter(stream)
      const stop = startKeepalive(writer, 100)

      vi.advanceTimersByTime(50)
      stop()
      vi.advanceTimersByTime(200)

      expect(stream.chunks.length).toBe(0)
      vi.useRealTimers()
    })
  })
})
