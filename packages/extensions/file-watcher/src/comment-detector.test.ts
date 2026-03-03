import { describe, expect, it } from 'vitest'

import {
  type AvaCommentDirective,
  directiveSignature,
  extractAvaCommentDirectives,
} from './comment-detector.js'

function first(content: string): AvaCommentDirective {
  const found = extractAvaCommentDirectives(content)
  if (found.length === 0) throw new Error('Expected directive')
  return found[0] as AvaCommentDirective
}

describe('comment detector', () => {
  it('detects // AVA directives', () => {
    const directives = extractAvaCommentDirectives('const a = 1\n// AVA: refactor this\n')
    expect(directives).toEqual([
      {
        marker: '// AVA:',
        message: 'refactor this',
        line: 2,
      },
    ])
  })

  it('detects # AVA directives', () => {
    const directives = extractAvaCommentDirectives('x = 1\n# AVA: improve logic\n')
    expect(directives).toEqual([
      {
        marker: '# AVA:',
        message: 'improve logic',
        line: 2,
      },
    ])
  })

  it('ignores inline non-directive occurrences', () => {
    const directives = extractAvaCommentDirectives(
      'const s = "// AVA: not directive"\nprint("# AVA: nope")\n'
    )
    expect(directives).toHaveLength(0)
  })

  it('detects multiple directives and preserves line numbers', () => {
    const directives = extractAvaCommentDirectives('// AVA: first\nconst x = 1\n# AVA: second\n')
    expect(directives).toHaveLength(2)
    expect(directives[0]?.line).toBe(1)
    expect(directives[1]?.line).toBe(3)
  })

  it('builds stable directive signatures', () => {
    const directive = first('// AVA: update docs')
    const sig = directiveSignature('/repo/a.ts', directive)
    expect(sig).toBe('/repo/a.ts:1:// AVA::update docs')
  })
})
