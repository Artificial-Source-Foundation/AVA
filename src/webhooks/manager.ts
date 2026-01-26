/**
 * Webhook Manager
 *
 * Manages webhook registrations and deliveries.
 */

import { randomUUID } from 'node:crypto'
import { createHmac } from 'node:crypto'
import type {
  WebhookConfig,
  WebhookDelivery,
  WebhookEvent,
  SlackPayload,
  DiscordPayload,
} from './types.js'

// =============================================================================
// Webhook Manager
// =============================================================================

export class WebhookManager {
  private webhooks: Map<string, WebhookConfig> = new Map()
  private deliveries: Map<string, WebhookDelivery> = new Map()
  private deliveryQueue: WebhookDelivery[] = []
  private isProcessing = false

  // ===========================================================================
  // Webhook Registration
  // ===========================================================================

  /**
   * Register a new webhook
   */
  registerWebhook(config: Omit<WebhookConfig, 'id'>): string {
    const id = `webhook_${randomUUID().slice(0, 8)}`
    const webhook: WebhookConfig = {
      ...config,
      id,
      headers: config.headers || {},
      retry: config.retry || { maxRetries: 3, backoffMs: 1000 },
    }

    this.webhooks.set(id, webhook)
    return id
  }

  /**
   * Update a webhook
   */
  updateWebhook(id: string, updates: Partial<WebhookConfig>): boolean {
    const webhook = this.webhooks.get(id)
    if (!webhook) return false

    Object.assign(webhook, updates)
    return true
  }

  /**
   * Remove a webhook
   */
  removeWebhook(id: string): boolean {
    return this.webhooks.delete(id)
  }

  /**
   * Get a webhook by ID
   */
  getWebhook(id: string): WebhookConfig | undefined {
    return this.webhooks.get(id)
  }

  /**
   * List all webhooks
   */
  listWebhooks(): WebhookConfig[] {
    return Array.from(this.webhooks.values())
  }

  // ===========================================================================
  // Event Dispatch
  // ===========================================================================

  /**
   * Dispatch an event to matching webhooks
   */
  async dispatchEvent(event: WebhookEvent): Promise<string[]> {
    const deliveryIds: string[] = []

    for (const webhook of this.webhooks.values()) {
      if (!webhook.enabled) continue
      if (!this.eventMatchesWebhook(event, webhook)) continue

      const deliveryId = await this.createDelivery(webhook, event)
      deliveryIds.push(deliveryId)
    }

    // Start processing queue if not already running
    this.processQueue()

    return deliveryIds
  }

  /**
   * Check if event matches webhook filters
   */
  private eventMatchesWebhook(event: WebhookEvent, webhook: WebhookConfig): boolean {
    // Check if event type is in webhook's event list
    // Support wildcards like "mission.*" or "task.*"
    return webhook.events.some((pattern) => {
      if (pattern === '*') return true
      if (pattern === event.type) return true
      if (pattern.endsWith('.*')) {
        const prefix = pattern.slice(0, -2)
        return event.type.startsWith(prefix + '.')
      }
      return false
    })
  }

  // ===========================================================================
  // Delivery Management
  // ===========================================================================

  /**
   * Create a delivery record
   */
  private async createDelivery(webhook: WebhookConfig, event: WebhookEvent): Promise<string> {
    const id = `delivery_${randomUUID().slice(0, 8)}`

    const payload = this.formatPayload(webhook, event)

    const delivery: WebhookDelivery = {
      id,
      webhookId: webhook.id,
      event: event.type,
      payload,
      status: 'pending',
      attempts: 0,
      createdAt: new Date().toISOString(),
    }

    this.deliveries.set(id, delivery)
    this.deliveryQueue.push(delivery)

    return id
  }

  /**
   * Format payload based on webhook format
   */
  private formatPayload(webhook: WebhookConfig, event: WebhookEvent): unknown {
    switch (webhook.format) {
      case 'slack':
        return this.formatSlackPayload(event)
      case 'discord':
        return this.formatDiscordPayload(event)
      case 'form':
        return new URLSearchParams(
          Object.entries(event.data).map(([k, v]) => [k, String(v)] as [string, string])
        ).toString()
      case 'json':
      default:
        if (webhook.template) {
          return this.applyTemplate(webhook.template, event)
        }
        return event
    }
  }

  /**
   * Format as Slack payload
   */
  private formatSlackPayload(event: WebhookEvent): SlackPayload {
    const color = this.getEventColor(event.type)
    const title = this.getEventTitle(event.type)

    const fields = Object.entries(event.data)
      .slice(0, 10) // Limit fields
      .map(([key, value]) => ({
        title: this.formatFieldTitle(key),
        value: String(value),
        short: String(value).length < 30,
      }))

    return {
      text: `Delta9: ${title}`,
      attachments: [
        {
          color,
          title: event.type,
          text: event.missionId ? `Mission: ${event.missionId}` : undefined,
          fields,
          footer: 'Delta9',
          ts: Math.floor(new Date(event.timestamp).getTime() / 1000),
        },
      ],
    }
  }

  /**
   * Format as Discord payload
   */
  private formatDiscordPayload(event: WebhookEvent): DiscordPayload {
    const color = this.getEventColorHex(event.type)
    const title = this.getEventTitle(event.type)

    const fields = Object.entries(event.data)
      .slice(0, 10)
      .map(([key, value]) => ({
        name: this.formatFieldTitle(key),
        value: String(value).slice(0, 1024),
        inline: String(value).length < 30,
      }))

    return {
      embeds: [
        {
          title: `Delta9: ${title}`,
          description: event.missionId ? `Mission: ${event.missionId}` : undefined,
          color,
          fields,
          footer: { text: 'Delta9' },
          timestamp: event.timestamp,
        },
      ],
    }
  }

  /**
   * Apply custom template
   */
  private applyTemplate(template: string, event: WebhookEvent): unknown {
    let result = template

    // Replace placeholders like {{event.type}}, {{data.taskId}}, etc.
    result = result.replace(/\{\{([^}]+)\}\}/g, (_, path) => {
      const parts = path.trim().split('.')
      let value: unknown = event

      for (const part of parts) {
        if (value && typeof value === 'object' && part in value) {
          value = (value as Record<string, unknown>)[part]
        } else {
          return ''
        }
      }

      return String(value)
    })

    try {
      return JSON.parse(result)
    } catch {
      return result
    }
  }

  /**
   * Get color for event type (Slack format)
   */
  private getEventColor(eventType: string): string {
    if (eventType.includes('failed') || eventType.includes('error')) return 'danger'
    if (eventType.includes('warning') || eventType.includes('exceeded')) return 'warning'
    if (eventType.includes('completed') || eventType.includes('passed')) return 'good'
    return '#0066cc'
  }

  /**
   * Get color for event type (Discord format - decimal)
   */
  private getEventColorHex(eventType: string): number {
    if (eventType.includes('failed') || eventType.includes('error')) return 0xff0000
    if (eventType.includes('warning') || eventType.includes('exceeded')) return 0xffcc00
    if (eventType.includes('completed') || eventType.includes('passed')) return 0x00ff00
    return 0x0066cc
  }

  /**
   * Get human-readable title for event
   */
  private getEventTitle(eventType: string): string {
    return eventType
      .split('.')
      .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
      .join(' ')
  }

  /**
   * Format field title
   */
  private formatFieldTitle(key: string): string {
    return key
      .replace(/_/g, ' ')
      .replace(/([A-Z])/g, ' $1')
      .replace(/^./, (s) => s.toUpperCase())
      .trim()
  }

  // ===========================================================================
  // Delivery Processing
  // ===========================================================================

  /**
   * Process delivery queue
   */
  private async processQueue(): Promise<void> {
    if (this.isProcessing) return
    this.isProcessing = true

    while (this.deliveryQueue.length > 0) {
      const delivery = this.deliveryQueue.shift()
      if (!delivery) continue

      await this.sendDelivery(delivery)
    }

    this.isProcessing = false
  }

  /**
   * Send a delivery
   */
  private async sendDelivery(delivery: WebhookDelivery): Promise<void> {
    const webhook = this.webhooks.get(delivery.webhookId)
    if (!webhook) {
      delivery.status = 'failed'
      delivery.error = 'Webhook not found'
      return
    }

    delivery.status = 'sending'
    delivery.attempts++
    delivery.lastAttemptAt = new Date().toISOString()

    try {
      const headers: Record<string, string> = {
        'Content-Type':
          webhook.format === 'form' ? 'application/x-www-form-urlencoded' : 'application/json',
        'User-Agent': 'Delta9-Webhook/1.0',
        ...webhook.headers,
      }

      // Add signature if secret is configured
      if (webhook.secret) {
        const payload =
          typeof delivery.payload === 'string' ? delivery.payload : JSON.stringify(delivery.payload)
        const signature = createHmac('sha256', webhook.secret).update(payload).digest('hex')
        headers['X-Delta9-Signature'] = `sha256=${signature}`
      }

      const body =
        webhook.format === 'form' ? (delivery.payload as string) : JSON.stringify(delivery.payload)

      // Use native fetch
      const controller = new AbortController()
      const timeoutId = setTimeout(() => controller.abort(), webhook.timeout)

      const response = await fetch(webhook.url, {
        method: webhook.method,
        headers,
        body,
        signal: controller.signal,
      })

      clearTimeout(timeoutId)

      delivery.response = {
        statusCode: response.status,
        body: await response.text().catch(() => undefined),
      }

      if (response.ok) {
        delivery.status = 'delivered'
        delivery.deliveredAt = new Date().toISOString()
      } else {
        throw new Error(`HTTP ${response.status}`)
      }
    } catch (error) {
      delivery.error = error instanceof Error ? error.message : String(error)

      // Retry if configured
      if (delivery.attempts < (webhook.retry?.maxRetries || 3)) {
        delivery.status = 'retrying'

        // Add back to queue with backoff
        const backoff = (webhook.retry?.backoffMs || 1000) * delivery.attempts
        setTimeout(() => {
          this.deliveryQueue.push(delivery)
          this.processQueue()
        }, backoff)
      } else {
        delivery.status = 'failed'
      }
    }
  }

  /**
   * Retry a failed delivery
   */
  async retryDelivery(deliveryId: string): Promise<boolean> {
    const delivery = this.deliveries.get(deliveryId)
    if (!delivery || delivery.status !== 'failed') return false

    delivery.status = 'pending'
    delivery.attempts = 0
    delivery.error = undefined

    this.deliveryQueue.push(delivery)
    this.processQueue()

    return true
  }

  // ===========================================================================
  // Getters
  // ===========================================================================

  /**
   * Get delivery by ID
   */
  getDelivery(id: string): WebhookDelivery | undefined {
    return this.deliveries.get(id)
  }

  /**
   * List deliveries for a webhook
   */
  listDeliveries(webhookId?: string, limit: number = 50): WebhookDelivery[] {
    let deliveries = Array.from(this.deliveries.values())

    if (webhookId) {
      deliveries = deliveries.filter((d) => d.webhookId === webhookId)
    }

    return deliveries
      .sort((a, b) => new Date(b.createdAt).getTime() - new Date(a.createdAt).getTime())
      .slice(0, limit)
  }

  /**
   * Get delivery statistics
   */
  getStats(webhookId?: string): {
    total: number
    delivered: number
    failed: number
    pending: number
    successRate: number
  } {
    let deliveries = Array.from(this.deliveries.values())

    if (webhookId) {
      deliveries = deliveries.filter((d) => d.webhookId === webhookId)
    }

    const total = deliveries.length
    const delivered = deliveries.filter((d) => d.status === 'delivered').length
    const failed = deliveries.filter((d) => d.status === 'failed').length
    const pending = deliveries.filter((d) =>
      ['pending', 'sending', 'retrying'].includes(d.status)
    ).length

    return {
      total,
      delivered,
      failed,
      pending,
      successRate: total > 0 ? delivered / total : 1,
    }
  }

  /**
   * Clear old deliveries
   */
  clearOldDeliveries(olderThanMs: number = 7 * 24 * 60 * 60 * 1000): number {
    const cutoff = Date.now() - olderThanMs
    let cleared = 0

    for (const [id, delivery] of this.deliveries) {
      if (new Date(delivery.createdAt).getTime() < cutoff) {
        this.deliveries.delete(id)
        cleared++
      }
    }

    return cleared
  }
}
