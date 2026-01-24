/**
 * Tests for Delta9 Task Router
 *
 * Tests routing logic for dispatching tasks to appropriate agents
 * based on keywords, complexity, and context.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest'
import {
  routeTask,
  canAgentModifyFiles,
  isSupportAgent,
  getAvailableAgents,
  describeRouteDecision,
  type RoutableAgent,
  type RouteDecision,
  type TaskRouterInput,
} from '../../src/routing/task-router.js'

// Mock complexity module
vi.mock('../../src/routing/complexity.js', () => ({
  analyzeComplexity: vi.fn((description: string) => {
    if (description.includes('refactor') || description.includes('architecture')) {
      return { complexity: 'high', score: 80, factors: [] }
    }
    if (description.includes('typo') || description.includes('simple')) {
      return { complexity: 'low', score: 20, factors: [] }
    }
    return { complexity: 'medium', score: 50, factors: [] }
  }),
}))

describe('Task Router', () => {
  beforeEach(() => {
    vi.clearAllMocks()
  })

  // =========================================================================
  // Keyword-Based Routing
  // =========================================================================

  describe('Keyword-Based Routing', () => {
    describe('Frontend/UI keywords → ui-ops (FACADE)', () => {
      const uiKeywords = [
        'create a React component',
        'add a button to the UI',
        'fix the CSS styling',
        'implement responsive design',
        'add accessibility attributes',
        'create a modal dialog',
        'Tailwind classes for the form',
      ]

      it.each(uiKeywords)('routes "%s" to ui-ops', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('ui-ops')
      })
    })

    describe('Testing keywords → qa (SENTINEL)', () => {
      const testKeywords = [
        'write unit tests for auth',
        'add test coverage',
        'create Jest specs',
        'Vitest test file',
        'Playwright e2e test',
        'add mock for API',
      ]

      it.each(testKeywords)('routes "%s" to qa', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('qa')
      })
    })

    describe('Documentation keywords → scribe (SCRIBE)', () => {
      const docKeywords = [
        'update the README',
        'add JSDoc comments',
        'write API documentation',
        'create a changelog entry',
        'add inline documentation',
      ]

      it.each(docKeywords)('routes "%s" to scribe', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('scribe')
      })
    })

    describe('Search keywords → scout (RECON)', () => {
      const searchKeywords = [
        'find all usages of AuthService',
        'search for config files',
        'grep for error handling',
        'locate the middleware',
        'which file has the router',
      ]

      it.each(searchKeywords)('routes "%s" to scout', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('scout')
      })
    })

    describe('Research keywords → intel (SIGINT)', () => {
      const researchKeywords = [
        'research best practices for auth',
        'lookup how to use the library',
        'how to use the npm package',
      ]

      it.each(researchKeywords)('routes "%s" to intel', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('intel')
      })
    })

    describe('Advice keywords → strategist (TACCOM)', () => {
      const adviceKeywords = [
        "I'm stuck on this bug",
        "I'm blocked and need help",
        'give me guidance on approach',
        "I've tried everything",
        'what alternative could work',
      ]

      it.each(adviceKeywords)('routes "%s" to strategist', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('strategist')
      })
    })

    describe('Visual keywords → optics (SPECTRE)', () => {
      const visualKeywords = [
        'analyze this screenshot',
        'look at the diagram',
        'what does this image show',
        'analyze the pdf file',
      ]

      it.each(visualKeywords)('routes "%s" to optics', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('optics')
      })
    })

    describe('Quick fix keywords → patcher (SURGEON)', () => {
      const quickFixKeywords = [
        'fix the typo in the file',
        'simple lint fix',
        'one line change',
        'trivial quick fix',
      ]

      it.each(quickFixKeywords)('routes "%s" to patcher', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('patcher')
      })
    })

    describe('Complex keywords → operator-complex', () => {
      const complexKeywords = [
        'refactor the entire module',
        'rewrite the auth system',
        'migrate to new architecture',
        'comprehensive code review',
      ]

      it.each(complexKeywords)('routes "%s" to operator-complex', (description) => {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe('operator-complex')
      })
    })
  })

  // =========================================================================
  // Complexity-Based Routing
  // =========================================================================

  describe('Complexity-Based Routing', () => {
    it('boosts operator-complex for high complexity tasks', () => {
      const decision = routeTask({
        taskDescription: 'refactor the authentication architecture',
      })

      expect(decision.agent).toBe('operator-complex')
      expect(decision.metadata?.complexity).toBe('high')
    })

    it('boosts patcher for low complexity tasks', () => {
      const decision = routeTask({
        taskDescription: 'fix a simple typo in the code',
      })

      expect(decision.agent).toBe('patcher')
      expect(decision.metadata?.complexity).toBe('low')
    })
  })

  // =========================================================================
  // Context-Based Routing
  // =========================================================================

  describe('Context-Based Routing', () => {
    it('boosts strategist when previous failures exist', () => {
      const decision = routeTask({
        taskDescription: 'implement the feature',
        context: { previousFailures: 3 },
      })

      expect(decision.agent).toBe('strategist')
    })

    it('prefers cheaper agents when budget constrained', () => {
      const decision = routeTask({
        taskDescription: 'implement something general',
        context: { budgetConstrained: true },
      })

      // Should prefer scout or patcher (cheaper)
      expect(['scout', 'patcher']).toContain(decision.agent)
    })
  })

  // =========================================================================
  // Default Routing
  // =========================================================================

  describe('Default Routing', () => {
    it('defaults to operator for general tasks', () => {
      const decision = routeTask({
        taskDescription: 'implement user profile page',
      })

      expect(decision.agent).toBe('operator')
    })

    it('has low confidence for default routing', () => {
      const decision = routeTask({
        taskDescription: 'do something vague',
      })

      expect(decision.confidence).toBeLessThan(0.5)
    })
  })

  // =========================================================================
  // Route Decision Structure
  // =========================================================================

  describe('Route Decision Structure', () => {
    it('includes all required fields', () => {
      const decision = routeTask({
        taskDescription: 'create a React component',
      })

      expect(decision.agent).toBeDefined()
      expect(decision.model).toBeDefined()
      expect(decision.reason).toBeDefined()
      expect(decision.confidence).toBeGreaterThanOrEqual(0)
      expect(decision.confidence).toBeLessThanOrEqual(1)
    })

    it('includes fallback agent', () => {
      const decision = routeTask({
        taskDescription: 'create a React component',
      })

      expect(decision.fallbackAgent).toBeDefined()
    })

    it('includes metadata with matched keywords', () => {
      const decision = routeTask({
        taskDescription: 'create a React button component',
      })

      expect(decision.metadata).toBeDefined()
      expect(decision.metadata?.matchedKeywords).toBeDefined()
      expect(decision.metadata?.matchedKeywords?.length).toBeGreaterThan(0)
    })

    it('includes agent capabilities in metadata', () => {
      const decision = routeTask({
        taskDescription: 'write unit tests',
      })

      expect(decision.metadata?.capabilities).toBeDefined()
      expect(decision.metadata?.capabilities).toContain('testing')
    })
  })

  // =========================================================================
  // Helper Functions
  // =========================================================================

  describe('canAgentModifyFiles', () => {
    const modifyAgents: RoutableAgent[] = [
      'operator', 'operator-complex', 'patcher', 'ui-ops', 'scribe', 'qa',
    ]

    const readOnlyAgents: RoutableAgent[] = [
      'scout', 'intel', 'strategist', 'optics',
    ]

    it.each(modifyAgents)('%s can modify files', (agent) => {
      expect(canAgentModifyFiles(agent)).toBe(true)
    })

    it.each(readOnlyAgents)('%s cannot modify files', (agent) => {
      expect(canAgentModifyFiles(agent)).toBe(false)
    })
  })

  describe('isSupportAgent', () => {
    const supportAgents: RoutableAgent[] = [
      'scout', 'intel', 'strategist', 'optics',
    ]

    const executionAgents: RoutableAgent[] = [
      'operator', 'operator-complex', 'patcher', 'ui-ops', 'scribe', 'qa',
    ]

    it.each(supportAgents)('%s is a support agent', (agent) => {
      expect(isSupportAgent(agent)).toBe(true)
    })

    it.each(executionAgents)('%s is not a support agent', (agent) => {
      expect(isSupportAgent(agent)).toBe(false)
    })
  })

  describe('getAvailableAgents', () => {
    it('returns all 10 agent types', () => {
      const agents = getAvailableAgents()

      expect(agents).toHaveLength(10)
      expect(agents).toContain('operator')
      expect(agents).toContain('scout')
      expect(agents).toContain('ui-ops')
      expect(agents).toContain('qa')
    })
  })

  describe('describeRouteDecision', () => {
    it('formats decision as human-readable string', () => {
      const decision: RouteDecision = {
        agent: 'ui-ops',
        model: 'google/gemini-2.0-flash',
        reason: 'Frontend/UI task detected',
        confidence: 0.85,
        fallbackAgent: 'operator',
        metadata: {
          matchedKeywords: ['react', 'component'],
          complexity: 'medium',
          capabilities: ['frontend', 'components'],
        },
      }

      const description = describeRouteDecision(decision)

      expect(description).toContain('UI-OPS')
      expect(description).toContain('google/gemini-2.0-flash')
      expect(description).toContain('85%')
      expect(description).toContain('Frontend')
      expect(description).toContain('react')
    })
  })

  // =========================================================================
  // Codename Mapping (Delta Team)
  // =========================================================================

  describe('Delta Team Codename Mapping', () => {
    const codenameMap: Record<RoutableAgent, string> = {
      'scout': 'RECON',
      'intel': 'SIGINT',
      'strategist': 'TACCOM',
      'patcher': 'SURGEON',
      'qa': 'SENTINEL',
      'scribe': 'SCRIBE',
      'ui-ops': 'FACADE',
      'optics': 'SPECTRE',
      'operator': 'OPERATOR',
      'operator-complex': 'OPERATOR-COMPLEX',
    }

    it('routes to agents that map to Delta codenames', () => {
      // This test documents the mapping between router agents and Delta codenames
      const agents = getAvailableAgents()

      for (const agent of agents) {
        expect(codenameMap[agent]).toBeDefined()
      }
    })
  })
})
