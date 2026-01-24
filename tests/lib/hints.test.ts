/**
 * Tests for Delta9 Context-Aware Hints
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
  it('has background task hints', () => {
    expect(hints.noTasks).toBeDefined()
    expect(hints.noRunningTasks).toBeDefined()
    expect(hints.tasksAllComplete).toBeDefined()
    expect(typeof hints.taskFailed).toBe('function')
    expect(typeof hints.taskStale).toBe('function')
  })

  it('has mission hints', () => {
    expect(hints.noMission).toBeDefined()
    expect(hints.missionComplete).toBeDefined()
    expect(hints.missionBlocked).toBeDefined()
    expect(hints.noObjectives).toBeDefined()
    expect(hints.missionNoTasks).toBeDefined()
    expect(hints.tasksNeedValidation).toBeDefined()
  })

  it('has council hints', () => {
    expect(hints.councilEmpty).toBeDefined()
    expect(typeof hints.councilPartial).toBe('function')
    expect(hints.quickConsultAvailable).toBeDefined()
  })

  it('has delegation hints', () => {
    expect(hints.simulationMode).toBeDefined()
    expect(typeof hints.agentRecommendation).toBe('function')
    expect(hints.backgroundRecommendation).toBeDefined()
  })

  it('has validation hints', () => {
    expect(hints.validationPending).toBeDefined()
    expect(hints.allTasksValidated).toBeDefined()
    expect(hints.validationFailed).toBeDefined()
  })

  it('has memory hints', () => {
    expect(hints.memoryEmpty).toBeDefined()
    expect(typeof hints.memoryAvailable).toBe('function')
  })

  it('has config hints', () => {
    expect(hints.usingDefaults).toBeDefined()
    expect(hints.configLoaded).toBeDefined()
  })
})

describe('dynamic hint functions', () => {
  describe('taskFailed', () => {
    it('includes agent name in hint', () => {
      const hint = hints.taskFailed('operator')
      expect(hint).toContain('operator')
    })
  })

  describe('taskStale', () => {
    it('includes task ID in hint', () => {
      const hint = hints.taskStale('bg_123')
      expect(hint).toContain('bg_123')
    })
  })

  describe('councilPartial', () => {
    it('includes counts in hint', () => {
      const hint = hints.councilPartial(2, 4)
      expect(hint).toContain('2')
      expect(hint).toContain('4')
    })
  })

  describe('agentRecommendation', () => {
    it('recommends operator_complex for complex tasks', () => {
      const hint = hints.agentRecommendation('complex')
      expect(hint).toContain('operator_complex')
    })

    it('recommends operator for simple tasks', () => {
      const hint = hints.agentRecommendation('simple')
      expect(hint).toContain('operator')
    })
  })

  describe('memoryAvailable', () => {
    it('includes count in hint', () => {
      const hint = hints.memoryAvailable(5)
      expect(hint).toContain('5')
    })
  })
})

describe('getHint', () => {
  it('returns noTasks hint when totalTasks is 0', () => {
    const hint = getHint({ totalTasks: 0 })
    expect(hint).toBe(hints.noTasks)
  })

  it('returns failed tasks hint when no running but have failed', () => {
    const hint = getHint({ totalTasks: 5, runningTasks: 0, failedTasks: 2 })
    expect(hint).toContain('failed')
  })

  it('returns noRunningTasks when all done', () => {
    const hint = getHint({ totalTasks: 5, runningTasks: 0, failedTasks: 0 })
    expect(hint).toBe(hints.noRunningTasks)
  })

  it('returns noMission hint when hasMission is false', () => {
    const hint = getHint({ hasMission: false })
    expect(hint).toBe(hints.noMission)
  })

  it('returns missionComplete hint when status is completed', () => {
    const hint = getHint({ hasMission: true, missionStatus: 'completed' })
    expect(hint).toBe(hints.missionComplete)
  })

  it('returns tasksNeedValidation hint when pending validation', () => {
    const hint = getHint({ pendingValidation: 3 })
    expect(hint).toBe(hints.tasksNeedValidation)
  })

  it('returns councilEmpty hint when no oracles', () => {
    const hint = getHint({ oracleCount: 0 })
    expect(hint).toBe(hints.councilEmpty)
  })

  it('returns councilPartial hint when not all oracles responded', () => {
    const hint = getHint({ oracleCount: 4, respondedOracles: 2 })
    expect(hint).toContain('2')
    expect(hint).toContain('4')
  })

  it('returns simulationMode hint when SDK unavailable', () => {
    const hint = getHint({ sdkAvailable: false })
    expect(hint).toBe(hints.simulationMode)
  })

  it('returns memoryEmpty hint when no memory keys', () => {
    const hint = getHint({ memoryKeyCount: 0 })
    expect(hint).toBe(hints.memoryEmpty)
  })

  it('returns usingDefaults hint when config not loaded', () => {
    const hint = getHint({ configLoaded: false })
    expect(hint).toBe(hints.usingDefaults)
  })

  it('returns undefined when no hints apply', () => {
    const hint = getHint({
      totalTasks: 5,
      runningTasks: 2,
      failedTasks: 0,
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

describe('getBackgroundListHint', () => {
  it('returns noTasks hint when total is 0', () => {
    const hint = getBackgroundListHint(0, 0, 0, 0)
    expect(hint).toBe(hints.noTasks)
  })

  it('returns failed hint when no running but have failed', () => {
    const hint = getBackgroundListHint(0, 3, 2, 5)
    expect(hint).toContain('failed')
    expect(hint).toContain('2')
  })

  it('returns tasksAllComplete when all completed', () => {
    const hint = getBackgroundListHint(0, 5, 0, 5)
    expect(hint).toBe(hints.tasksAllComplete)
  })

  it('returns undefined when tasks running', () => {
    const hint = getBackgroundListHint(2, 3, 0, 5)
    expect(hint).toBeUndefined()
  })
})

describe('getMissionStatusHint', () => {
  it('returns noMission hint when no mission', () => {
    const hint = getMissionStatusHint(false)
    expect(hint).toBe(hints.noMission)
  })

  it('returns missionComplete hint when completed', () => {
    const hint = getMissionStatusHint(true, 'completed')
    expect(hint).toBe(hints.missionComplete)
  })

  it('returns missionBlocked hint when blocked', () => {
    const hint = getMissionStatusHint(true, 'blocked')
    expect(hint).toBe(hints.missionBlocked)
  })

  it('returns noTasks hint when taskCount is 0', () => {
    const hint = getMissionStatusHint(true, 'in_progress', 0)
    expect(hint).toBe(hints.noTasks)
  })

  it('returns validation pending hint when tasks need validation', () => {
    const hint = getMissionStatusHint(true, 'in_progress', 5, 3)
    expect(hint).toContain('3')
    expect(hint).toContain('validation')
  })

  it('returns undefined when no hints apply', () => {
    const hint = getMissionStatusHint(true, 'in_progress', 5, 0)
    expect(hint).toBeUndefined()
  })
})

describe('getCouncilStatusHint', () => {
  it('returns councilEmpty hint when no oracles', () => {
    const hint = getCouncilStatusHint(0)
    expect(hint).toBe(hints.councilEmpty)
  })

  it('returns hint about adding more oracles when only 1', () => {
    const hint = getCouncilStatusHint(1)
    expect(hint).toContain('Single oracle')
    expect(hint).toContain('more')
  })

  it('returns undefined when multiple oracles configured', () => {
    const hint = getCouncilStatusHint(3)
    expect(hint).toBeUndefined()
  })
})
