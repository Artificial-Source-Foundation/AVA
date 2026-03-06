import { describe, expect, it } from 'vitest'
import { analyzeDomain, analyzeTask, selectWorker } from './router.js'

describe('router', () => {
  it('classifies test tasks to reviewer role', () => {
    const task = analyzeTask('Add unit tests and review edge cases')
    const selected = selectWorker(task, [
      { name: 'engineer', displayName: 'Engineer', description: '', systemPrompt: '', tools: [] },
      { name: 'reviewer', displayName: 'Reviewer', description: '', systemPrompt: '', tools: [] },
    ])
    expect(selected?.name).toBe('reviewer')
  })

  it('detects backend domain for auth bug fix', () => {
    expect(analyzeDomain('Fix import bug in auth.ts')).toBe('backend')
  })
})
