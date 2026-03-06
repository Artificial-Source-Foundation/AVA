import { describe, expect, it } from 'vitest'
import { MockFileSystem } from '../../../core-v2/src/__test-utils__/mock-platform.js'
import { loadCrossToolSkillInstructions } from './skill-compat.js'

describe('loadCrossToolSkillInstructions', () => {
  it('discovers skill files across tool formats and deduplicates by path', async () => {
    const fs = new MockFileSystem()
    fs.addFile('/repo/.claude/skills/a.md', 'claude')
    fs.addFile('/repo/.agents/skills/b.md', 'agents')
    fs.addFile('/repo/GEMINI.md', 'gemini')
    fs.addFile('/repo/.github/copilot-instructions.md', 'copilot')

    const files = await loadCrossToolSkillInstructions('/repo', fs)
    const paths = files.map((f) => f.path).sort()

    expect(paths).toEqual([
      '/repo/.agents/skills/b.md',
      '/repo/.claude/skills/a.md',
      '/repo/.github/copilot-instructions.md',
      '/repo/GEMINI.md',
    ])
  })
})
