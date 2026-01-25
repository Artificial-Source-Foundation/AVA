/**
 * Webhook Manager Tests
 */

import { describe, it, expect, beforeEach } from 'vitest'
import { WebhookManager } from '../../src/webhooks/index.js'
import type { WebhookEvent } from '../../src/webhooks/types.js'

describe('WebhookManager', () => {
  let manager: WebhookManager

  beforeEach(() => {
    manager = new WebhookManager()
  })

  describe('webhook registration', () => {
    it('should register a webhook', () => {
      const id = manager.registerWebhook({
        name: 'Test Webhook',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['mission.completed'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      expect(id).toMatch(/^webhook_/)
      expect(manager.getWebhook(id)).toBeDefined()
    })

    it('should list all webhooks', () => {
      manager.registerWebhook({
        name: 'Webhook 1',
        url: 'https://example.com/webhook1',
        method: 'POST',
        events: ['mission.completed'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      manager.registerWebhook({
        name: 'Webhook 2',
        url: 'https://example.com/webhook2',
        method: 'POST',
        events: ['task.completed'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      const webhooks = manager.listWebhooks()
      expect(webhooks).toHaveLength(2)
    })

    it('should update webhook', () => {
      const id = manager.registerWebhook({
        name: 'Original Name',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['mission.completed'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      const updated = manager.updateWebhook(id, { name: 'Updated Name' })
      expect(updated).toBe(true)
      expect(manager.getWebhook(id)?.name).toBe('Updated Name')
    })

    it('should remove webhook', () => {
      const id = manager.registerWebhook({
        name: 'Test',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      expect(manager.removeWebhook(id)).toBe(true)
      expect(manager.getWebhook(id)).toBeUndefined()
    })
  })

  describe('event matching', () => {
    it('should match exact event types', async () => {
      manager.registerWebhook({
        name: 'Mission Webhook',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['mission.completed'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      const event: WebhookEvent = {
        type: 'mission.completed',
        timestamp: new Date().toISOString(),
        data: { missionId: '123' },
        missionId: '123',
      }

      const deliveryIds = await manager.dispatchEvent(event)
      expect(deliveryIds.length).toBeGreaterThan(0)
    })

    it('should match wildcard events', async () => {
      manager.registerWebhook({
        name: 'All Events',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      const event: WebhookEvent = {
        type: 'anything.here',
        timestamp: new Date().toISOString(),
        data: {},
      }

      const deliveryIds = await manager.dispatchEvent(event)
      expect(deliveryIds.length).toBeGreaterThan(0)
    })

    it('should match prefix wildcards', async () => {
      manager.registerWebhook({
        name: 'Mission Events',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['mission.*'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      const event1: WebhookEvent = {
        type: 'mission.started',
        timestamp: new Date().toISOString(),
        data: {},
      }

      const event2: WebhookEvent = {
        type: 'task.started',
        timestamp: new Date().toISOString(),
        data: {},
      }

      const deliveryIds1 = await manager.dispatchEvent(event1)
      const deliveryIds2 = await manager.dispatchEvent(event2)

      expect(deliveryIds1.length).toBeGreaterThan(0)
      expect(deliveryIds2).toHaveLength(0)
    })

    it('should skip disabled webhooks', async () => {
      manager.registerWebhook({
        name: 'Disabled Webhook',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['*'],
        enabled: false,
        timeout: 5000,
        format: 'json',
      })

      const event: WebhookEvent = {
        type: 'test.event',
        timestamp: new Date().toISOString(),
        data: {},
      }

      const deliveryIds = await manager.dispatchEvent(event)
      expect(deliveryIds).toHaveLength(0)
    })
  })

  describe('delivery tracking', () => {
    it('should track deliveries', async () => {
      manager.registerWebhook({
        name: 'Test Webhook',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['test.event'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      const event: WebhookEvent = {
        type: 'test.event',
        timestamp: new Date().toISOString(),
        data: { key: 'value' },
      }

      const [deliveryId] = await manager.dispatchEvent(event)
      const delivery = manager.getDelivery(deliveryId)

      expect(delivery).toBeDefined()
      expect(delivery?.event).toBe('test.event')
    })

    it('should list deliveries', async () => {
      const webhookId = manager.registerWebhook({
        name: 'Test',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      await manager.dispatchEvent({ type: 'event1', timestamp: new Date().toISOString(), data: {} })
      await manager.dispatchEvent({ type: 'event2', timestamp: new Date().toISOString(), data: {} })

      const deliveries = manager.listDeliveries(webhookId)
      expect(deliveries).toHaveLength(2)
    })
  })

  describe('statistics', () => {
    it('should calculate stats', async () => {
      manager.registerWebhook({
        name: 'Test',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      await manager.dispatchEvent({ type: 'event', timestamp: new Date().toISOString(), data: {} })

      const stats = manager.getStats()
      expect(stats.total).toBeGreaterThanOrEqual(1)
    })
  })

  describe('cleanup', () => {
    it('should clear old deliveries', async () => {
      manager.registerWebhook({
        name: 'Test',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      await manager.dispatchEvent({ type: 'event', timestamp: new Date().toISOString(), data: {} })

      // Wait a tiny bit to ensure the delivery timestamp is in the past
      await new Promise(resolve => setTimeout(resolve, 10))

      // Clear deliveries older than 1ms (effectively all of them after the wait)
      const cleared = manager.clearOldDeliveries(1)
      expect(cleared).toBeGreaterThanOrEqual(1)
    })

    it('should not clear recent deliveries', async () => {
      manager.registerWebhook({
        name: 'Test',
        url: 'https://example.com/webhook',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'json',
      })

      await manager.dispatchEvent({ type: 'event', timestamp: new Date().toISOString(), data: {} })

      // Clear deliveries older than 1 hour - our recent delivery should not be cleared
      const cleared = manager.clearOldDeliveries(60 * 60 * 1000)
      expect(cleared).toBe(0)
    })
  })

  describe('webhook formats', () => {
    it('should format slack payload', async () => {
      const webhookId = manager.registerWebhook({
        name: 'Slack Webhook',
        url: 'https://hooks.slack.com/services/test',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'slack',
      })

      const event: WebhookEvent = {
        type: 'mission.completed',
        timestamp: new Date().toISOString(),
        data: { missionId: 'test-123' },
        missionId: 'test-123',
      }

      const [deliveryId] = await manager.dispatchEvent(event)
      const delivery = manager.getDelivery(deliveryId)

      expect(delivery?.payload).toHaveProperty('text')
      expect(delivery?.payload).toHaveProperty('attachments')
    })

    it('should format discord payload', async () => {
      manager.registerWebhook({
        name: 'Discord Webhook',
        url: 'https://discord.com/api/webhooks/test',
        method: 'POST',
        events: ['*'],
        enabled: true,
        timeout: 5000,
        format: 'discord',
      })

      const event: WebhookEvent = {
        type: 'task.completed',
        timestamp: new Date().toISOString(),
        data: { taskId: 'task-456' },
      }

      const [deliveryId] = await manager.dispatchEvent(event)
      const delivery = manager.getDelivery(deliveryId)

      expect(delivery?.payload).toHaveProperty('embeds')
    })
  })
})
