/**
 * Delta9 Notification System
 *
 * Toast-style notifications for task progress and events.
 * Integrates with the event system for persistence and replay.
 *
 * Notification Types:
 * - info: General information
 * - success: Task completed successfully
 * - warning: Something requires attention
 * - error: Something went wrong
 * - progress: Task progress update
 */

import { getEventStore } from '../events/store.js'
import { getNamedLogger } from './logger.js'

// Logger for notifications (silent in TUI mode)
const log = getNamedLogger('notifications')

// =============================================================================
// Types
// =============================================================================

export type NotificationType = 'info' | 'success' | 'warning' | 'error' | 'progress'

export interface Notification {
  /** Unique notification ID */
  id: string
  /** Notification type */
  type: NotificationType
  /** Short title */
  title: string
  /** Longer description */
  message?: string
  /** Related task/session ID */
  taskId?: string
  /** Agent name */
  agent?: string
  /** Progress percentage (0-100) for progress type */
  progress?: number
  /** When notification was created */
  timestamp: string
  /** Auto-dismiss timeout in ms (0 = persistent) */
  duration?: number
  /** Action buttons */
  actions?: Array<{
    label: string
    action: string // Action identifier
  }>
}

export interface NotificationConfig {
  /** Enable notifications */
  enabled: boolean
  /** Default duration for auto-dismiss (ms) */
  defaultDuration: number
  /** Max notifications to keep in memory */
  maxNotifications: number
  /** Emit to event store */
  emitEvents: boolean
}

// =============================================================================
// Notification Store
// =============================================================================

class NotificationStore {
  private notifications: Notification[] = []
  private listeners: Set<(notification: Notification) => void> = new Set()
  private config: NotificationConfig = {
    enabled: true,
    defaultDuration: 5000, // 5 seconds
    maxNotifications: 50,
    emitEvents: true,
  }
  private counter = 0

  /**
   * Configure the notification store
   */
  configure(config: Partial<NotificationConfig>): void {
    this.config = { ...this.config, ...config }
  }

  /**
   * Create a notification
   */
  notify(
    type: NotificationType,
    title: string,
    options?: {
      message?: string
      taskId?: string
      agent?: string
      progress?: number
      duration?: number
      actions?: Notification['actions']
    }
  ): Notification {
    if (!this.config.enabled) {
      // Return a dummy notification if disabled
      return {
        id: 'disabled',
        type,
        title,
        timestamp: new Date().toISOString(),
      }
    }

    const notification: Notification = {
      id: `notif_${++this.counter}_${Date.now()}`,
      type,
      title,
      message: options?.message,
      taskId: options?.taskId,
      agent: options?.agent,
      progress: options?.progress,
      timestamp: new Date().toISOString(),
      duration: options?.duration ?? (type === 'progress' ? 0 : this.config.defaultDuration),
      actions: options?.actions,
    }

    // Add to store
    this.notifications.push(notification)

    // Trim to max size
    if (this.notifications.length > this.config.maxNotifications) {
      this.notifications = this.notifications.slice(-this.config.maxNotifications)
    }

    // Emit to event store
    if (this.config.emitEvents) {
      this.emitEvent(notification)
    }

    // Notify listeners
    for (const listener of this.listeners) {
      try {
        listener(notification)
      } catch (error) {
        log.error('[notifications] Listener error', { error: String(error) })
      }
    }

    return notification
  }

  /**
   * Convenience methods for each type
   */
  info(title: string, options?: Omit<Parameters<typeof this.notify>[2], 'never'>): Notification {
    return this.notify('info', title, options)
  }

  success(title: string, options?: Omit<Parameters<typeof this.notify>[2], 'never'>): Notification {
    return this.notify('success', title, options)
  }

  warning(title: string, options?: Omit<Parameters<typeof this.notify>[2], 'never'>): Notification {
    return this.notify('warning', title, options)
  }

  error(title: string, options?: Omit<Parameters<typeof this.notify>[2], 'never'>): Notification {
    return this.notify('error', title, { ...options, duration: 0 }) // Errors persist
  }

  progress(
    title: string,
    progressValue: number,
    options?: Omit<Parameters<typeof this.notify>[2], 'progress'>
  ): Notification {
    return this.notify('progress', title, { ...options, progress: progressValue })
  }

  /**
   * Update a progress notification
   */
  updateProgress(id: string, progressValue: number, message?: string): Notification | null {
    const notification = this.notifications.find((n) => n.id === id)
    if (!notification) return null

    notification.progress = progressValue
    if (message !== undefined) {
      notification.message = message
    }
    notification.timestamp = new Date().toISOString()

    // Notify listeners
    for (const listener of this.listeners) {
      try {
        listener(notification)
      } catch (error) {
        log.error('[notifications] Listener error', { error: String(error) })
      }
    }

    return notification
  }

  /**
   * Dismiss a notification
   */
  dismiss(id: string): boolean {
    const index = this.notifications.findIndex((n) => n.id === id)
    if (index === -1) return false

    this.notifications.splice(index, 1)
    return true
  }

  /**
   * Get all notifications
   */
  getAll(): Notification[] {
    return [...this.notifications]
  }

  /**
   * Get recent notifications
   */
  getRecent(count: number = 10): Notification[] {
    return this.notifications.slice(-count).reverse()
  }

  /**
   * Clear all notifications
   */
  clear(): void {
    this.notifications = []
  }

  /**
   * Subscribe to notifications
   */
  subscribe(listener: (notification: Notification) => void): () => void {
    this.listeners.add(listener)
    return () => this.listeners.delete(listener)
  }

  /**
   * Emit notification to event store
   */
  private emitEvent(notification: Notification): void {
    try {
      const eventStore = getEventStore()
      eventStore.append('system.notification', {
        notificationId: notification.id,
        type: notification.type,
        title: notification.title,
        message: notification.message,
        taskId: notification.taskId,
        agent: notification.agent,
      })
    } catch {
      // Event store may not be initialized yet
    }
  }
}

// =============================================================================
// Singleton & Exports
// =============================================================================

const store = new NotificationStore()

/**
 * Get the notification store
 */
export function getNotificationStore(): NotificationStore {
  return store
}

/**
 * Send a notification
 */
export function notify(
  type: NotificationType,
  title: string,
  options?: {
    message?: string
    taskId?: string
    agent?: string
    progress?: number
    duration?: number
    actions?: Notification['actions']
  }
): Notification {
  return store.notify(type, title, options)
}

/**
 * Send info notification
 */
export function notifyInfo(title: string, message?: string): Notification {
  return store.info(title, { message })
}

/**
 * Send success notification
 */
export function notifySuccess(title: string, message?: string): Notification {
  return store.success(title, { message })
}

/**
 * Send warning notification
 */
export function notifyWarning(title: string, message?: string): Notification {
  return store.warning(title, { message })
}

/**
 * Send error notification
 */
export function notifyError(title: string, message?: string): Notification {
  return store.error(title, { message })
}

/**
 * Send progress notification
 */
export function notifyProgress(
  title: string,
  progress: number,
  options?: { message?: string; taskId?: string; agent?: string }
): Notification {
  return store.progress(title, progress, options)
}

/**
 * Task-specific notification helpers
 */
export const taskNotifications = {
  started(taskId: string, agent: string, title: string): Notification {
    return store.info(`Task Started: ${title}`, {
      taskId,
      agent,
      message: `${agent} agent is working on this task`,
    })
  },

  progress(taskId: string, agent: string, progress: number, status: string): Notification {
    return store.progress(`Task Progress: ${status}`, progress, {
      taskId,
      agent,
    })
  },

  completed(taskId: string, agent: string, title: string): Notification {
    return store.success(`Task Completed: ${title}`, {
      taskId,
      agent,
      message: `Successfully completed by ${agent}`,
    })
  },

  failed(taskId: string, agent: string, title: string, error: string): Notification {
    return store.error(`Task Failed: ${title}`, {
      taskId,
      agent,
      message: error,
    })
  },

  cancelled(taskId: string, title: string): Notification {
    return store.warning(`Task Cancelled: ${title}`, {
      taskId,
    })
  },
}

/**
 * Mission-specific notification helpers
 */
export const missionNotifications = {
  started(missionId: string, description: string): Notification {
    return store.info('Mission Started', {
      taskId: missionId,
      message: description,
    })
  },

  phaseChange(missionId: string, phase: string): Notification {
    return store.info(`Phase: ${phase}`, {
      taskId: missionId,
    })
  },

  completed(missionId: string): Notification {
    return store.success('Mission Completed', {
      taskId: missionId,
      message: 'All tasks have been successfully completed',
    })
  },

  failed(missionId: string, error: string): Notification {
    return store.error('Mission Failed', {
      taskId: missionId,
      message: error,
    })
  },
}

/**
 * Council-specific notification helpers
 */
export const councilNotifications = {
  convened(taskId: string, mode: string, oracleCount: number): Notification {
    return store.info(`Council Convened (${mode})`, {
      taskId,
      message: `${oracleCount} oracles deliberating`,
    })
  },

  consensus(taskId: string, confidence: number): Notification {
    return store.success('Council Consensus', {
      taskId,
      message: `Consensus reached with ${Math.round(confidence * 100)}% confidence`,
    })
  },

  conflict(taskId: string): Notification {
    return store.warning('Council Conflict', {
      taskId,
      message: 'Oracles disagree - Commander will resolve',
    })
  },
}

// =============================================================================
// Squadron Notifications
// =============================================================================

/**
 * Squadron-specific notification helpers
 */
export const squadronNotifications = {
  started(squadronAlias: string, waveCount: number, agentCount: number): Notification {
    return store.info(`Squadron "${squadronAlias}" Launched`, {
      message: `${waveCount} waves, ${agentCount} agents`,
    })
  },

  waveStarted(squadronAlias: string, waveNumber: number, agentCount: number): Notification {
    return store.info(`Wave ${waveNumber} Started`, {
      message: `${agentCount} agents executing`,
      agent: squadronAlias,
    })
  },

  waveCompleted(squadronAlias: string, waveNumber: number): Notification {
    return store.success(`Wave ${waveNumber} Complete`, {
      agent: squadronAlias,
    })
  },

  waveFailed(squadronAlias: string, waveNumber: number, reason: string): Notification {
    return store.error(`Wave ${waveNumber} Failed`, {
      message: reason,
      agent: squadronAlias,
    })
  },

  completed(squadronAlias: string): Notification {
    return store.success(`Squadron "${squadronAlias}" Complete`, {
      message: 'All waves completed successfully',
    })
  },

  failed(squadronAlias: string, reason: string): Notification {
    return store.error(`Squadron "${squadronAlias}" Failed`, {
      message: reason,
    })
  },

  cancelled(squadronAlias: string): Notification {
    return store.warning(`Squadron "${squadronAlias}" Cancelled`, {})
  },
}

// =============================================================================
// SDK Toast Integration
// =============================================================================

/**
 * OpenCode SDK client type for toast display
 */
export interface ToastClient {
  tui?: {
    showToast: (opts: {
      body: {
        title: string
        message: string
        variant?: 'info' | 'success' | 'warning' | 'error'
        duration?: number
      }
    }) => Promise<void>
  }
}

/**
 * Show a toast notification via the OpenCode SDK
 *
 * Falls back to internal notification if SDK not available.
 */
export async function showToast(
  client: ToastClient | undefined,
  options: {
    title: string
    message: string
    variant?: 'info' | 'success' | 'warning' | 'error'
    duration?: number
  }
): Promise<void> {
  // Try SDK toast first
  if (client?.tui?.showToast) {
    try {
      await client.tui.showToast({
        body: {
          title: options.title,
          message: options.message,
          variant: options.variant || 'info',
          duration: options.duration || 5000,
        },
      })
      return
    } catch (error) {
      log.debug('[notifications] SDK toast failed, falling back to internal', {
        error: String(error),
      })
    }
  }

  // Fallback to internal notification
  const type: NotificationType =
    options.variant === 'success'
      ? 'success'
      : options.variant === 'warning'
        ? 'warning'
        : options.variant === 'error'
          ? 'error'
          : 'info'

  store.notify(type, options.title, { message: options.message, duration: options.duration })
}

/**
 * Show squadron status toast
 */
export async function showSquadronToast(
  client: ToastClient | undefined,
  options: {
    squadronAlias: string
    title: string
    agents: Array<{ name: string; status: string; duration?: string }>
    variant?: 'info' | 'success' | 'warning' | 'error'
  }
): Promise<void> {
  const message = options.agents
    .map((a) => `${a.name}: ${a.status}${a.duration ? ` (${a.duration})` : ''}`)
    .join('\n')

  await showToast(client, {
    title: options.title,
    message,
    variant: options.variant || 'info',
    duration: 5000,
  })
}
