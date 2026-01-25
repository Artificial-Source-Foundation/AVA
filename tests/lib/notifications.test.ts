/**
 * Tests for Delta9 Notification System
 */

import { describe, it, expect, beforeEach, vi } from 'vitest'
import {
  getNotificationStore,
  notify,
  notifyInfo,
  notifySuccess,
  notifyWarning,
  notifyError,
  notifyProgress,
  taskNotifications,
  missionNotifications,
  councilNotifications,
} from '../../src/lib/notifications.js'

describe('NotificationStore', () => {
  beforeEach(() => {
    const store = getNotificationStore()
    store.clear()
    store.configure({ enabled: true, emitEvents: false }) // Disable events for testing
  })

  describe('Basic Notifications', () => {
    it('should create info notification', () => {
      const notif = notifyInfo('Test title', 'Test message')

      expect(notif.id).toBeDefined()
      expect(notif.type).toBe('info')
      expect(notif.title).toBe('Test title')
      expect(notif.message).toBe('Test message')
      expect(notif.timestamp).toBeDefined()
    })

    it('should create success notification', () => {
      const notif = notifySuccess('Success!', 'Operation completed')

      expect(notif.type).toBe('success')
      expect(notif.title).toBe('Success!')
    })

    it('should create warning notification', () => {
      const notif = notifyWarning('Warning!', 'Something needs attention')

      expect(notif.type).toBe('warning')
    })

    it('should create error notification with persistence', () => {
      const notif = notifyError('Error!', 'Something went wrong')

      expect(notif.type).toBe('error')
      expect(notif.duration).toBe(0) // Errors persist
    })

    it('should create progress notification', () => {
      const notif = notifyProgress('Loading...', 50, { taskId: 'task-1' })

      expect(notif.type).toBe('progress')
      expect(notif.progress).toBe(50)
      expect(notif.taskId).toBe('task-1')
      expect(notif.duration).toBe(0) // Progress notifications persist
    })
  })

  describe('Notification Store', () => {
    it('should store notifications', () => {
      const store = getNotificationStore()

      notifyInfo('First')
      notifyInfo('Second')
      notifyInfo('Third')

      const all = store.getAll()
      expect(all.length).toBe(3)
    })

    it('should get recent notifications', () => {
      const store = getNotificationStore()

      for (let i = 0; i < 10; i++) {
        notifyInfo(`Notification ${i}`)
      }

      const recent = store.getRecent(3)
      expect(recent.length).toBe(3)
      expect(recent[0].title).toBe('Notification 9') // Most recent first
    })

    it('should dismiss notification', () => {
      const store = getNotificationStore()

      const notif = notifyInfo('Dismissable')
      expect(store.getAll().length).toBe(1)

      const dismissed = store.dismiss(notif.id)
      expect(dismissed).toBe(true)
      expect(store.getAll().length).toBe(0)
    })

    it('should return false for non-existent dismiss', () => {
      const store = getNotificationStore()
      const dismissed = store.dismiss('non-existent')
      expect(dismissed).toBe(false)
    })

    it('should clear all notifications', () => {
      const store = getNotificationStore()

      notifyInfo('First')
      notifyInfo('Second')
      expect(store.getAll().length).toBe(2)

      store.clear()
      expect(store.getAll().length).toBe(0)
    })

    it('should respect max notifications limit', () => {
      const store = getNotificationStore()
      store.configure({ maxNotifications: 5, emitEvents: false })

      for (let i = 0; i < 10; i++) {
        notifyInfo(`Notification ${i}`)
      }

      expect(store.getAll().length).toBe(5)
      expect(store.getAll()[0].title).toBe('Notification 5') // Oldest kept
    })
  })

  describe('Progress Updates', () => {
    it('should update progress notification', () => {
      const store = getNotificationStore()

      const notif = notifyProgress('Loading...', 25)
      expect(notif.progress).toBe(25)

      const updated = store.updateProgress(notif.id, 75, 'Almost done...')
      expect(updated).not.toBeNull()
      expect(updated?.progress).toBe(75)
      expect(updated?.message).toBe('Almost done...')
    })

    it('should return null for non-existent progress update', () => {
      const store = getNotificationStore()
      const updated = store.updateProgress('non-existent', 50)
      expect(updated).toBeNull()
    })
  })

  describe('Subscriptions', () => {
    it('should notify subscribers', () => {
      const store = getNotificationStore()
      const listener = vi.fn()

      store.subscribe(listener)
      notifyInfo('Test')

      expect(listener).toHaveBeenCalledTimes(1)
      expect(listener).toHaveBeenCalledWith(expect.objectContaining({ title: 'Test' }))
    })

    it('should allow unsubscribe', () => {
      const store = getNotificationStore()
      const listener = vi.fn()

      const unsubscribe = store.subscribe(listener)
      notifyInfo('First')
      expect(listener).toHaveBeenCalledTimes(1)

      unsubscribe()
      notifyInfo('Second')
      expect(listener).toHaveBeenCalledTimes(1) // Still 1, not called again
    })
  })

  describe('Disabled Mode', () => {
    it('should return dummy notification when disabled', () => {
      const store = getNotificationStore()
      store.configure({ enabled: false, emitEvents: false })

      const notif = notifyInfo('Disabled')
      expect(notif.id).toBe('disabled')

      expect(store.getAll().length).toBe(0)
    })
  })

  describe('Notify with Options', () => {
    it('should accept full options', () => {
      const notif = notify('info', 'Full options', {
        message: 'Detailed message',
        taskId: 'task-123',
        agent: 'operator',
        duration: 10000,
        actions: [{ label: 'View', action: 'view' }],
      })

      expect(notif.message).toBe('Detailed message')
      expect(notif.taskId).toBe('task-123')
      expect(notif.agent).toBe('operator')
      expect(notif.duration).toBe(10000)
      expect(notif.actions).toHaveLength(1)
    })
  })
})

describe('Task Notifications', () => {
  beforeEach(() => {
    const store = getNotificationStore()
    store.clear()
    store.configure({ enabled: true, emitEvents: false })
  })

  it('should create task started notification', () => {
    const notif = taskNotifications.started('task-1', 'operator', 'Build feature')

    expect(notif.type).toBe('info')
    expect(notif.title).toContain('Started')
    expect(notif.taskId).toBe('task-1')
    expect(notif.agent).toBe('operator')
  })

  it('should create task progress notification', () => {
    const notif = taskNotifications.progress('task-1', 'operator', 50, 'Processing files')

    expect(notif.type).toBe('progress')
    expect(notif.progress).toBe(50)
  })

  it('should create task completed notification', () => {
    const notif = taskNotifications.completed('task-1', 'operator', 'Build feature')

    expect(notif.type).toBe('success')
    expect(notif.title).toContain('Completed')
  })

  it('should create task failed notification', () => {
    const notif = taskNotifications.failed('task-1', 'operator', 'Build feature', 'Syntax error')

    expect(notif.type).toBe('error')
    expect(notif.title).toContain('Failed')
    expect(notif.message).toBe('Syntax error')
  })

  it('should create task cancelled notification', () => {
    const notif = taskNotifications.cancelled('task-1', 'Build feature')

    expect(notif.type).toBe('warning')
    expect(notif.title).toContain('Cancelled')
  })
})

describe('Mission Notifications', () => {
  beforeEach(() => {
    const store = getNotificationStore()
    store.clear()
    store.configure({ enabled: true, emitEvents: false })
  })

  it('should create mission started notification', () => {
    const notif = missionNotifications.started('mission-1', 'Build new feature')

    expect(notif.type).toBe('info')
    expect(notif.title).toContain('Mission Started')
    expect(notif.message).toBe('Build new feature')
  })

  it('should create phase change notification', () => {
    const notif = missionNotifications.phaseChange('mission-1', 'planning')

    expect(notif.type).toBe('info')
    expect(notif.title).toContain('Phase')
    expect(notif.title).toContain('planning')
  })

  it('should create mission completed notification', () => {
    const notif = missionNotifications.completed('mission-1')

    expect(notif.type).toBe('success')
    expect(notif.title).toContain('Completed')
  })

  it('should create mission failed notification', () => {
    const notif = missionNotifications.failed('mission-1', 'Build failed')

    expect(notif.type).toBe('error')
    expect(notif.title).toContain('Failed')
    expect(notif.message).toBe('Build failed')
  })
})

describe('Council Notifications', () => {
  beforeEach(() => {
    const store = getNotificationStore()
    store.clear()
    store.configure({ enabled: true, emitEvents: false })
  })

  it('should create council convened notification', () => {
    const notif = councilNotifications.convened('task-1', 'STANDARD', 4)

    expect(notif.type).toBe('info')
    expect(notif.title).toContain('Convened')
    expect(notif.message).toContain('4 oracles')
  })

  it('should create council consensus notification', () => {
    const notif = councilNotifications.consensus('task-1', 0.85)

    expect(notif.type).toBe('success')
    expect(notif.title).toContain('Consensus')
    expect(notif.message).toContain('85%')
  })

  it('should create council conflict notification', () => {
    const notif = councilNotifications.conflict('task-1')

    expect(notif.type).toBe('warning')
    expect(notif.title).toContain('Conflict')
  })
})
