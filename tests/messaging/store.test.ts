/**
 * Tests for Delta9 Message Store
 */

import { describe, it, expect, beforeEach } from 'vitest'
import { MessageStore, resetMessageStore } from '../../src/messaging/store.js'
import type { MessageEvent } from '../../src/messaging/types.js'

describe('MessageStore', () => {
  let store: MessageStore

  beforeEach(() => {
    resetMessageStore()
    store = new MessageStore({ enableAutoCleanup: false, baseDir: '/tmp/delta9-test-' + Date.now() })
    store.clear() // Ensure clean state
  })

  describe('send', () => {
    it('should send message to specific agent', () => {
      const result = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Execute task',
        body: 'Please implement the authentication system',
      })

      expect(result.success).toBe(true)
      expect(result.messageId).toBeDefined()
      expect(result.recipients).toEqual(['operator'])
    })

    it('should send broadcast message to all agents', () => {
      const result = store.send('commander', {
        to: 'broadcast',
        type: 'alert',
        subject: 'System alert',
        body: 'Budget warning at 80%',
      })

      expect(result.success).toBe(true)
      expect(result.recipients!.length).toBeGreaterThan(10) // All agents
      expect(result.recipients).toContain('operator')
      expect(result.recipients).toContain('cipher')
      expect(result.recipients).toContain('recon')
    })

    it('should send message to council group', () => {
      const result = store.send('commander', {
        to: 'council',
        type: 'request',
        subject: 'Need architectural decision',
        body: 'How should we structure the auth system?',
      })

      expect(result.success).toBe(true)
      expect(result.recipients).toEqual(['cipher', 'vector', 'prism', 'apex'])
    })

    it('should send message to operators group', () => {
      const result = store.send('commander', {
        to: 'operators',
        type: 'coordination',
        subject: 'Task assignment',
        body: 'New tasks available',
      })

      expect(result.success).toBe(true)
      expect(result.recipients).toEqual(['operator', 'operator_complex'])
    })

    it('should send message to support group (Delta Team)', () => {
      const result = store.send('commander', {
        to: 'support',
        type: 'status',
        subject: 'Support check',
        body: 'Status report needed',
      })

      expect(result.success).toBe(true)
      expect(result.recipients).toContain('recon')
      expect(result.recipients).toContain('sigint')
      expect(result.recipients).toContain('spectre')
    })

    it('should set message priority', () => {
      store.send('commander', {
        to: 'operator',
        type: 'alert',
        subject: 'Critical issue',
        body: 'Build is broken',
        priority: 'critical',
      })

      const inbox = store.getInbox({ agentId: 'operator' })
      expect(inbox.messages[0].priority).toBe('critical')
    })

    it('should set message expiration', () => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Test body',
        ttlMs: 5000,
      })

      const inbox = store.getInbox({ agentId: 'operator' })
      expect(inbox.messages[0].expiresAt).toBeDefined()
    })

    it('should include task and mission IDs', () => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Task request',
        body: 'Do the thing',
        taskId: 'task-123',
        missionId: 'mission-456',
      })

      const inbox = store.getInbox({ agentId: 'operator' })
      expect(inbox.messages[0].taskId).toBe('task-123')
      expect(inbox.messages[0].missionId).toBe('mission-456')
    })
  })

  describe('getInbox', () => {
    beforeEach(() => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Task 1',
        body: 'First task',
        taskId: 'task-1',
      })
      store.send('validator', {
        to: 'operator',
        type: 'response',
        subject: 'Validation result',
        body: 'Task passed',
        taskId: 'task-1',
      })
      store.send('scout', {
        to: 'operator',
        type: 'status',
        subject: 'Recon complete',
        body: 'Found 5 files',
      })
    })

    it('should return all messages for agent', () => {
      const inbox = store.getInbox({ agentId: 'operator' })

      expect(inbox.success).toBe(true)
      expect(inbox.messages).toHaveLength(3)
      expect(inbox.totalCount).toBe(3)
    })

    it('should filter by message type', () => {
      const inbox = store.getInbox({
        agentId: 'operator',
        types: ['request'],
      })

      expect(inbox.messages).toHaveLength(1)
      expect(inbox.messages[0].type).toBe('request')
    })

    it('should filter by sender', () => {
      const inbox = store.getInbox({
        agentId: 'operator',
        from: 'validator',
      })

      expect(inbox.messages).toHaveLength(1)
      expect(inbox.messages[0].from).toBe('validator')
    })

    it('should filter by task ID', () => {
      const inbox = store.getInbox({
        agentId: 'operator',
        taskId: 'task-1',
      })

      expect(inbox.messages).toHaveLength(2)
    })

    it('should filter unread only', () => {
      // Mark one as read
      const inbox = store.getInbox({ agentId: 'operator' })
      store.markRead(inbox.messages[0].id, 'operator')

      const unreadInbox = store.getInbox({
        agentId: 'operator',
        unreadOnly: true,
      })

      expect(unreadInbox.messages).toHaveLength(2)
      expect(unreadInbox.unreadCount).toBe(2)
    })

    it('should sort by newest first', async () => {
      // Clear and create fresh messages with distinct timestamps
      store.clear()

      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'First message',
        body: 'Sent first',
      })

      // Small delay to ensure different timestamp
      await new Promise((resolve) => setTimeout(resolve, 5))

      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Second message',
        body: 'Sent second',
      })

      const inbox = store.getInbox({ agentId: 'operator' })

      // Newest (second) should be first
      expect(inbox.messages[0].subject).toBe('Second message')
      expect(inbox.messages[1].subject).toBe('First message')
    })

    it('should respect limit', () => {
      const inbox = store.getInbox({
        agentId: 'operator',
        limit: 2,
      })

      expect(inbox.messages).toHaveLength(2)
      expect(inbox.totalCount).toBe(3) // Total available
    })

    it('should return empty inbox for unknown agent', () => {
      const inbox = store.getInbox({ agentId: 'unknown-agent' })

      expect(inbox.success).toBe(true)
      expect(inbox.messages).toHaveLength(0)
    })
  })

  describe('read', () => {
    it('should read message by ID', () => {
      const sendResult = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Test body',
      })

      const readResult = store.read(sendResult.messageId!, 'operator')

      expect(readResult.success).toBe(true)
      expect(readResult.message).toBeDefined()
      expect(readResult.message!.subject).toBe('Test')
      expect(readResult.message!.body).toBe('Test body')
    })

    it('should fail for non-existent message', () => {
      const result = store.read('non-existent-id', 'operator')

      expect(result.success).toBe(false)
      expect(result.error).toContain('not found')
    })

    it('should fail for unauthorized agent', () => {
      const sendResult = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Private',
        body: 'Secret stuff',
      })

      const readResult = store.read(sendResult.messageId!, 'unauthorized-agent')

      expect(readResult.success).toBe(false)
      expect(readResult.error).toContain('Not authorized')
    })

    it('should allow sender to read their own message', () => {
      const sendResult = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Test body',
      })

      const readResult = store.read(sendResult.messageId!, 'commander')

      expect(readResult.success).toBe(true)
    })
  })

  describe('markRead', () => {
    it('should mark message as read', () => {
      const sendResult = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Test body',
      })

      const marked = store.markRead(sendResult.messageId!, 'operator')

      expect(marked).toBe(true)

      const inbox = store.getInbox({ agentId: 'operator' })
      expect(inbox.messages[0].readAt).toBeDefined()
      expect(inbox.unreadCount).toBe(0)
    })

    it('should return false for non-existent message', () => {
      const marked = store.markRead('non-existent', 'operator')

      expect(marked).toBe(false)
    })

    it('should return false for unauthorized agent', () => {
      const sendResult = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Test body',
      })

      const marked = store.markRead(sendResult.messageId!, 'unauthorized')

      expect(marked).toBe(false)
    })
  })

  describe('reply', () => {
    it('should reply to a message', () => {
      const original = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Do this task',
        body: 'Please complete task A',
      })

      const reply = store.reply('operator', original.messageId!, 'Task completed successfully')

      expect(reply.success).toBe(true)
      expect(reply.messageId).toBeDefined()

      // Check commander's inbox
      const inbox = store.getInbox({ agentId: 'commander' })
      expect(inbox.messages).toHaveLength(1)
      expect(inbox.messages[0].subject).toBe('Re: Do this task')
      expect(inbox.messages[0].replyTo).toBe(original.messageId)
      expect(inbox.messages[0].type).toBe('response')
    })

    it('should fail to reply to non-existent message', () => {
      const reply = store.reply('operator', 'non-existent', 'Reply body')

      expect(reply.success).toBe(false)
      expect(reply.error).toContain('not found')
    })

    it('should preserve task and mission IDs', () => {
      const original = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Task',
        body: 'Do it',
        taskId: 'task-123',
        missionId: 'mission-456',
      })

      store.reply('operator', original.messageId!, 'Done')

      const inbox = store.getInbox({ agentId: 'commander' })
      expect(inbox.messages[0].taskId).toBe('task-123')
      expect(inbox.messages[0].missionId).toBe('mission-456')
    })
  })

  describe('getThread', () => {
    it('should return all messages in a thread', () => {
      // Create a thread
      const msg1 = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Original',
        body: 'First message',
      })

      const msg2 = store.reply('operator', msg1.messageId!, 'Reply 1')
      store.reply('commander', msg2.messageId!, 'Reply 2')

      const thread = store.getThread(msg1.messageId!)

      expect(thread).toHaveLength(3)
      expect(thread[0].subject).toBe('Original')
      expect(thread[1].subject).toBe('Re: Original')
      expect(thread[2].subject).toBe('Re: Re: Original')
    })

    it('should return single message if no replies', () => {
      const msg = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Standalone',
        body: 'No replies',
      })

      const thread = store.getThread(msg.messageId!)

      expect(thread).toHaveLength(1)
    })

    it('should return empty array for non-existent message', () => {
      const thread = store.getThread('non-existent')

      expect(thread).toHaveLength(0)
    })
  })

  describe('expiration', () => {
    it('should expire messages after TTL', async () => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Expiring',
        body: 'Short lived',
        ttlMs: 10,
      })

      // Wait for expiration
      await new Promise((resolve) => setTimeout(resolve, 20))

      const inbox = store.getInbox({ agentId: 'operator' })
      expect(inbox.messages).toHaveLength(0)
    })

    it('should return error when reading expired message', async () => {
      const sendResult = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Expiring',
        body: 'Short lived',
        ttlMs: 10,
      })

      await new Promise((resolve) => setTimeout(resolve, 20))

      const readResult = store.read(sendResult.messageId!, 'operator')
      expect(readResult.success).toBe(false)
      expect(readResult.error).toContain('expired')
    })

    it('should cleanup expired messages', async () => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Short 1',
        body: 'Short lived',
        ttlMs: 10,
      })
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Short 2',
        body: 'Short lived',
        ttlMs: 10,
      })
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Long',
        body: 'Long lived',
        ttlMs: 60000,
      })

      await new Promise((resolve) => setTimeout(resolve, 20))

      const count = store.cleanupExpired()
      expect(count).toBe(2)

      const inbox = store.getInbox({ agentId: 'operator' })
      expect(inbox.messages).toHaveLength(1)
      expect(inbox.messages[0].subject).toBe('Long')
    })
  })

  describe('events', () => {
    it('should emit sent event', () => {
      const events: MessageEvent[] = []
      store.on((event) => events.push(event))

      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Test body',
      })

      expect(events).toHaveLength(1)
      expect(events[0].type).toBe('sent')
      expect(events[0].from).toBe('commander')
      expect(events[0].subject).toBe('Test')
    })

    it('should emit read event', () => {
      const events: MessageEvent[] = []
      store.on((event) => events.push(event))

      const sendResult = store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Test body',
      })

      store.markRead(sendResult.messageId!, 'operator')

      expect(events.find((e) => e.type === 'read')).toBeDefined()
    })

    it('should emit expired event', async () => {
      const events: MessageEvent[] = []
      store.on((event) => events.push(event))

      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Expiring',
        body: 'Short lived',
        ttlMs: 10,
      })

      await new Promise((resolve) => setTimeout(resolve, 20))

      store.cleanupExpired()

      expect(events.find((e) => e.type === 'expired')).toBeDefined()
    })

    it('should support removing listeners', () => {
      const events: MessageEvent[] = []
      const listener = (event: MessageEvent) => events.push(event)

      store.on(listener)
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test 1',
        body: 'Body 1',
      })

      store.off(listener)
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test 2',
        body: 'Body 2',
      })

      expect(events).toHaveLength(1)
    })
  })

  describe('getStats', () => {
    it('should return correct statistics', () => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Request 1',
        body: 'Body',
      })
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Request 2',
        body: 'Body',
      })
      store.send('validator', {
        to: 'operator',
        type: 'response',
        subject: 'Response',
        body: 'Body',
      })

      const stats = store.getStats()

      expect(stats.totalMessages).toBe(3)
      expect(stats.messagesByType.get('request')).toBe(2)
      expect(stats.messagesByType.get('response')).toBe(1)
      expect(stats.inboxSizes.get('operator')).toBe(3)
      expect(stats.unreadCounts.get('operator')).toBe(3)
    })
  })

  describe('clear', () => {
    it('should clear all messages', () => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Body',
      })

      store.clear()

      const inbox = store.getInbox({ agentId: 'operator' })
      expect(inbox.messages).toHaveLength(0)
    })
  })

  describe('destroy', () => {
    it('should clear all state', () => {
      store.send('commander', {
        to: 'operator',
        type: 'request',
        subject: 'Test',
        body: 'Body',
      })
      store.on(() => {})

      store.destroy()

      const stats = store.getStats()
      expect(stats.totalMessages).toBe(0)
    })
  })
})
