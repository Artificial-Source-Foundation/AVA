/**
 * Tests for Delta9 Task Router
 *
 * Consolidated tests using representative sampling.
 * Tests keyword routing, complexity routing, and helper functions.
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

  describe('Keyword-Based Routing', () => {
    // Test 2 representative keywords per agent type
    const keywordRoutes: [string, RoutableAgent][] = [
      ['create a React component', 'ui-ops'],
      ['fix the CSS styling', 'ui-ops'],
      ['write unit tests for auth', 'qa'],
      ['create Jest specs', 'qa'],
      ['update the README', 'scribe'],
      ['add JSDoc comments', 'scribe'],
      ['find all usages of AuthService', 'scout'],
      ['search for config files', 'scout'],
      ['research best practices for auth', 'intel'],
      ['how to use the npm package', 'intel'],
      ["I'm stuck on this bug", 'strategist'],
      ['give me guidance on approach', 'strategist'],
      ['analyze this screenshot', 'optics'],
      ['look at the diagram', 'optics'],
      ['fix the typo in the file', 'patcher'],
      ['simple lint fix', 'patcher'],
      ['refactor the entire module', 'operator-complex'],
      ['migrate to new architecture', 'operator-complex'],
    ]

    it('routes tasks to correct agents based on keywords', () => {
      for (const [description, expectedAgent] of keywordRoutes) {
        const decision = routeTask({ taskDescription: description })
        expect(decision.agent).toBe(expectedAgent)
      }
    })
  })

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
      expect(['scout', 'patcher']).toContain(decision.agent)
    })
  })

  describe('Default Routing', () => {
    it('defaults to operator for general tasks with low confidence', () => {
      const decision = routeTask({
        taskDescription: 'do something vague',
      })
      expect(decision.agent).toBe('operator')
      expect(decision.confidence).toBeLessThan(0.5)
    })
  })

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
      expect(decision.fallbackAgent).toBeDefined()
      expect(decision.metadata?.matchedKeywords).toBeDefined()
    })
  })

  describe('Helper Functions', () => {
    it('canAgentModifyFiles correctly identifies file-modifying agents', () => {
      const modifyAgents: RoutableAgent[] = ['operator', 'operator-complex', 'patcher', 'ui-ops', 'scribe', 'qa']
      const readOnlyAgents: RoutableAgent[] = ['scout', 'intel', 'strategist', 'optics']

      for (const agent of modifyAgents) {
        expect(canAgentModifyFiles(agent)).toBe(true)
      }
      for (const agent of readOnlyAgents) {
        expect(canAgentModifyFiles(agent)).toBe(false)
      }
    })

    it('isSupportAgent correctly identifies support agents', () => {
      const supportAgents: RoutableAgent[] = ['scout', 'intel', 'strategist', 'optics']
      const executionAgents: RoutableAgent[] = ['operator', 'operator-complex', 'patcher', 'ui-ops', 'scribe', 'qa']

      for (const agent of supportAgents) {
        expect(isSupportAgent(agent)).toBe(true)
      }
      for (const agent of executionAgents) {
        expect(isSupportAgent(agent)).toBe(false)
      }
    })

    it('getAvailableAgents returns all 10 agent types', () => {
      const agents = getAvailableAgents()
      expect(agents).toHaveLength(10)
      expect(agents).toContain('operator')
      expect(agents).toContain('scout')
      expect(agents).toContain('ui-ops')
    })

    it('describeRouteDecision formats as readable string', () => {
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
      expect(description).toContain('85%')
    })
  })
})
