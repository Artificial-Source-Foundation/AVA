import { createSignal } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

import { MessageQueueWidget, type QueuedItem } from './MessageQueueWidget'

describe('MessageQueueWidget backend-managed row controls', () => {
  let container: HTMLElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    document.body.removeChild(container)
    vi.restoreAllMocks()
  })

  function mount(queueItems: QueuedItem[]) {
    const onRemove = vi.fn()
    const onEdit = vi.fn()
    const onReorder = vi.fn()
    const onClearAll = vi.fn()
    const [queue] = createSignal(queueItems)

    dispose = render(
      () => (
        <MessageQueueWidget
          queuedMessages={queue}
          onRemove={onRemove}
          onReorder={onReorder}
          onEdit={onEdit}
          onClearAll={onClearAll}
        />
      ),
      container
    )

    return { onRemove, onEdit, onReorder, onClearAll }
  }

  it('renders row controls and local-clear only for user-managed queue items', () => {
    const { onClearAll } = mount([
      { id: 'q-1', content: 'Editable queued item', tier: 'queued', backendManaged: false },
    ])

    // Use aria-label selectors for better accessibility testing
    const editButton = container.querySelector('button[aria-label^="Edit"]')
    const removeButton = container.querySelector('button[aria-label^="Remove"]')
    const clearAllButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.textContent?.trim() === 'Clear local'
    )

    expect(editButton).not.toBeNull()
    expect(removeButton).not.toBeNull()
    expect(clearAllButton).not.toBeUndefined()

    clearAllButton?.dispatchEvent(new MouseEvent('click', { bubbles: true }))
    expect(onClearAll).toHaveBeenCalledTimes(1)
  })

  it('hides row controls and local-clear for backend-managed rows', () => {
    mount([
      { id: 'q-1', content: 'Backend follow-up', tier: 'follow-up', backendManaged: true },
      {
        id: 'q-2',
        content: 'Backend post-complete',
        tier: 'post-complete',
        group: 2,
        backendManaged: true,
      },
    ])

    expect(container.textContent).toContain('2 queued messages')
    expect(container.textContent).toContain('Post-complete')
    expect(container.textContent).toContain('G2')
    expect(container.querySelector('button[aria-label^="Edit"]')).toBeNull()
    expect(container.querySelector('button[aria-label^="Remove"]')).toBeNull()
    expect(
      Array.from(container.querySelectorAll('button')).find(
        (button) => button.textContent?.trim() === 'Clear local'
      )
    ).toBeUndefined()
  })

  it('hides reorder controls when the adjacent row is backend-managed', () => {
    mount([
      { id: 'q-1', content: 'Local queued item', tier: 'queued', backendManaged: false },
      { id: 'q-2', content: 'Backend follow-up', tier: 'follow-up', backendManaged: true },
    ])

    expect(container.querySelector('button[aria-label="Move message down in queue"]')).toBeNull()
  })

  it('maps regular-section actions to section-local indices when post-complete rows are present', () => {
    const { onRemove, onReorder } = mount([
      { id: 'q-1', content: 'Queued first', tier: 'queued', backendManaged: false },
      { id: 'q-2', content: 'Backend post-complete', tier: 'post-complete', group: 1 },
      { id: 'q-3', content: 'Queued second', tier: 'queued', backendManaged: false },
    ])

    const removeSecondQueued = container.querySelector(
      'button[aria-label="Remove queued message 2"]'
    )
    expect(removeSecondQueued).not.toBeNull()

    const moveUpButtons = container.querySelectorAll(
      'button[aria-label="Move message up in queue"]'
    )
    moveUpButtons[0]?.dispatchEvent(new MouseEvent('click', { bubbles: true }))
    removeSecondQueued?.dispatchEvent(new MouseEvent('click', { bubbles: true }))

    expect(onReorder).toHaveBeenCalledWith(1, 0, 'regular')
    expect(onRemove).toHaveBeenCalledWith(1, 'regular')
  })

  it('maps post-complete section actions to post-complete-local indices', () => {
    const { onRemove } = mount([
      { id: 'q-1', content: 'Queued first', tier: 'queued', backendManaged: false },
      {
        id: 'q-2',
        content: 'Local post one',
        tier: 'post-complete',
        group: 1,
        backendManaged: false,
      },
      {
        id: 'q-3',
        content: 'Local post two',
        tier: 'post-complete',
        group: 2,
        backendManaged: false,
      },
    ])

    const removeSecondPost = container.querySelector(
      'button[aria-label="Remove post-complete message 2"]'
    )

    expect(removeSecondPost).not.toBeNull()
    expect(
      container.querySelectorAll('button[aria-label="Move message up in queue"]')
    ).toHaveLength(0)
    expect(
      container.querySelectorAll('button[aria-label="Move message down in queue"]')
    ).toHaveLength(0)

    removeSecondPost?.dispatchEvent(new MouseEvent('click', { bubbles: true }))

    expect(onRemove).toHaveBeenCalledWith(1, 'post-complete')
  })
})
