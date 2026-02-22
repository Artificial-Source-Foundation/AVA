/**
 * Agent Evaluator Tests
 */

import { describe, expect, it } from 'vitest'
import { analyzeToolUsage, calculateMetrics, calculateProgress, evaluateGoal } from './evaluator.js'
import { createMockResult, createMockStep, createMockToolCallInfo } from './test-helpers.js'
import { AgentTerminateMode } from './types.js'

// ============================================================================
// calculateProgress Tests
// ============================================================================

describe('calculateProgress', () => {
  it('returns not_started status for empty steps', () => {
    const report = calculateProgress([])

    expect(report.status).toBe('not_started')
    expect(report.percentComplete).toBe(0)
    expect(report.stepsCompleted).toBe(0)
    expect(report.stepsExecuted).toBe(0)
    expect(report.summary).toBe('Agent has not started execution')
    expect(report.milestones).toEqual([])
    expect(report.blockers).toEqual([])
  })

  it('returns in_progress status for single running step', () => {
    const steps = [createMockStep({ status: 'running' })]
    const report = calculateProgress(steps)

    expect(report.status).toBe('in_progress')
    expect(report.currentStep).toBeDefined()
    expect(report.currentStep?.status).toBe('running')
  })

  it('returns in_progress status for single success step', () => {
    const steps = [createMockStep({ status: 'success' })]
    const report = calculateProgress(steps)

    // With 1 success and no totalExpectedSteps, total = max(1, 1+1) = 2
    // So 1/2 = 50%, not completed
    expect(report.status).toBe('in_progress')
    expect(report.stepsCompleted).toBe(1)
    expect(report.percentComplete).toBe(50)
  })

  it('returns completed status when all steps match expected total', () => {
    const steps = [createMockStep({ status: 'success' }), createMockStep({ status: 'success' })]
    const report = calculateProgress(steps, 2)

    expect(report.status).toBe('completed')
    expect(report.percentComplete).toBe(100)
    expect(report.stepsCompleted).toBe(2)
    expect(report.summary).toContain('All 2 steps successful')
  })

  it('returns failed status when all steps failed', () => {
    const steps = [
      createMockStep({ status: 'failed', error: 'Error 1' }),
      createMockStep({ status: 'failed', error: 'Error 2' }),
    ]
    const report = calculateProgress(steps)

    expect(report.status).toBe('failed')
    expect(report.blockers).toEqual(['Error 1', 'Error 2'])
    expect(report.summary).toContain('2 of 2 steps failed')
  })

  it('returns blocked status for mix of success and failed', () => {
    const steps = [
      createMockStep({ status: 'success' }),
      createMockStep({ status: 'failed', error: 'Some error' }),
    ]
    const report = calculateProgress(steps)

    expect(report.status).toBe('blocked')
    expect(report.stepsCompleted).toBe(1)
    expect(report.blockers).toContain('Some error')
  })

  it('calculates percentComplete correctly', () => {
    const steps = [
      createMockStep({ status: 'success' }),
      createMockStep({ status: 'pending' }),
      createMockStep({ status: 'pending' }),
    ]
    const report = calculateProgress(steps, 3)

    // 1 of 3 = 33.33% → rounds to 33%
    expect(report.percentComplete).toBe(33)
  })

  it('extracts milestones from successful steps with tools', () => {
    const steps = [
      createMockStep({
        status: 'success',
        description: 'Created user authentication',
        toolsCalled: [createMockToolCallInfo({ name: 'create_file' })],
      }),
      createMockStep({
        status: 'success',
        description: 'No tools used here',
        toolsCalled: [],
      }),
      createMockStep({
        status: 'success',
        description: 'Implemented database schema with proper indexes',
        toolsCalled: [createMockToolCallInfo({ name: 'write_file' })],
      }),
    ]
    const report = calculateProgress(steps)

    // Only steps with tools should be milestones
    expect(report.milestones).toHaveLength(2)
    expect(report.milestones[0]).toBe('Created user authentication')
    // Long descriptions are truncated to 50 chars
    expect(report.milestones[1]).toBe('Implemented database schema with proper indexes')
  })

  it('extracts blockers from failed steps with errors', () => {
    const steps = [
      createMockStep({ status: 'success' }),
      createMockStep({ status: 'failed', error: 'Network timeout' }),
      createMockStep({ status: 'failed', error: 'Invalid syntax' }),
    ]
    const report = calculateProgress(steps)

    expect(report.blockers).toEqual(['Network timeout', 'Invalid syntax'])
  })

  it('uses totalExpectedSteps override for percentage calculation', () => {
    const steps = [createMockStep({ status: 'success' }), createMockStep({ status: 'success' })]
    const report = calculateProgress(steps, 10)

    // 2 of 10 = 20%
    expect(report.percentComplete).toBe(20)
    expect(report.status).toBe('in_progress')
  })

  it('handles zero expected steps edge case', () => {
    const steps = [createMockStep({ status: 'success' })]
    // totalExpectedSteps = 0 should be handled (uses max logic)
    const report = calculateProgress(steps, 0)

    // total = 0 (passed as totalExpectedSteps), so 1/0 = Infinity
    // Math.round(Infinity) = Infinity
    // The implementation uses totalExpectedSteps ?? Math.max, so 0 is truthy and used directly
    // This results in division by zero. Expect Infinity.
    expect(report.percentComplete).toBe(Number.POSITIVE_INFINITY)
  })
})

// ============================================================================
// evaluateGoal Tests
// ============================================================================

describe('evaluateGoal', () => {
  it('marks goal as achieved for successful result with GOAL terminate mode', () => {
    const result = createMockResult({
      success: true,
      terminateMode: AgentTerminateMode.GOAL,
      output: 'Task completed successfully',
    })
    const evaluation = evaluateGoal('Complete the task', result)

    expect(evaluation.achieved).toBe(true)
    expect(evaluation.confidence).toBeGreaterThanOrEqual(0.8)
    expect(evaluation.evidence).toContain('Agent signaled goal completion via attempt_completion')
  })

  it('increases confidence for successful result with many steps', () => {
    const result = createMockResult({
      success: true,
      terminateMode: AgentTerminateMode.GOAL,
      output: 'Done',
      steps: [
        createMockStep({ status: 'success' }),
        createMockStep({ status: 'success' }),
        createMockStep({ status: 'success' }),
      ],
    })
    const evaluation = evaluateGoal('Do something', result)

    // Base 0.8 + 0.1 for >2 steps = 0.9
    expect(evaluation.confidence).toBeGreaterThanOrEqual(0.9)
    expect(evaluation.evidence).toContain('3 steps completed successfully')
  })

  it('increases confidence for successful result with long output', () => {
    const result = createMockResult({
      success: true,
      terminateMode: AgentTerminateMode.GOAL,
      output: 'A'.repeat(150), // Long output
    })
    const evaluation = evaluateGoal('Do something', result)

    // Base 0.8 + 0.1 for long output = 0.9
    expect(evaluation.confidence).toBeGreaterThanOrEqual(0.9)
    expect(evaluation.evidence).toContain('Agent provided detailed output')
  })

  it('marks goal as not achieved for failed result', () => {
    const result = createMockResult({
      success: false,
      terminateMode: AgentTerminateMode.ERROR,
      output: '',
    })
    const evaluation = evaluateGoal('Complete task', result)

    expect(evaluation.achieved).toBe(false)
    expect(evaluation.confidence).toBeLessThanOrEqual(0.4)
  })

  it('calculates partial confidence for failed result with some successes', () => {
    const result = createMockResult({
      success: false,
      terminateMode: AgentTerminateMode.ERROR,
      steps: [
        createMockStep({ status: 'success' }),
        createMockStep({ status: 'success' }),
        createMockStep({ status: 'failed' }),
      ],
    })
    const evaluation = evaluateGoal('Do task', result)

    // Base 0.2 + 0.2 * (2/3) ≈ 0.33
    expect(evaluation.confidence).toBeGreaterThan(0.2)
    expect(evaluation.confidence).toBeLessThan(0.5)
  })

  it('adds missing requirement for no output', () => {
    const result = createMockResult({
      success: false,
      output: '',
    })
    const evaluation = evaluateGoal('Task', result)

    expect(evaluation.missingRequirements).toContain('No output provided')
    expect(evaluation.suggestions).toContain('Ensure attempt_completion is called with a result')
  })

  it('adds missing requirements for failed steps', () => {
    const result = createMockResult({
      success: false,
      steps: [
        createMockStep({
          status: 'failed',
          description: 'Setup database',
          error: 'Connection failed',
        }),
        createMockStep({ status: 'failed', description: 'Run migration', error: 'Invalid schema' }),
      ],
    })
    const evaluation = evaluateGoal('Setup DB', result)

    expect(evaluation.missingRequirements).toContain('2 steps failed')
    expect(evaluation.missingRequirements.some((r) => r.includes('Connection failed'))).toBe(true)
    expect(evaluation.missingRequirements.some((r) => r.includes('Invalid schema'))).toBe(true)
  })

  it('includes evidence for goal completion via GOAL terminate mode', () => {
    const result = createMockResult({
      success: true,
      terminateMode: AgentTerminateMode.GOAL,
    })
    const evaluation = evaluateGoal('Task', result)

    expect(evaluation.evidence).toContain('Agent signaled goal completion via attempt_completion')
  })

  it('generates suggestions on failure', () => {
    const result = createMockResult({
      success: false,
      terminateMode: AgentTerminateMode.ERROR,
      turns: 5,
      steps: [createMockStep({ status: 'failed', error: 'Failed' })],
    })
    const evaluation = evaluateGoal('Task', result)

    expect(evaluation.suggestions.length).toBeGreaterThan(0)
    expect(evaluation.suggestions).toContain(
      'Review failed steps and consider alternative approaches'
    )
  })

  it('suggests breaking task into smaller sub-goals', () => {
    const result = createMockResult({
      success: false,
      turns: 10,
      steps: [createMockStep(), createMockStep()],
    })
    const evaluation = evaluateGoal('Complex task', result)

    expect(evaluation.suggestions).toContain('Consider breaking the task into smaller sub-goals')
  })
})

// ============================================================================
// analyzeToolUsage Tests
// ============================================================================

describe('analyzeToolUsage', () => {
  it('returns empty map for empty steps', () => {
    const stats = analyzeToolUsage([])

    expect(stats.size).toBe(0)
  })

  it('returns correct stats for single tool', () => {
    const steps = [
      createMockStep({
        toolsCalled: [
          createMockToolCallInfo({ name: 'read_file', success: true, durationMs: 100 }),
        ],
      }),
    ]
    const stats = analyzeToolUsage(steps)

    expect(stats.size).toBe(1)
    const readFile = stats.get('read_file')
    expect(readFile?.count).toBe(1)
    expect(readFile?.successRate).toBe(1)
    expect(readFile?.avgDuration).toBe(100)
  })

  it('aggregates multiple calls to same tool', () => {
    const steps = [
      createMockStep({
        toolsCalled: [
          createMockToolCallInfo({ name: 'bash', success: true, durationMs: 200 }),
          createMockToolCallInfo({ name: 'bash', success: false, durationMs: 150 }),
        ],
      }),
      createMockStep({
        toolsCalled: [createMockToolCallInfo({ name: 'bash', success: true, durationMs: 250 })],
      }),
    ]
    const stats = analyzeToolUsage(steps)

    const bash = stats.get('bash')
    expect(bash?.count).toBe(3)
    expect(bash?.successRate).toBeCloseTo(2 / 3, 2)
    expect(bash?.avgDuration).toBeCloseTo(200, 0) // (200+150+250)/3 = 200
  })

  it('calculates success rate correctly for mixed results', () => {
    const steps = [
      createMockStep({
        toolsCalled: [
          createMockToolCallInfo({ name: 'write_file', success: true }),
          createMockToolCallInfo({ name: 'write_file', success: true }),
          createMockToolCallInfo({ name: 'write_file', success: false }),
        ],
      }),
    ]
    const stats = analyzeToolUsage(steps)

    const writeFile = stats.get('write_file')
    expect(writeFile?.successRate).toBeCloseTo(2 / 3, 2)
  })

  it('handles missing duration gracefully', () => {
    const steps = [
      createMockStep({
        toolsCalled: [
          createMockToolCallInfo({ name: 'glob', success: true, durationMs: undefined }),
        ],
      }),
    ]
    const stats = analyzeToolUsage(steps)

    const glob = stats.get('glob')
    expect(glob?.avgDuration).toBe(0)
  })

  it('tracks multiple different tools separately', () => {
    const steps = [
      createMockStep({
        toolsCalled: [
          createMockToolCallInfo({ name: 'read_file', durationMs: 100 }),
          createMockToolCallInfo({ name: 'write_file', durationMs: 200 }),
          createMockToolCallInfo({ name: 'bash', durationMs: 300 }),
        ],
      }),
    ]
    const stats = analyzeToolUsage(steps)

    expect(stats.size).toBe(3)
    expect(stats.get('read_file')?.count).toBe(1)
    expect(stats.get('write_file')?.count).toBe(1)
    expect(stats.get('bash')?.count).toBe(1)
  })

  it('calculates success rate as 0 for all failures', () => {
    const steps = [
      createMockStep({
        toolsCalled: [
          createMockToolCallInfo({ name: 'bash', success: false }),
          createMockToolCallInfo({ name: 'bash', success: false }),
        ],
      }),
    ]
    const stats = analyzeToolUsage(steps)

    const bash = stats.get('bash')
    expect(bash?.successRate).toBe(0)
  })

  it('averages duration across multiple steps', () => {
    const steps = [
      createMockStep({
        toolsCalled: [createMockToolCallInfo({ name: 'grep', durationMs: 100 })],
      }),
      createMockStep({
        toolsCalled: [createMockToolCallInfo({ name: 'grep', durationMs: 200 })],
      }),
      createMockStep({
        toolsCalled: [createMockToolCallInfo({ name: 'grep', durationMs: 300 })],
      }),
    ]
    const stats = analyzeToolUsage(steps)

    const grep = stats.get('grep')
    expect(grep?.avgDuration).toBeCloseTo(200, 0) // (100+200+300)/3 = 200
  })
})

// ============================================================================
// calculateMetrics Tests
// ============================================================================

describe('calculateMetrics', () => {
  it('calculates basic metrics from simple result', () => {
    const now = Date.now()
    const result = createMockResult({
      durationMs: 5000,
      turns: 2,
      tokensUsed: 1000,
      steps: [
        createMockStep({ status: 'success', startedAt: now, completedAt: now + 1000 }),
        createMockStep({ status: 'success', startedAt: now + 1000, completedAt: now + 3000 }),
      ],
    })
    const metrics = calculateMetrics(result)

    expect(metrics.totalDuration).toBe(5000)
    expect(metrics.avgStepDuration).toBe(1500) // (1000 + 2000) / 2
    expect(metrics.toolCallsPerTurn).toBe(0) // No tools in default mock steps
    expect(metrics.successRate).toBe(1) // 2/2 = 100%
    expect(metrics.tokensPerTurn).toBe(500) // 1000/2
  })

  it('handles zero turns without division by zero', () => {
    const result = createMockResult({
      turns: 0,
      tokensUsed: 1000,
      steps: [],
    })
    const metrics = calculateMetrics(result)

    expect(metrics.toolCallsPerTurn).toBe(0)
    expect(metrics.tokensPerTurn).toBe(0)
  })

  it('handles zero steps without division by zero', () => {
    const result = createMockResult({
      steps: [],
      turns: 1,
    })
    const metrics = calculateMetrics(result)

    expect(metrics.avgStepDuration).toBe(0)
    expect(metrics.successRate).toBe(0)
  })

  it('calculates step duration from startedAt and completedAt', () => {
    const base = Date.now()
    const result = createMockResult({
      steps: [
        createMockStep({ startedAt: base, completedAt: base + 1000 }),
        createMockStep({ startedAt: base + 1000, completedAt: base + 4000 }),
        createMockStep({ startedAt: base + 4000, completedAt: base + 5000 }),
      ],
    })
    const metrics = calculateMetrics(result)

    // (1000 + 3000 + 1000) / 3 = 1666.67
    expect(metrics.avgStepDuration).toBeCloseTo(1666.67, 1)
  })

  it('calculates tokensPerTurn correctly', () => {
    const result = createMockResult({
      turns: 5,
      tokensUsed: 2500,
    })
    const metrics = calculateMetrics(result)

    expect(metrics.tokensPerTurn).toBe(500) // 2500/5
  })

  it('calculates success rate with mixed step statuses', () => {
    const result = createMockResult({
      steps: [
        createMockStep({ status: 'success' }),
        createMockStep({ status: 'success' }),
        createMockStep({ status: 'failed' }),
        createMockStep({ status: 'failed' }),
      ],
    })
    const metrics = calculateMetrics(result)

    expect(metrics.successRate).toBe(0.5) // 2/4 = 50%
  })
})
