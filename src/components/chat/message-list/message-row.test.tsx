import type { JSXElement } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

// Mock notification context (used by MessageActions which is rendered inside MessageBubble)
const notifySuccessMock = vi.fn()
const notifyErrorMock = vi.fn()

vi.mock('../../../contexts/notification', () => ({
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

// Mock @kobalte/core/button to avoid JSX file extension issues in tests
vi.mock('@kobalte/core/button', () => ({
  Button: (props: { children: JSXElement; onClick?: () => void; disabled?: boolean }) => (
    <button type="button" onClick={() => props.onClick?.()} disabled={props.disabled}>
      {props.children}
    </button>
  ),
}))

import type { Message } from '../../../types'
import { MessageRow } from './message-row'

describe('MessageRow context menu branch capability', () => {
  let dispose: (() => void) | undefined

  const baseMessage: Message = {
    id: 'msg-1',
    sessionId: 'session-1',
    role: 'user',
    content: 'Test message content',
    createdAt: Date.now(),
  }

  const createProps = (overrides: { canBranch?: boolean; message?: Message } = {}) => ({
    message: overrides.message ?? baseMessage,
    extraClass: '',
    readOnly: false,
    shouldAnimate: false,
    isEditing: false,
    isRetrying: false,
    isStreaming: false,
    isLastMessage: true,
    onStartEdit: vi.fn(),
    onCancelEdit: vi.fn(),
    onSaveEdit: vi.fn().mockResolvedValue(undefined),
    onRetry: vi.fn(),
    onRegenerate: vi.fn(),
    onDelete: vi.fn(),
    onBranch: vi.fn(),
    onRewind: vi.fn(),
    onRestoreCheckpoint: vi.fn(),
    canBranch: overrides.canBranch,
  })

  const triggerContextMenu = (element: HTMLElement): void => {
    const event = new MouseEvent('contextmenu', {
      bubbles: true,
      cancelable: true,
      clientX: 100,
      clientY: 100,
    })
    element.dispatchEvent(event)
  }

  const getContextMenuItems = (): HTMLElement[] => {
    // Context menu is rendered via Portal to document.body
    return Array.from(document.body.querySelectorAll('[class*="group/item"]'))
  }

  const findMenuItemByLabel = (label: string): HTMLElement | undefined => {
    return getContextMenuItems().find((item) => item.textContent?.includes(label))
  }

  beforeEach(() => {
    vi.clearAllMocks()
  })

  afterEach(() => {
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('shows "Branch from here" in context menu when canBranch is true', () => {
    const container = document.createElement('div')
    document.body.append(container)

    const onBranch = vi.fn()

    dispose = render(
      () =>
        MessageRow({
          ...createProps({ canBranch: true }),
          onBranch,
        }),
      container
    )

    // Trigger context menu on the article element
    const article = container.querySelector('article')
    expect(article).toBeDefined()
    triggerContextMenu(article!)

    // Find the "Branch from here" menu item
    const branchItem = findMenuItemByLabel('Branch from here')
    expect(branchItem).toBeDefined()
    expect(branchItem?.textContent).toContain('Branch from here')
  })

  it('calls onBranch when "Branch from here" is clicked', () => {
    const container = document.createElement('div')
    document.body.append(container)

    const onBranch = vi.fn()

    dispose = render(
      () =>
        MessageRow({
          ...createProps({ canBranch: true }),
          onBranch,
        }),
      container
    )

    // Trigger context menu
    const article = container.querySelector('article')
    triggerContextMenu(article!)

    // Click the branch menu item
    const branchItem = findMenuItemByLabel('Branch from here')
    expect(branchItem).toBeDefined()
    branchItem!.click()

    expect(onBranch).toHaveBeenCalledTimes(1)
  })

  it('hides "Branch from here" when canBranch is false', () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => MessageRow(createProps({ canBranch: false })), container)

    // Trigger context menu
    const article = container.querySelector('article')
    triggerContextMenu(article!)

    // "Branch from here" should NOT be present
    const branchItem = findMenuItemByLabel('Branch from here')
    expect(branchItem).toBeUndefined()
  })

  it('shows other menu items when canBranch is false', () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => MessageRow(createProps({ canBranch: false })), container)

    // Trigger context menu
    const article = container.querySelector('article')
    triggerContextMenu(article!)

    // "Copy" should still be visible
    const copyItem = findMenuItemByLabel('Copy')
    expect(copyItem).toBeDefined()
    expect(copyItem?.textContent).toContain('Copy')

    // "Edit" should still be visible for user messages
    const editItem = findMenuItemByLabel('Edit')
    expect(editItem).toBeDefined()
    expect(editItem?.textContent).toContain('Edit')

    // "Delete message" should still be visible
    const deleteItem = findMenuItemByLabel('Delete message')
    expect(deleteItem).toBeDefined()
    expect(deleteItem?.textContent).toContain('Delete message')
  })

  it('shows "Branch from here" when canBranch is omitted (backward compatibility)', () => {
    const container = document.createElement('div')
    document.body.append(container)

    const onBranch = vi.fn()

    dispose = render(
      () =>
        MessageRow({
          ...createProps(),
          onBranch,
          // canBranch is intentionally omitted
        }),
      container
    )

    // Trigger context menu
    const article = container.querySelector('article')
    triggerContextMenu(article!)

    // "Branch from here" should be present (defaults to showing)
    const branchItem = findMenuItemByLabel('Branch from here')
    expect(branchItem).toBeDefined()
    expect(branchItem?.textContent).toContain('Branch from here')

    // Click should work
    branchItem!.click()
    expect(onBranch).toHaveBeenCalledTimes(1)
  })

  it('menu closes after clicking an item', async () => {
    // Mock clipboard API for this test
    const originalClipboard = navigator.clipboard
    // @ts-expect-error Mocking clipboard for test
    navigator.clipboard = { writeText: vi.fn().mockResolvedValue(undefined) }

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => MessageRow(createProps({ canBranch: true })), container)

    // Trigger context menu
    const article = container.querySelector('article')
    triggerContextMenu(article!)

    // Menu should be visible
    expect(getContextMenuItems().length).toBeGreaterThan(0)

    // Click an item
    const copyItem = findMenuItemByLabel('Copy')
    copyItem!.click()

    // Wait a tick for the action and onClose to process
    await new Promise((resolve) => setTimeout(resolve, 0))

    // After clicking, the portal menu should be removed from body
    // The menu items should no longer be present
    const menuAfterClick = document.body.querySelector('[class*="animate-context-menu"]')
    expect(menuAfterClick).toBeNull()

    // Restore clipboard
    // @ts-expect-error Restoring clipboard
    navigator.clipboard = originalClipboard
  })
})
