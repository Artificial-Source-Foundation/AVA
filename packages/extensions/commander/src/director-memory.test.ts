import { describe, expect, it } from 'vitest'
import { buildDirectorMemoryPrompt, loadDirectorMemory } from './director-memory.js'

describe('director memory', () => {
  it('queries and limits memories to 5', async () => {
    const entries = Array.from({ length: 8 }).map((_, index) => ({
      id: `m-${index}`,
      text: `memory ${index}`,
    }))
    const result = await loadDirectorMemory(
      { goal: 'fix bug', cwd: '/repo' },
      {
        search: async () => entries,
        recent: async () => entries,
      }
    )
    expect(result.length).toBe(5)
  })

  it('injects memories into prompt', () => {
    const prompt = buildDirectorMemoryPrompt([{ id: 'm1', text: 'Use AbortController strategy.' }])
    expect(prompt).toContain('Use AbortController strategy.')
  })
})
