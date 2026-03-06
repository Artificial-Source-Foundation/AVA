import { describe, expect, it } from 'vitest'
import { WindowedFileView } from './windowed-view'

function fileText(lines: number): string {
  return Array.from({ length: lines }, (_, idx) => `line ${idx + 1}`).join('\n')
}

describe('WindowedFileView', () => {
  it('shows small files entirely (no windowing)', () => {
    const view = new WindowedFileView(100)
    const path = '/virtual/small.ts'
    view.setFileContent(path, fileText(10))
    const state = view.open(path)

    expect(view.getWindowText(state).split('\n')).toHaveLength(10)
  })

  it('shows a 100-line window for large files with status', () => {
    const view = new WindowedFileView(100)
    const path = '/virtual/large.ts'
    view.setFileContent(path, fileText(300))
    const state = view.open(path)

    expect(view.getWindowText(state).split('\n')).toHaveLength(100)
    expect(view.formatStatus(state)).toContain('(300 lines)')
  })

  it('scroll_down advances the window correctly', () => {
    const view = new WindowedFileView(100)
    const path = '/virtual/scroll.ts'
    view.setFileContent(path, fileText(300))
    view.open(path)

    const state = view.scrollDown(25)
    expect(state.firstLine).toBe(25)
  })

  it('goto_line centers around target', () => {
    const view = new WindowedFileView(100)
    const path = '/virtual/goto.ts'
    view.setFileContent(path, fileText(300))
    view.open(path)

    const state = view.goto(200)
    expect(state.firstLine).toBe(149)
  })

  it('edit auto-center behavior can center near changed line via goto', () => {
    const view = new WindowedFileView(100)
    const path = '/virtual/edit.ts'
    view.setFileContent(path, fileText(300))
    view.open(path)

    const state = view.goto(250)
    expect(state.firstLine).toBeGreaterThan(100)
  })

  it('status line shows correct above/below counts', () => {
    const view = new WindowedFileView(100)
    const path = '/virtual/status.ts'
    view.setFileContent(path, fileText(300))
    const state = view.open(path)
    view.scrollDown(40)

    const status = view.formatStatus(state)
    expect(status).toContain('40 more above')
    expect(status).toContain('160 more below')
  })
})
