/**
 * A2A SSE Streaming
 *
 * Server-Sent Events writer for streaming A2A task events
 * to HTTP clients. Framework-agnostic: works with any
 * writable stream (Node http.ServerResponse, etc.)
 */

import type { A2AEvent, A2AMessage, A2ATask, TaskStatusUpdateEvent } from './types.js'

// ============================================================================
// Types
// ============================================================================

/**
 * Minimal writable interface for SSE streaming.
 * Compatible with http.ServerResponse and any stream with write/end.
 */
export interface SSEWritable {
  write(data: string): boolean
  end(data?: string): void
  /** Whether the stream is still writable */
  writable?: boolean
}

// ============================================================================
// SSE Headers
// ============================================================================

/** Standard SSE response headers */
export const SSE_HEADERS: Record<string, string> = {
  'Content-Type': 'text/event-stream',
  'Cache-Control': 'no-cache',
  Connection: 'keep-alive',
  'X-Accel-Buffering': 'no',
}

// ============================================================================
// SSE Writer
// ============================================================================

/**
 * SSE writer for streaming A2A events to a client.
 *
 * Usage:
 * ```ts
 * const writer = new SSEWriter(res)
 * writer.sendEvent(statusUpdateEvent)
 * writer.close()
 * ```
 */
export class SSEWriter {
  private closed = false

  constructor(private stream: SSEWritable) {}

  /**
   * Send an A2A event as an SSE message.
   */
  sendEvent(event: A2AEvent): boolean {
    if (this.closed) return false
    if (this.stream.writable === false) return false

    const data = JSON.stringify(event)
    return this.stream.write(formatSSE(event.kind, data))
  }

  /**
   * Send a raw SSE message with custom event type and data.
   */
  sendRaw(eventType: string, data: string): boolean {
    if (this.closed) return false
    if (this.stream.writable === false) return false

    return this.stream.write(formatSSE(eventType, data))
  }

  /**
   * Send a comment (SSE keepalive).
   */
  sendComment(text: string): boolean {
    if (this.closed) return false
    if (this.stream.writable === false) return false

    return this.stream.write(`: ${text}\n\n`)
  }

  /**
   * Close the SSE stream.
   */
  close(): void {
    if (this.closed) return
    this.closed = true

    try {
      this.stream.end()
    } catch {
      // Stream may already be closed
    }
  }

  /**
   * Check if the writer is still open.
   */
  isOpen(): boolean {
    return !this.closed && this.stream.writable !== false
  }
}

// ============================================================================
// SSE Formatting
// ============================================================================

/**
 * Format an SSE message according to the spec.
 *
 * @param eventType - Event type (maps to SSE 'event' field)
 * @param data - JSON string payload
 * @returns Formatted SSE message string
 */
export function formatSSE(eventType: string, data: string): string {
  return `event: ${eventType}\ndata: ${data}\n\n`
}

/**
 * Format a JSON-RPC style SSE message (Gemini CLI compatible).
 *
 * @param taskId - Task identifier
 * @param event - A2A event payload
 * @returns Formatted SSE data line
 */
export function formatJsonRpcSSE(taskId: string, event: A2AEvent): string {
  const payload = {
    jsonrpc: '2.0',
    id: taskId,
    result: event,
  }
  return `data: ${JSON.stringify(payload)}\n\n`
}

// ============================================================================
// Event Constructors
// ============================================================================

/**
 * Create a status update SSE event from task and message.
 */
export function statusEvent(
  task: A2ATask,
  final: boolean,
  message?: A2AMessage
): TaskStatusUpdateEvent {
  return {
    kind: 'status-update',
    taskId: task.id,
    contextId: task.contextId,
    final,
    status: {
      state: task.status.state,
      message,
      timestamp: new Date().toISOString(),
    },
  }
}

// ============================================================================
// Keepalive
// ============================================================================

/**
 * Create a keepalive interval that sends SSE comments to prevent timeout.
 *
 * @param writer - SSE writer
 * @param intervalMs - Interval between keepalives (default 15s)
 * @returns Cleanup function to stop the keepalive
 */
export function startKeepalive(writer: SSEWriter, intervalMs = 15_000): () => void {
  const handle = setInterval(() => {
    if (!writer.isOpen()) {
      clearInterval(handle)
      return
    }
    writer.sendComment('keepalive')
  }, intervalMs)

  return () => clearInterval(handle)
}
