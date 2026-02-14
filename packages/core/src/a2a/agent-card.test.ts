/**
 * Agent Card Tests
 */

import { describe, expect, it } from 'vitest'
import { createAgentCard } from './agent-card.js'
import { A2A_PROTOCOL_VERSION, DEFAULT_A2A_PORT, DEFAULT_AGENT_VERSION } from './types.js'

describe('agent-card', () => {
  describe('createAgentCard', () => {
    it('should create card with defaults', () => {
      const card = createAgentCard()

      expect(card.name).toBe('AVA')
      expect(card.protocolVersion).toBe(A2A_PROTOCOL_VERSION)
      expect(card.version).toBe(DEFAULT_AGENT_VERSION)
      expect(card.url).toBe(`http://localhost:${DEFAULT_A2A_PORT}/`)
    })

    it('should use custom port', () => {
      const card = createAgentCard({ port: 9999 })
      expect(card.url).toBe('http://localhost:9999/')
    })

    it('should use custom host', () => {
      const card = createAgentCard({ host: '0.0.0.0', port: 8080 })
      expect(card.url).toBe('http://0.0.0.0:8080/')
    })

    it('should use custom version', () => {
      const card = createAgentCard({ agentVersion: '2.0.0' })
      expect(card.version).toBe('2.0.0')
    })

    it('should include capabilities', () => {
      const card = createAgentCard()

      expect(card.capabilities).toEqual({
        streaming: true,
        pushNotifications: false,
        stateTransitionHistory: true,
      })
    })

    it('should include skills', () => {
      const card = createAgentCard()

      expect(card.skills.length).toBeGreaterThan(0)
      expect(card.skills[0]!.id).toBe('code-editing')

      // All skills should have required fields
      for (const skill of card.skills) {
        expect(skill.id).toBeTruthy()
        expect(skill.name).toBeTruthy()
        expect(skill.description).toBeTruthy()
      }
    })

    it('should include authentication when authToken is set', () => {
      const card = createAgentCard({ authToken: 'secret-token' })

      expect(card.authentication).toBeDefined()
      expect(card.authentication!.schemes).toHaveLength(1)
      expect(card.authentication!.schemes[0]!.scheme).toBe('bearer')
    })

    it('should omit authentication when no authToken', () => {
      const card = createAgentCard()
      expect(card.authentication).toBeUndefined()
    })

    it('should include provider info', () => {
      const card = createAgentCard()

      expect(card.provider).toBeDefined()
      expect(card.provider!.organization).toBe('AVA')
    })

    it('should include input/output modes', () => {
      const card = createAgentCard()

      expect(card.defaultInputModes).toContain('text')
      expect(card.defaultOutputModes).toContain('text')
    })

    it('should include description', () => {
      const card = createAgentCard()
      expect(card.description).toContain('coding assistant')
    })
  })
})
