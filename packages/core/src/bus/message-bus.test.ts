/**
 * Message Bus Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import { PolicyEngine } from '../policy/engine.js'
import { MessageBus } from './message-bus.js'
import {
  BusMessageType,
  type ToolConfirmationRequest,
  type ToolConfirmationResponse,
} from './types.js'

describe('MessageBus', () => {
  let bus: MessageBus

  beforeEach(() => {
    bus = new MessageBus()
  })

  // =========================================================================
  // Publish/Subscribe
  // =========================================================================

  describe('publish/subscribe', () => {
    it('should deliver messages to subscribers', () => {
      const handler = vi.fn()
      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, handler)

      bus.publish({
        type: BusMessageType.TOOL_EXECUTION_START,
        correlationId: 'test-1',
        timestamp: Date.now(),
        toolName: 'bash',
        toolArgs: { command: 'ls' },
      })

      expect(handler).toHaveBeenCalledTimes(1)
      expect(handler.mock.calls[0]![0].toolName).toBe('bash')
    })

    it('should not deliver messages to wrong type subscribers', () => {
      const handler = vi.fn()
      bus.subscribe(BusMessageType.TOOL_EXECUTION_SUCCESS, handler)

      bus.publish({
        type: BusMessageType.TOOL_EXECUTION_START,
        correlationId: 'test-1',
        timestamp: Date.now(),
        toolName: 'bash',
        toolArgs: { command: 'ls' },
      })

      expect(handler).not.toHaveBeenCalled()
    })

    it('should support multiple subscribers', () => {
      const handler1 = vi.fn()
      const handler2 = vi.fn()

      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, handler1)
      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, handler2)

      bus.publish({
        type: BusMessageType.TOOL_EXECUTION_START,
        correlationId: 'test-1',
        timestamp: Date.now(),
        toolName: 'bash',
        toolArgs: {},
      })

      expect(handler1).toHaveBeenCalledTimes(1)
      expect(handler2).toHaveBeenCalledTimes(1)
    })

    it('should unsubscribe correctly', () => {
      const handler = vi.fn()
      const unsub = bus.subscribe(BusMessageType.TOOL_EXECUTION_START, handler)

      unsub()

      bus.publish({
        type: BusMessageType.TOOL_EXECUTION_START,
        correlationId: 'test-1',
        timestamp: Date.now(),
        toolName: 'bash',
        toolArgs: {},
      })

      expect(handler).not.toHaveBeenCalled()
    })

    it('should handle handler errors gracefully', () => {
      const errorHandler = vi.fn(() => {
        throw new Error('boom')
      })
      const normalHandler = vi.fn()

      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, errorHandler)
      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, normalHandler)

      bus.publish({
        type: BusMessageType.TOOL_EXECUTION_START,
        correlationId: 'test-1',
        timestamp: Date.now(),
        toolName: 'bash',
        toolArgs: {},
      })

      // Error handler threw but normal handler still ran
      expect(errorHandler).toHaveBeenCalled()
      expect(normalHandler).toHaveBeenCalled()
    })
  })

  // =========================================================================
  // Request/Response
  // =========================================================================

  describe('request/response', () => {
    it('should match requests and responses by correlation ID', async () => {
      // Set up response handler
      bus.subscribe<ToolConfirmationRequest>(BusMessageType.TOOL_CONFIRMATION_REQUEST, (msg) => {
        // Simulate UI responding
        bus.publish({
          type: BusMessageType.TOOL_CONFIRMATION_RESPONSE,
          correlationId: msg.correlationId,
          timestamp: Date.now(),
          confirmed: true,
        })
      })

      const response = await bus.request<ToolConfirmationRequest, ToolConfirmationResponse>(
        {
          type: BusMessageType.TOOL_CONFIRMATION_REQUEST,
          toolName: 'bash',
          toolArgs: { command: 'ls' },
          riskLevel: 'low',
        } as Omit<ToolConfirmationRequest, 'correlationId' | 'timestamp'>,
        BusMessageType.TOOL_CONFIRMATION_RESPONSE,
        5000
      )

      expect(response.confirmed).toBe(true)
    })

    it('should timeout if no response', async () => {
      await expect(
        bus.request(
          {
            type: BusMessageType.TOOL_CONFIRMATION_REQUEST,
            toolName: 'bash',
            toolArgs: {},
            riskLevel: 'low',
          } as Omit<ToolConfirmationRequest, 'correlationId' | 'timestamp'>,
          BusMessageType.TOOL_CONFIRMATION_RESPONSE,
          100 // Very short timeout
        )
      ).rejects.toThrow('timeout')
    })

    it('should not match wrong correlation IDs', async () => {
      // Subscribe and respond with WRONG correlation ID
      bus.subscribe<ToolConfirmationRequest>(BusMessageType.TOOL_CONFIRMATION_REQUEST, () => {
        bus.publish({
          type: BusMessageType.TOOL_CONFIRMATION_RESPONSE,
          correlationId: 'wrong-id',
          timestamp: Date.now(),
          confirmed: true,
        })
      })

      await expect(
        bus.request(
          {
            type: BusMessageType.TOOL_CONFIRMATION_REQUEST,
            toolName: 'bash',
            toolArgs: {},
            riskLevel: 'low',
          } as Omit<ToolConfirmationRequest, 'correlationId' | 'timestamp'>,
          BusMessageType.TOOL_CONFIRMATION_RESPONSE,
          200
        )
      ).rejects.toThrow('timeout')
    })
  })

  // =========================================================================
  // Tool Confirmation Flow
  // =========================================================================

  describe('confirmToolExecution', () => {
    it('should auto-approve when no policy engine', async () => {
      const result = await bus.confirmToolExecution('bash', { command: 'ls' })
      expect(result.confirmed).toBe(true)
    })

    it('should approve when policy says allow', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'allow-read',
            toolName: 'read_file',
            decision: 'allow',
            priority: 100,
            source: 'test',
          },
        ],
      })
      const bus = new MessageBus(engine)

      const result = await bus.confirmToolExecution('read_file', { path: '/foo.ts' })
      expect(result.confirmed).toBe(true)
    })

    it('should deny when policy says deny', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'deny-all',
            toolName: '*',
            decision: 'deny',
            priority: 100,
            source: 'test',
            denyMessage: 'Everything is blocked',
          },
        ],
      })
      const bus = new MessageBus(engine)

      // Use a non-bash tool to avoid compound command checking
      const result = await bus.confirmToolExecution('write_file', { path: '/foo', content: '' })
      expect(result.confirmed).toBe(false)
      expect(result.reason).toContain('Everything is blocked')
    })

    it('should auto-approve when ask_user but no UI', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'ask-bash',
            toolName: 'bash',
            decision: 'ask_user',
            priority: 100,
            source: 'test',
          },
        ],
      })
      const bus = new MessageBus(engine)

      // No UI subscribers → auto-approve
      const result = await bus.confirmToolExecution('bash', { command: 'npm build' })
      expect(result.confirmed).toBe(true)
      expect(result.reason).toContain('No confirmation UI')
    })

    it('should route to UI when ask_user and UI is connected', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'ask-bash',
            toolName: 'bash',
            decision: 'ask_user',
            priority: 100,
            source: 'test',
          },
        ],
      })
      const bus = new MessageBus(engine)

      // Simulate UI connection
      bus.subscribe<ToolConfirmationRequest>(BusMessageType.TOOL_CONFIRMATION_REQUEST, (msg) => {
        bus.publish({
          type: BusMessageType.TOOL_CONFIRMATION_RESPONSE,
          correlationId: msg.correlationId,
          timestamp: Date.now(),
          confirmed: true,
          rememberChoice: 'session',
        })
      })

      const result = await bus.confirmToolExecution('bash', { command: 'npm build' })
      expect(result.confirmed).toBe(true)
      expect(result.remember).toBe('session')
    })

    it('should handle user denial', async () => {
      const engine = new PolicyEngine({
        rules: [
          {
            name: 'ask-bash',
            toolName: 'bash',
            decision: 'ask_user',
            priority: 100,
            source: 'test',
          },
        ],
      })
      const bus = new MessageBus(engine)

      bus.subscribe<ToolConfirmationRequest>(BusMessageType.TOOL_CONFIRMATION_REQUEST, (msg) => {
        bus.publish({
          type: BusMessageType.TOOL_CONFIRMATION_RESPONSE,
          correlationId: msg.correlationId,
          timestamp: Date.now(),
          confirmed: false,
          denyReason: 'Too dangerous',
        })
      })

      const result = await bus.confirmToolExecution('bash', { command: 'rm -rf /' })
      expect(result.confirmed).toBe(false)
      expect(result.reason).toContain('Too dangerous')
    })
  })

  // =========================================================================
  // Convenience Publishers
  // =========================================================================

  describe('convenience publishers', () => {
    it('should publish tool start notification', () => {
      const handler = vi.fn()
      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, handler)

      bus.notifyToolStart('bash', { command: 'ls' })

      expect(handler).toHaveBeenCalledTimes(1)
      expect(handler.mock.calls[0]![0].toolName).toBe('bash')
    })

    it('should publish tool success notification', () => {
      const handler = vi.fn()
      bus.subscribe(BusMessageType.TOOL_EXECUTION_SUCCESS, handler)

      bus.notifyToolSuccess('bash', 150, 'output preview')

      expect(handler).toHaveBeenCalledTimes(1)
      expect(handler.mock.calls[0]![0].durationMs).toBe(150)
    })

    it('should publish tool failure notification', () => {
      const handler = vi.fn()
      bus.subscribe(BusMessageType.TOOL_EXECUTION_FAILURE, handler)

      bus.notifyToolFailure('bash', 'Command failed', 200)

      expect(handler).toHaveBeenCalledTimes(1)
      expect(handler.mock.calls[0]![0].error).toBe('Command failed')
    })
  })

  // =========================================================================
  // Lifecycle
  // =========================================================================

  describe('lifecycle', () => {
    it('should report subscriber presence', () => {
      expect(bus.hasSubscribers(BusMessageType.TOOL_EXECUTION_START)).toBe(false)

      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, () => {})

      expect(bus.hasSubscribers(BusMessageType.TOOL_EXECUTION_START)).toBe(true)
    })

    it('should clear all subscribers', () => {
      bus.subscribe(BusMessageType.TOOL_EXECUTION_START, () => {})
      bus.subscribe(BusMessageType.TOOL_EXECUTION_SUCCESS, () => {})

      bus.clear()

      expect(bus.hasSubscribers(BusMessageType.TOOL_EXECUTION_START)).toBe(false)
      expect(bus.hasSubscribers(BusMessageType.TOOL_EXECUTION_SUCCESS)).toBe(false)
    })
  })
})
