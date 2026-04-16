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

    const editButton = container.querySelector('button[title="Edit"]')
    const removeButton = container.querySelector('button[title="Remove"]')
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
    expect(container.querySelector('button[title="Edit"]')).toBeNull()
    expect(container.querySelector('button[title="Remove"]')).toBeNull()
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

    expect(container.querySelector('button[title="Move down"]')).toBeNull()
  })
})
