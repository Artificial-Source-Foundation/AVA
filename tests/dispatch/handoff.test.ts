/**
 * Operator Handoff Tests
 */

import { describe, it, expect } from 'vitest'
import { buildOperatorHandoff, formatHandoffForPrompt } from '../../src/dispatch/handoff.js'
import type { Task, Mission, Objective } from '../../src/types/mission.js'
import { DEFAULT_MUST_NOT } from '../../src/types/handoff.js'

const createTask = (overrides: Partial<Task> = {}): Task => ({
  id: 'task-1',
  description: 'Implement login form',
  status: 'pending',
  attempts: 0,
  acceptanceCriteria: ['Form validates email', 'Shows error on invalid input'],
  files: ['src/components/LoginForm.tsx'],
  filesReadonly: ['src/types/auth.ts'],
  mustNot: ['Do not use any third-party auth libraries'],
  ...overrides,
})

const createMission = (tasks: Task[] = []): Mission => ({
  id: 'mission-1',
  description: 'Build user authentication system',
  status: 'in_progress',
  complexity: 'medium',
  councilMode: 'oracle',
  objectives: [{
    id: 'obj-1',
    description: 'Implement frontend auth',
    status: 'in_progress',
    tasks,
  }] as Objective[],
  currentObjective: 0,
  budget: { limit: 10, spent: 0, breakdown: { council: 0, operators: 0, validators: 0, support: 0 } },
  createdAt: new Date().toISOString(),
  updatedAt: new Date().toISOString(),
})

describe('buildOperatorHandoff', () => {
  it('should build handoff with contract, context, and escalation', () => {
    const task = createTask()
    const mission = createMission([task])

    const handoff = buildOperatorHandoff({
      task,
      mission,
      allTasks: [task],
    })

    // Contract section
    expect(handoff.contract.taskId).toBe('task-1')
    expect(handoff.contract.filesOwned).toContain('src/components/LoginForm.tsx')
    expect(handoff.contract.filesReadonly).toContain('src/types/auth.ts')
    expect(handoff.contract.successCriteria).toHaveLength(2)
    expect(handoff.contract.mustNot).toContain('Do not use any third-party auth libraries')

    // Context section
    expect(handoff.context.missionSummary).toContain('Build user authentication')
    expect(handoff.context.yourRole).toContain('Implement login form')

    // Escalation section
    expect(handoff.escalation.blockedAction).toBeTruthy()
    expect(handoff.escalation.scopeChangeAction).toBeTruthy()
  })

  it('should include default mustNot constraints', () => {
    const task = createTask({ mustNot: undefined })
    const mission = createMission([task])

    const handoff = buildOperatorHandoff({
      task,
      mission,
      allTasks: [task],
    })

    expect(handoff.contract.mustNot).toEqual(expect.arrayContaining(DEFAULT_MUST_NOT))
  })

  it('should show prior work from completed tasks', () => {
    const completedTask = createTask({
      id: 'task-0',
      description: 'Setup auth types',
      status: 'completed',
      filesChanged: ['src/types/auth.ts'],
    })
    const currentTask = createTask({ id: 'task-1' })
    const mission = createMission([completedTask, currentTask])

    const handoff = buildOperatorHandoff({
      task: currentTask,
      mission,
      allTasks: [completedTask, currentTask],
    })

    expect(handoff.context.priorWork).toContain('Setup auth types')
    expect(handoff.context.priorWork).toContain('src/types/auth.ts')
  })

  it('should show pending tasks in next steps', () => {
    const currentTask = createTask({ id: 'task-1' })
    const nextTask = createTask({
      id: 'task-2',
      description: 'Add password reset',
      status: 'pending',
    })
    const mission = createMission([currentTask, nextTask])

    const handoff = buildOperatorHandoff({
      task: currentTask,
      mission,
      allTasks: [currentTask, nextTask],
    })

    expect(handoff.context.nextSteps).toContain('Add password reset')
  })

  it('should include additional context in next steps', () => {
    const task = createTask()
    const mission = createMission([task])

    const handoff = buildOperatorHandoff({
      task,
      mission,
      allTasks: [task],
      additionalContext: 'User reported Safari has issues',
    })

    expect(handoff.context.nextSteps).toContain('Safari has issues')
  })
})

describe('formatHandoffForPrompt', () => {
  it('should format handoff as markdown', () => {
    const task = createTask()
    const mission = createMission([task])

    const handoff = buildOperatorHandoff({
      task,
      mission,
      allTasks: [task],
    })

    const formatted = formatHandoffForPrompt(handoff)

    expect(formatted).toContain('# OPERATOR HANDOFF CONTRACT')
    expect(formatted).toContain('## CONTRACT')
    expect(formatted).toContain('Files You Own')
    expect(formatted).toContain('src/components/LoginForm.tsx')
    expect(formatted).toContain('Files Read-Only')
    expect(formatted).toContain('src/types/auth.ts')
    expect(formatted).toContain('Success Criteria')
    expect(formatted).toContain('[ ] Form validates email')
    expect(formatted).toContain('MUST NOT')
    expect(formatted).toContain('## CONTEXT')
    expect(formatted).toContain('## ESCALATION')
  })

  it('should handle empty file lists gracefully', () => {
    const task = createTask({ files: [], filesReadonly: [] })
    const mission = createMission([task])

    const handoff = buildOperatorHandoff({
      task,
      mission,
      allTasks: [task],
    })

    const formatted = formatHandoffForPrompt(handoff)

    // Should not contain file sections when empty
    expect(formatted).not.toContain('Files You Own')
    expect(formatted).not.toContain('Files Read-Only')
  })
})
