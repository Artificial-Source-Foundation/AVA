/**
 * ACP Session Store Tests
 */

import { beforeEach, describe, expect, it } from 'vitest'
import { SessionManager } from '../session/manager.js'
import { AcpSessionStore, createAcpSessionStore } from './session-store.js'

// ============================================================================
// Helpers
// ============================================================================

function createMockSessionManager(): SessionManager {
  return new SessionManager({ maxSessions: 10 })
}

// ============================================================================
// Tests
// ============================================================================

describe('AcpSessionStore', () => {
  let store: AcpSessionStore
  let manager: SessionManager

  beforeEach(() => {
    manager = createMockSessionManager()
    store = new AcpSessionStore(manager)
  })

  describe('create', () => {
    it('should create a new session', async () => {
      const session = await store.create('acp-1', '/home/user/project')

      expect(session.id).toBeTruthy()
      expect(session.workingDirectory).toBe('/home/user/project')
      expect(session.name).toContain('ACP: ')
      expect(session.status).toBe('active')
    })

    it('should map ACP session ID to Estela session', async () => {
      const session = await store.create('acp-2', '/tmp')

      const estelaId = store.getEstelaId('acp-2')
      expect(estelaId).toBe(session.id)
    })

    it('should track session info', async () => {
      await store.create('acp-3', '/home')

      const info = store.getInfo('acp-3')
      expect(info).not.toBeNull()
      expect(info!.sessionId).toBe('acp-3')
      expect(info!.workingDirectory).toBe('/home')
      expect(info!.mode).toBe('agent')
    })
  })

  describe('get', () => {
    it('should return session by ACP ID', async () => {
      const created = await store.create('acp-get-1', '/tmp')
      const retrieved = await store.get('acp-get-1')

      expect(retrieved).not.toBeNull()
      expect(retrieved!.id).toBe(created.id)
    })

    it('should return null for unknown ACP ID', async () => {
      const result = await store.get('nonexistent')
      expect(result).toBeNull()
    })
  })

  describe('load', () => {
    it('should load a session by Estela ID', async () => {
      const created = await store.create('acp-load-1', '/tmp')
      const loaded = await store.load(created.id)

      expect(loaded).not.toBeNull()
      expect(loaded!.id).toBe(created.id)
    })

    it('should return null for unknown Estela ID', async () => {
      const result = await store.load('nonexistent')
      expect(result).toBeNull()
    })
  })

  describe('save', () => {
    it('should save a session without error', async () => {
      await store.create('acp-save-1', '/tmp')
      await expect(store.save('acp-save-1')).resolves.toBeUndefined()
    })

    it('should handle save for unknown session', async () => {
      await expect(store.save('nonexistent')).resolves.toBeUndefined()
    })
  })

  describe('saveAll', () => {
    it('should save all sessions', async () => {
      await store.create('acp-all-1', '/a')
      await store.create('acp-all-2', '/b')

      await expect(store.saveAll()).resolves.toBeUndefined()
    })
  })

  describe('delete', () => {
    it('should delete a session', async () => {
      await store.create('acp-del-1', '/tmp')
      expect(store.has('acp-del-1')).toBe(true)

      await store.delete('acp-del-1')
      expect(store.has('acp-del-1')).toBe(false)
    })

    it('should handle delete for unknown session', async () => {
      await expect(store.delete('nonexistent')).resolves.toBeUndefined()
    })
  })

  describe('list', () => {
    it('should list all sessions', async () => {
      await store.create('list-1', '/a')
      await store.create('list-2', '/b')

      const sessions = await store.list()
      expect(sessions.length).toBeGreaterThanOrEqual(2)
    })
  })

  describe('has', () => {
    it('should return true for existing session', async () => {
      await store.create('has-1', '/tmp')
      expect(store.has('has-1')).toBe(true)
    })

    it('should return false for missing session', () => {
      expect(store.has('nope')).toBe(false)
    })
  })

  describe('mode', () => {
    it('should default to agent mode', async () => {
      await store.create('mode-1', '/tmp')
      expect(store.getMode('mode-1')).toBe('agent')
    })

    it('should set and get mode', async () => {
      await store.create('mode-2', '/tmp')

      store.setMode('mode-2', 'plan')
      expect(store.getMode('mode-2')).toBe('plan')

      store.setMode('mode-2', 'agent')
      expect(store.getMode('mode-2')).toBe('agent')
    })

    it('should return null for unknown session mode', () => {
      expect(store.getMode('unknown')).toBeNull()
    })
  })

  describe('dispose', () => {
    it('should dispose without error', async () => {
      await store.create('dispose-1', '/tmp')
      await expect(store.dispose()).resolves.toBeUndefined()
    })

    it('should reject operations after dispose', async () => {
      await store.dispose()
      await expect(store.create('post-dispose', '/tmp')).rejects.toThrow('disposed')
    })

    it('should handle double dispose', async () => {
      await store.dispose()
      await expect(store.dispose()).resolves.toBeUndefined()
    })
  })

  describe('factory', () => {
    it('should create store with factory function', () => {
      const factoryStore = createAcpSessionStore(manager)
      expect(factoryStore).toBeInstanceOf(AcpSessionStore)
    })
  })
})
