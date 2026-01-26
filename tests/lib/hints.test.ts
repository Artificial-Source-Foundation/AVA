/**
 * Tests for Delta9 Context-Aware Hints
 *
 * Consolidated tests - verify behavior, not exhaustive property checks.
 */

import { describe, it, expect } from 'vitest'
import {
  hints,
  getHint,
  getBackgroundListHint,
  getMissionStatusHint,
  getCouncilStatusHint,
} from '../../src/lib/hints.js'

describe('hints object', () => {
  it('should have all required static hints', () => {
    const staticHints = [
      'noTasks', 'noRunningTasks', 'tasksAllComplete',
      'noMission', 'missionComplete', 'missionBlocked',
      'councilEmpty', 'simulationMode', 'memoryEmpty',
      'usingDefaults', 'configLoaded',
    ]
    for (const hint of staticHints) {
      expect(hints[hint as keyof typeof hints]).toBeDefined()
    }
  })

  it('should have all required dynamic hint functions', () => {
    expect(typeof hints.taskFailed).toBe('function')
    expect(typeof hints.taskStale).toBe('function')
    expect(typeof hints.councilPartial).toBe('function')
    expect(typeof hints.agentRecommendation).toBe('function')
    expect(typeof hints.memoryAvailable).toBe('function')
  })

  it('dynamic functions should return strings with parameters', () => {
    expect(hints.taskFailed('operator')).toContain('operator')
    expect(hints.taskStale('bg_123')).toContain('bg_123')
    expect(hints.councilPartial(2, 4)).toContain('2')
    expect(hints.agentRecommendation('complex')).toContain('operator_complex')
    expect(hints.memoryAvailable(5)).toContain('5')
  })
})

describe('getHint', () => {
  it('should return appropriate hints based on context', () => {
    expect(getHint({ totalTasks: 0 })).toBe(hints.noTasks)
    expect(getHint({ hasMission: false })).toBe(hints.noMission)
    expect(getHint({ hasMission: true, missionStatus: 'completed' })).toBe(hints.missionComplete)
    expect(getHint({ oracleCount: 0 })).toBe(hints.councilEmpty)
    expect(getHint({ sdkAvailable: false })).toBe(hints.simulationMode)
    expect(getHint({ memoryKeyCount: 0 })).toBe(hints.memoryEmpty)
    expect(getHint({ configLoaded: false })).toBe(hints.usingDefaults)
  })

  it('should return undefined when no hints apply', () => {
    const hint = getHint({
      totalTasks: 5,
      runningTasks: 2,
      hasMission: true,
      missionStatus: 'in_progress',
      oracleCount: 3,
      respondedOracles: 3,
      sdkAvailable: true,
      configLoaded: true,
    })
    expect(hint).toBeUndefined()
  })
})

describe('specialized hint functions', () => {
  it('getBackgroundListHint returns correct hints', () => {
    expect(getBackgroundListHint(0, 0, 0, 0)).toBe(hints.noTasks)
    expect(getBackgroundListHint(0, 5, 0, 5)).toBe(hints.tasksAllComplete)
    expect(getBackgroundListHint(2, 3, 0, 5)).toBeUndefined()
  })

  it('getMissionStatusHint returns correct hints', () => {
    expect(getMissionStatusHint(false)).toBe(hints.noMission)
    expect(getMissionStatusHint(true, 'completed')).toBe(hints.missionComplete)
    expect(getMissionStatusHint(true, 'blocked')).toBe(hints.missionBlocked)
    expect(getMissionStatusHint(true, 'in_progress', 5, 0)).toBeUndefined()
  })

  it('getCouncilStatusHint returns correct hints', () => {
    expect(getCouncilStatusHint(0)).toBe(hints.councilEmpty)
    expect(getCouncilStatusHint(1)).toContain('Single oracle')
    expect(getCouncilStatusHint(3)).toBeUndefined()
  })
})
