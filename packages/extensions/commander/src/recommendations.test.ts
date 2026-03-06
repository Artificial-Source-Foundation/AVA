import { describe, expect, it } from 'vitest'
import { buildRecommendation, formatRecommendationMessage } from './recommendations.js'

describe('recommendations', () => {
  it('reads pending roadmap tasks and suggests next step', () => {
    const rec = buildRecommendation('Task X', [
      {
        path: 'docs/planning/roadmap.md',
        content: '- [x] Task X\n- [ ] Task Y\n- [ ] Task Z',
      },
    ])
    expect(rec?.nextAction).toContain('Task Y')
  })

  it('gracefully skips when no roadmap files', () => {
    expect(buildRecommendation('Task X', [])).toBeNull()
  })

  it('formats final recommendation message', () => {
    const msg = formatRecommendationMessage({
      summary: 'Done.',
      nextAction: 'Start Y.',
      parallel: 'Z can run in parallel.',
    })
    expect(msg).toContain('Recommendation')
  })
})
