import { describe, expect, it } from 'vitest'
import type { TaskPlan } from './planning.js'
import { formatPlanSummary, orderSubtasks, parseTaskPlan } from './planning.js'

describe('parseTaskPlan', () => {
  it('parses valid JSON plan from output', () => {
    const output = `Here is the plan:
{
  "subtasks": [
    { "description": "Add login form", "domain": "frontend", "files": ["src/Login.tsx"], "assignTo": "frontend-lead" },
    { "description": "Add auth API", "domain": "backend", "files": ["src/api/auth.ts"], "assignTo": "backend-lead" }
  ],
  "dependencies": [[1, 0]]
}
Done.`

    const plan = parseTaskPlan(output)
    expect(plan).not.toBeNull()
    expect(plan!.subtasks).toHaveLength(2)
    expect(plan!.subtasks[0].assignTo).toBe('frontend-lead')
    expect(plan!.dependencies).toEqual([[1, 0]])
  })

  it('returns null for non-JSON output', () => {
    expect(parseTaskPlan('Just some text')).toBeNull()
  })

  it('returns null for malformed JSON', () => {
    expect(parseTaskPlan('{ "subtasks": "not-an-array" }')).toBeNull()
  })

  it('returns null for missing required fields', () => {
    const output = '{ "subtasks": [{ "description": "test" }] }'
    expect(parseTaskPlan(output)).toBeNull()
  })

  it('defaults dependencies to empty array', () => {
    const output =
      '{ "subtasks": [{ "description": "test", "domain": "frontend", "files": [], "assignTo": "frontend-lead" }] }'
    const plan = parseTaskPlan(output)
    expect(plan!.dependencies).toEqual([])
  })
})

describe('orderSubtasks', () => {
  it('returns indices respecting dependencies', () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'A', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
        { description: 'B', domain: 'backend', files: [], assignTo: 'backend-lead' },
        { description: 'C', domain: 'testing', files: [], assignTo: 'qa-lead' },
      ],
      dependencies: [
        [0, 2],
        [1, 2],
      ], // C depends on A and B
    }

    const order = orderSubtasks(plan)
    // C (index 2) must come after A (0) and B (1)
    expect(order.indexOf(2)).toBeGreaterThan(order.indexOf(0))
    expect(order.indexOf(2)).toBeGreaterThan(order.indexOf(1))
  })

  it('handles no dependencies', () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'A', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
        { description: 'B', domain: 'backend', files: [], assignTo: 'backend-lead' },
      ],
      dependencies: [],
    }

    const order = orderSubtasks(plan)
    expect(order).toHaveLength(2)
    expect(order).toContain(0)
    expect(order).toContain(1)
  })

  it('handles cycles by returning original order', () => {
    const plan: TaskPlan = {
      subtasks: [
        { description: 'A', domain: 'frontend', files: [], assignTo: 'frontend-lead' },
        { description: 'B', domain: 'backend', files: [], assignTo: 'backend-lead' },
      ],
      dependencies: [
        [0, 1],
        [1, 0],
      ], // cycle
    }

    const order = orderSubtasks(plan)
    expect(order).toEqual([0, 1])
  })
})

describe('formatPlanSummary', () => {
  it('formats plan into readable summary', () => {
    const plan: TaskPlan = {
      subtasks: [
        {
          description: 'Add button',
          domain: 'frontend',
          files: ['src/Button.tsx'],
          assignTo: 'frontend-lead',
        },
        {
          description: 'Add API',
          domain: 'backend',
          files: ['src/api.ts'],
          assignTo: 'backend-lead',
        },
      ],
      dependencies: [[1, 0]],
    }

    const summary = formatPlanSummary(plan)
    expect(summary).toContain('2 subtasks')
    expect(summary).toContain('Add button')
    expect(summary).toContain('delegate_frontend-lead')
    expect(summary).toContain('src/Button.tsx')
    expect(summary).toContain('after #2')
  })
})
