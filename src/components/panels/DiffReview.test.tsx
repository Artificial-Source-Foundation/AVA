import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { DiffReview } from './DiffReview'

vi.mock('../ui/DiffViewer', () => ({
  DiffViewer: (props: { oldContent: string; newContent: string; filename: string }) => (
    <div data-testid={`diff-${props.filename}`}>
      <pre data-kind="old">{props.oldContent}</pre>
      <pre data-kind="new">{props.newContent}</pre>
    </div>
  ),
}))

const OLD_CONTENT = [
  'alpha',
  'hunk-a-old',
  'charlie',
  'delta',
  'echo',
  'foxtrot',
  'golf',
  'hunk-b-old',
  'india',
  'juliet',
].join('\n')

const NEW_CONTENT = [
  'alpha',
  'hunk-a-new',
  'charlie',
  'delta',
  'echo',
  'foxtrot',
  'golf',
  'hunk-b-new',
  'india',
  'juliet',
].join('\n')

describe('DiffReview', () => {
  let container: HTMLElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    document.body.innerHTML = ''
  })

  it('detects separated hunks', () => {
    dispose = render(
      () => <DiffReview oldContent={OLD_CONTENT} newContent={NEW_CONTENT} filename="sample.ts" />,
      container
    )

    expect(container.textContent).toContain('2 hunk(s)')
    expect(container.textContent).toContain('Hunk 1')
    expect(container.textContent).toContain('Hunk 2')
  })

  it('supports per-hunk accept/reject decisions', () => {
    dispose = render(
      () => <DiffReview oldContent={OLD_CONTENT} newContent={NEW_CONTENT} filename="sample.ts" />,
      container
    )

    const acceptButtons = container.querySelectorAll('button[title="Accept hunk"]')
    const rejectButtons = container.querySelectorAll('button[title="Reject hunk"]')
    expect(acceptButtons).toHaveLength(2)
    expect(rejectButtons).toHaveLength(2)

    ;(acceptButtons[0] as HTMLButtonElement).click()
    ;(rejectButtons[1] as HTMLButtonElement).click()

    expect(container.textContent).toContain('hunk-a-new')
    expect(container.textContent).not.toContain('hunk-a-old')
    expect(container.textContent).toContain('hunk-b-old')
    expect(container.textContent).not.toContain('hunk-b-new')
  })

  it('supports bulk accept/reject and reset', () => {
    dispose = render(
      () => <DiffReview oldContent={OLD_CONTENT} newContent={NEW_CONTENT} filename="sample.ts" />,
      container
    )

    const acceptAll = Array.from(container.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Accept All'
    )
    const rejectAll = Array.from(container.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Reject All'
    )
    const reset = container.querySelector('button[title="Reset decisions"]')

    expect(acceptAll).toBeDefined()
    expect(rejectAll).toBeDefined()
    expect(reset).toBeDefined()

    ;(acceptAll as HTMLButtonElement).click()
    expect(container.textContent).toContain('hunk-a-new')
    expect(container.textContent).toContain('hunk-b-new')
    expect(container.textContent).not.toContain('hunk-a-old')
    expect(container.textContent).not.toContain('hunk-b-old')

    ;(rejectAll as HTMLButtonElement).click()
    expect(container.textContent).toContain('hunk-a-old')
    expect(container.textContent).toContain('hunk-b-old')
    expect(container.textContent).not.toContain('hunk-a-new')
    expect(container.textContent).not.toContain('hunk-b-new')

    ;(reset as HTMLButtonElement).click()
    expect(container.textContent).toContain('hunk-a-old')
    expect(container.textContent).toContain('hunk-a-new')
    expect(container.textContent).toContain('hunk-b-old')
    expect(container.textContent).toContain('hunk-b-new')
  })
})
