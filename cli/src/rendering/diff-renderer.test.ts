import { EventEmitter } from 'node:events'
import { describe, expect, it } from 'vitest'
import { DiffRenderer } from './diff-renderer.js'

class MockStream extends EventEmitter {
  public readonly chunks: string[] = []
  public isTTY = true
  public columns = 120

  write(chunk: string): boolean {
    this.chunks.push(chunk)
    return true
  }

  clear(): void {
    this.chunks.length = 0
  }
}

describe('DiffRenderer', () => {
  it('identical frames produce no changed-line output', () => {
    const stream = new MockStream()
    const renderer = new DiffRenderer(stream)

    renderer.render(['one', 'two'])
    stream.clear()

    renderer.render(['one', 'two'])

    const output = stream.chunks.join('')
    expect(output).toContain('\x1b[?25l')
    expect(output).toContain('\x1b[?25h')
    expect(output).not.toContain('\x1b[1;1H')
    expect(output).not.toContain('\x1b[2;1H')
  })

  it('single line change only outputs that line', () => {
    const stream = new MockStream()
    const renderer = new DiffRenderer(stream)

    renderer.render(['one', 'two'])
    stream.clear()

    renderer.render(['one', 'changed'])

    const output = stream.chunks.join('')
    expect(output).toContain('\x1b[2;1H\x1b[2Kchanged')
    expect(output).not.toContain('\x1b[1;1H\x1b[2Kone')
  })

  it('resize triggers full redraw', () => {
    const stream = new MockStream()
    const renderer = new DiffRenderer(stream)

    renderer.render(['one'])
    stream.clear()

    stream.emit('resize')
    renderer.render(['one'])

    const output = stream.chunks.join('')
    expect(output).toContain('\x1b[H\x1b[2J')
    expect(output).toContain('\x1b[1;1H\x1b[2Kone')
  })

  it('uses correct ANSI cursor visibility and positioning', () => {
    const stream = new MockStream()
    const renderer = new DiffRenderer(stream)

    renderer.render(['first'])

    const output = stream.chunks.join('')
    expect(output).toContain('\x1b[?25l')
    expect(output).toContain('\x1b[1;1H\x1b[2Kfirst')
    expect(output).toContain('\x1b[2;1H')
    expect(output).toContain('\x1b[?25h')
  })
})
