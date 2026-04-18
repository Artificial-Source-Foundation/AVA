import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { ApprovalRequest } from '../../hooks/useAgent'
import { ApprovalDock } from './ApprovalDock'

function makeRequest(overrides: Partial<ApprovalRequest> = {}): ApprovalRequest {
  return {
    id: 'test-1',
    toolName: 'write_file',
    type: 'file',
    riskLevel: 'medium',
    description: 'Write to /src/test.ts',
    args: { path: '/src/test.ts', content: 'hello' },
    resolve: () => {},
    ...overrides,
  }
}

describe('ApprovalDock', () => {
  let container: HTMLElement
  let dispose: () => void

  beforeEach(() => {
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    document.body.removeChild(container)
    vi.restoreAllMocks()
  })

  it('renders nothing when request is null', () => {
    const onResolve = vi.fn()
    dispose = render(() => <ApprovalDock request={null} onResolve={onResolve} />, container)
    expect(container.innerHTML).toBe('')
  })

  it('shows tool name and risk badge', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    expect(container.textContent).toContain('write_file')
    expect(container.textContent).toContain('Medium')
  })

  it('shows three action buttons: Deny, Approve, Always Allow', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    const buttons = Array.from(container.querySelectorAll('button'))
    const labels = buttons.map((b) => b.textContent?.trim())
    expect(labels).toContain('Deny')
    expect(labels).toContain('Approve')
    expect(labels).toContain('Always Allow')
  })

  it('calls onResolve(true, false) when Approve is clicked', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    const buttons = Array.from(container.querySelectorAll('button'))
    const approveBtn = buttons.find((b) => b.textContent?.includes('Approve'))
    expect(approveBtn).toBeDefined()
    approveBtn!.click()
    expect(onResolve).toHaveBeenCalledWith(true, false)
  })

  it('calls onResolve(true, true) when Always Allow is clicked', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    const buttons = Array.from(container.querySelectorAll('button'))
    const alwaysBtn = buttons.find((b) => b.textContent?.includes('Always Allow'))
    expect(alwaysBtn).toBeDefined()
    alwaysBtn!.click()
    expect(onResolve).toHaveBeenCalledWith(true, true)
  })

  it('calls onResolve(false) when Deny is clicked', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    const buttons = container.querySelectorAll('button')
    const denyBtn = Array.from(buttons).find((b) => b.textContent?.includes('Deny'))
    expect(denyBtn).toBeDefined()
    denyBtn!.click()
    expect(onResolve).toHaveBeenCalledWith(false)
  })

  it('handles Enter key to approve', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter' }))
    expect(onResolve).toHaveBeenCalledWith(true, false)
  })

  it('handles Shift+Enter key to always allow', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', shiftKey: true }))
    expect(onResolve).toHaveBeenCalledWith(true, true)
  })

  it('handles Escape key to deny', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Escape' }))
    expect(onResolve).toHaveBeenCalledWith(false)
  })

  it('auto-expands for high risk', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest({ riskLevel: 'high' })} onResolve={onResolve} />,
      container
    )
    // High risk should show the warning banner
    expect(container.textContent).toContain('High-risk operation')
  })

  it('auto-expands for critical risk and hides Always Allow button', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest({ riskLevel: 'critical' })} onResolve={onResolve} />,
      container
    )
    expect(container.textContent).toContain('Critical operation')
    // Should not have Always Allow button for critical risk
    const buttons = Array.from(container.querySelectorAll('button'))
    const alwaysBtn = buttons.find((b) => b.textContent?.includes('Always Allow'))
    expect(alwaysBtn).toBeUndefined()
  })

  it('does not call always allow via Shift+Enter for critical risk', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest({ riskLevel: 'critical' })} onResolve={onResolve} />,
      container
    )
    document.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', shiftKey: true }))
    // Shift+Enter should do nothing for critical (no always-allow)
    expect(onResolve).not.toHaveBeenCalled()
  })

  it('defaults riskLevel to medium when undefined', () => {
    const onResolve = vi.fn()
    const req = makeRequest()
    // Simulate the agent bridge path where riskLevel might be missing
    ;(req as unknown as Record<string, unknown>).riskLevel = undefined
    dispose = render(() => <ApprovalDock request={req} onResolve={onResolve} />, container)
    expect(container.textContent).toContain('Medium')
  })

  it('shows args in expanded view', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest({ riskLevel: 'high' })} onResolve={onResolve} />,
      container
    )
    // Auto-expanded for high risk, should show args
    expect(container.textContent).toContain('path:')
    expect(container.textContent).toContain('/src/test.ts')
  })

  it('preserves native button activation when Enter is pressed on a focused button', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    // Get the Deny button and focus it
    const denyBtn = container.querySelector('button') as HTMLButtonElement
    expect(denyBtn?.textContent).toContain('Deny')
    denyBtn.focus()
    expect(document.activeElement).toBe(denyBtn)

    // Press Enter while Deny button is focused - dispatch on the button itself
    // so e.target is the button element
    denyBtn.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

    // Global handler should NOT have been called because focus is on a button
    // Native button click handler will fire instead (handled by the button's onClick)
    expect(onResolve).not.toHaveBeenCalledWith(true, false)
    expect(onResolve).not.toHaveBeenCalledWith(true, true)
  })

  it('preserves native button activation when Enter is pressed on Always Allow button', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    // Find the Always Allow button
    const buttons = Array.from(container.querySelectorAll('button'))
    const alwaysBtn = buttons.find((b) =>
      b.textContent?.includes('Always Allow')
    ) as HTMLButtonElement
    expect(alwaysBtn).toBeDefined()

    alwaysBtn.focus()
    expect(document.activeElement).toBe(alwaysBtn)

    // Press Enter while Always Allow button is focused - dispatch on the button
    alwaysBtn.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }))

    // Global handler should NOT have been called
    expect(onResolve).not.toHaveBeenCalledWith(true, false)
    expect(onResolve).not.toHaveBeenCalledWith(true, true)
  })
})
