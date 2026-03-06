import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { DiffReviewPanel } from './DiffReviewPanel'

const { fileOperationsMock, getAvailableEditorsMock, openInEditorMock, emitEventMock } = vi.hoisted(
  () => ({
    fileOperationsMock: vi.fn(),
    getAvailableEditorsMock: vi.fn(),
    openInEditorMock: vi.fn(),
    emitEventMock: vi.fn(),
  })
)

vi.mock('../../stores/session', () => ({
  useSession: () => ({
    fileOperations: fileOperationsMock,
  }),
}))

vi.mock('../../services/ide-integration', () => ({
  getAvailableEditors: getAvailableEditorsMock,
  openInEditor: openInEditorMock,
}))

vi.mock('../../hooks/useExtensionEvents', () => ({
  useExtensionEvent: () => () => undefined,
}))

vi.mock('@ava/core-v2/extensions', () => ({
  emitEvent: emitEventMock,
}))

vi.mock('./DiffReview', () => ({
  DiffReview: (props: { oldContent: string; newContent: string; filename: string }) => (
    <div data-testid={`review-${props.filename}`}>{props.newContent}</div>
  ),
}))

describe('DiffReviewPanel', () => {
  let container: HTMLElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
    fileOperationsMock.mockReset()
    getAvailableEditorsMock.mockReset()
    openInEditorMock.mockReset()
    getAvailableEditorsMock.mockResolvedValue([])
  })

  afterEach(() => {
    dispose?.()
    document.body.innerHTML = ''
  })

  it('deduplicates file operations by latest timestamp per file', async () => {
    fileOperationsMock.mockReturnValue([
      {
        type: 'edit',
        filePath: '/project/src/app.ts',
        timestamp: 100,
        originalContent: 'const version = 1',
        newContent: 'const version = 2',
        linesAdded: 1,
      },
      {
        type: 'edit',
        filePath: '/project/src/app.ts',
        timestamp: 200,
        originalContent: 'const version = 2',
        newContent: 'const version = 3',
        linesAdded: 3,
      },
      {
        type: 'read',
        filePath: '/project/src/skip.ts',
        timestamp: 300,
      },
    ])

    dispose = render(() => <DiffReviewPanel />, container)

    await new Promise((resolve) => setTimeout(resolve, 0))

    expect(container.textContent).toContain('1 file changed')
    expect(container.textContent).toContain('+3')

    const row = Array.from(container.querySelectorAll('button')).find((button) =>
      button.textContent?.includes('app.ts')
    )
    expect(row).toBeDefined()
    ;(row as HTMLButtonElement).click()

    expect(container.textContent).toContain('const version = 3')
    expect(container.textContent).not.toContain('const version = 1')
  })

  it('detects IDE editors and opens files in the preferred editor', async () => {
    fileOperationsMock.mockReturnValue([
      {
        type: 'write',
        filePath: '/project/src/editor.ts',
        timestamp: 10,
        originalContent: 'before',
        newContent: 'after',
      },
    ])
    getAvailableEditorsMock.mockResolvedValue([{ name: 'VS Code', command: 'code' }])

    dispose = render(() => <DiffReviewPanel />, container)

    await new Promise((resolve) => setTimeout(resolve, 0))

    const openButton = container.querySelector('button[title="Open in VS Code"]')
    expect(openButton).toBeDefined()
    ;(openButton as HTMLButtonElement).click()

    expect(openInEditorMock).toHaveBeenCalledWith('code', '/project/src/editor.ts')
  })
})
