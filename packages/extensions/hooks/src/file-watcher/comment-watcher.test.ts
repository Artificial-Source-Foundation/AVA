import { describe, expect, it } from 'vitest'
import { CommentWatcher } from './comment-watcher.js'

describe('CommentWatcher', () => {
  it('detects // ava: fix this pattern', () => {
    const watcher = new CommentWatcher()
    const triggers = watcher.scan('/tmp/a.ts', 'const x = 1\n// ava: fix this\n')
    expect(triggers).toHaveLength(1)
    expect(triggers[0]?.comment).toBe('ava: fix this')
  })

  it('detects # ava: fix this pattern', () => {
    const watcher = new CommentWatcher()
    const triggers = watcher.scan('/tmp/tool.py', '# ava: fix this\nprint(1)\n')
    expect(triggers).toHaveLength(1)
    expect(triggers[0]?.comment).toBe('ava: fix this')
  })

  it('detects // TODO(ava): implement pattern', () => {
    const watcher = new CommentWatcher()
    const triggers = watcher.scan('/tmp/a.ts', 'const a = 1\n// TODO(ava): implement\n')
    expect(triggers).toHaveLength(1)
    expect(triggers[0]?.comment).toBe('TODO(ava): implement')
  })

  it('ignores regular comments without ava prefix', () => {
    const watcher = new CommentWatcher()
    const triggers = watcher.scan('/tmp/a.ts', '// TODO: normal task\n// FIXME: normal\n')
    expect(triggers).toHaveLength(0)
  })

  it('returns correct line number and surrounding context', () => {
    const watcher = new CommentWatcher()
    const content = ['line 1', 'line 2', 'line 3', '// ava: inspect this', 'line 5', 'line 6'].join(
      '\n'
    )
    const triggers = watcher.scan('/tmp/a.ts', content)
    expect(triggers).toHaveLength(1)
    expect(triggers[0]?.line).toBe(4)
    expect(triggers[0]?.surrounding).toContain('line 2')
    expect(triggers[0]?.surrounding).toContain('line 6')
  })

  it('supports custom patterns', () => {
    const watcher = new CommentWatcher([String.raw`^\s*//\s*bot:\s*(.+)$`])
    const triggers = watcher.scan('/tmp/a.ts', '// bot: optimize\n')
    expect(triggers).toHaveLength(1)
    expect(triggers[0]?.comment).toBe('ava: optimize')
  })
})
