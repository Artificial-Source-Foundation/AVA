import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { buildSplitPairs, computeDiff, DiffViewer } from './DiffViewer'

vi.mock('./Button', () => ({
  Button: (props: {
    children: string
    onClick?: () => void
    icon?: unknown
    variant?: string
    size?: string
  }) => (
    <button type="button" onClick={() => props.onClick?.()}>
      {props.icon as string}
      {props.children}
    </button>
  ),
}))

describe('DiffViewer', () => {
  let container: HTMLElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {
        writeText: vi.fn().mockResolvedValue(undefined),
      },
    })
  })

  afterEach(() => {
    dispose?.()
    document.body.innerHTML = ''
    vi.restoreAllMocks()
  })

  it('renders unified and split modes differently', () => {
    dispose = render(
      () => (
        <DiffViewer oldContent={'a\nb'} newContent={'a\nc'} filename="demo.ts" mode="unified" />
      ),
      container
    )

    expect(container.querySelectorAll('td.w-px')).toHaveLength(0)

    dispose?.()
    dispose = render(
      () => <DiffViewer oldContent={'a\nb'} newContent={'a\nc'} filename="demo.ts" mode="split" />,
      container
    )

    expect(container.querySelectorAll('td.w-px').length).toBeGreaterThan(0)
  })

  it('computes line-level diff output correctly', () => {
    const lines = computeDiff('one\ntwo\nthree', 'one\n2\nthree\nfour')

    expect(lines.map((line) => line.type)).toEqual([
      'unchanged',
      'remove',
      'add',
      'unchanged',
      'add',
    ])
    expect(lines[1]).toMatchObject({ type: 'remove', content: 'two', oldLineNumber: 2 })
    expect(lines[2]).toMatchObject({ type: 'add', content: '2', newLineNumber: 2 })

    const pairs = buildSplitPairs(lines)
    expect(pairs.length).toBeGreaterThanOrEqual(4)
    expect(pairs[1]).toMatchObject({
      left: { type: 'remove', content: 'two' },
      right: { type: 'add', content: '2' },
    })
  })

  it('copies unified diff text to clipboard', async () => {
    dispose = render(
      () => (
        <DiffViewer oldContent={'alpha\nbeta'} newContent={'alpha\ngamma'} filename="copy.ts" />
      ),
      container
    )

    const copy = Array.from(container.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Copy'
    )
    expect(copy).toBeDefined()

    ;(copy as HTMLButtonElement).click()
    await Promise.resolve()

    const writeText = (navigator.clipboard.writeText as unknown as ReturnType<typeof vi.fn>).mock
    expect(writeText.calls).toHaveLength(1)
    expect(writeText.calls[0]?.[0]).toContain('- beta')
    expect(writeText.calls[0]?.[0]).toContain('+ gamma')
  })
})
