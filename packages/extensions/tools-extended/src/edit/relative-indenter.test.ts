import { describe, expect, it } from 'vitest'
import { RelativeIndenter } from './relative-indenter'

describe('RelativeIndenter', () => {
  it('round-trips absolute indentation', () => {
    const input = ['function demo() {', '  if (ok) {', '    run()', '  }', '}'].join('\n')
    const indenter = new RelativeIndenter([input])

    const relative = indenter.makeRelative(input)
    const restored = indenter.makeAbsolute(relative)

    expect(restored).toBe(input)
  })

  it('handles dedent transitions', () => {
    const input = ['a', '  b', '    c', '  d', 'e'].join('\n')
    const indenter = new RelativeIndenter([input])
    const restored = indenter.makeAbsolute(indenter.makeRelative(input))
    expect(restored).toBe(input)
  })
})
