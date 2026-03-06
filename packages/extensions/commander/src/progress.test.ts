import { describe, expect, it } from 'vitest'
import { PraxisProgressTracker } from './progress.js'

describe('progress tracker', () => {
  it('builds progress tree from events', () => {
    const tracker = new PraxisProgressTracker()
    tracker.handleEvent({ type: 'praxis:mode-selected', mode: 'full' })
    tracker.handleEvent({
      type: 'praxis:lead-assigned',
      childAgentId: 'lead-1',
      domain: 'frontend',
    })
    tracker.handleEvent({ type: 'praxis:engineer-spawned', childAgentId: 'eng-1', task: 'task-a' })

    const progress = tracker.getProgress()
    expect(progress.mode).toBe('full')
    expect(progress.leads[0]?.engineers[0]?.id).toBe('eng-1')
  })

  it('transitions engineer statuses across review and merge', () => {
    const tracker = new PraxisProgressTracker()
    tracker.handleEvent({
      type: 'praxis:lead-assigned',
      childAgentId: 'lead-1',
      domain: 'frontend',
    })
    tracker.handleEvent({ type: 'praxis:engineer-spawned', childAgentId: 'eng-1', task: 'task-a' })
    tracker.handleEvent({ type: 'praxis:review-requested', agentId: 'eng-1' })
    tracker.handleEvent({ type: 'praxis:review-complete', agentId: 'eng-1', approved: true })
    tracker.handleEvent({ type: 'praxis:merge-complete' })

    const progress = tracker.getProgress()
    expect(progress.leads[0]?.engineers[0]?.status).toBe('complete')
    expect(progress.leads[0]?.status).toBe('complete')
  })
})
