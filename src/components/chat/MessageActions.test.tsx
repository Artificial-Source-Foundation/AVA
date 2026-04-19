import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const notifySuccessMock = vi.fn()
const notifyErrorMock = vi.fn()

vi.mock('../../contexts/notification', () => ({
  useNotification: () => ({
    success: notifySuccessMock,
    error: notifyErrorMock,
  }),
}))

vi.mock('../../../hooks/use-rust-agent', () => ({
  useRustAgent: () => ({
    isRunning: () => false,
  }),
}))

import { MessageActions } from './MessageActions'

describe('MessageActions branch affordance', () => {
  let dispose: (() => void) | undefined
  const baseMessage = {
    id: 'msg-1',
    sessionId: 'session-1',
    role: 'user' as const,
    content: 'Test message',
    createdAt: Date.now(),
  }

  beforeEach(() => {
    vi.clearAllMocks()
    notifySuccessMock.mockClear()
    notifyErrorMock.mockClear()
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('shows branch button when canBranch is true (Tauri mode)', () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(
      () =>
        MessageActions({
          message: baseMessage,
          isLastMessage: true,
          readOnly: false,
          onEdit: vi.fn(),
          onRegenerate: vi.fn(),
          onCopy: vi.fn(),
          onDelete: vi.fn(),
          onBranch: vi.fn(),
          onRewind: vi.fn(),
          isLoading: false,
          canBranch: true,
        }),
      container
    )

    // Look for the branch button by its title
    const branchButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Branch conversation here'
    )

    expect(branchButton).toBeDefined()
    expect(branchButton?.getAttribute('aria-label')).toBe('Branch conversation here')
  })

  it('shows branch button when canBranch is undefined (backward compatibility)', () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(
      () =>
        MessageActions({
          message: baseMessage,
          isLastMessage: true,
          readOnly: false,
          onEdit: vi.fn(),
          onRegenerate: vi.fn(),
          onCopy: vi.fn(),
          onDelete: vi.fn(),
          onBranch: vi.fn(),
          onRewind: vi.fn(),
          isLoading: false,
          // canBranch is not provided, should default to showing
        }),
      container
    )

    const branchButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Branch conversation here'
    )

    expect(branchButton).toBeDefined()
  })

  it('hides branch button when canBranch is false (web mode)', () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(
      () =>
        MessageActions({
          message: baseMessage,
          isLastMessage: true,
          readOnly: false,
          onEdit: vi.fn(),
          onRegenerate: vi.fn(),
          onCopy: vi.fn(),
          onDelete: vi.fn(),
          onBranch: vi.fn(),
          onRewind: vi.fn(),
          isLoading: false,
          canBranch: false,
        }),
      container
    )

    const branchButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Branch conversation here'
    )

    expect(branchButton).toBeUndefined()
  })

  it('shows copy button regardless of canBranch setting', () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(
      () =>
        MessageActions({
          message: baseMessage,
          isLastMessage: true,
          readOnly: false,
          onEdit: vi.fn(),
          onRegenerate: vi.fn(),
          onCopy: vi.fn(),
          onDelete: vi.fn(),
          onBranch: vi.fn(),
          onRewind: vi.fn(),
          isLoading: false,
          canBranch: false,
        }),
      container
    )

    // Copy button should always be visible
    const copyButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Copy message'
    )

    expect(copyButton).toBeDefined()
  })

  it('shows other action buttons even when canBranch is false', () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(
      () =>
        MessageActions({
          message: { ...baseMessage, role: 'assistant' },
          isLastMessage: false,
          readOnly: false,
          onEdit: vi.fn(),
          onRegenerate: vi.fn(),
          onCopy: vi.fn(),
          onDelete: vi.fn(),
          onBranch: vi.fn(),
          onRewind: vi.fn(),
          isLoading: false,
          canBranch: false,
        }),
      container
    )

    // Copy button should be visible
    const copyButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Copy message'
    )
    expect(copyButton).toBeDefined()

    // Regenerate button should be visible for assistant messages
    const regenerateButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Regenerate response'
    )
    expect(regenerateButton).toBeDefined()

    // Rewind button should be visible for non-last messages
    const rewindButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Rewind to here'
    )
    expect(rewindButton).toBeDefined()

    // Delete button should be visible
    const deleteButton = Array.from(container.querySelectorAll('button')).find((button) =>
      button.getAttribute('title')?.includes('Delete')
    )
    expect(deleteButton).toBeDefined()

    // Branch button should NOT be visible
    const branchButton = Array.from(container.querySelectorAll('button')).find(
      (button) => button.getAttribute('title') === 'Branch conversation here'
    )
    expect(branchButton).toBeUndefined()
  })
})
