import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import type { ApprovalRequest } from '../../hooks/useAgent'
import { ApprovalDock } from './ApprovalDock'

// Mock Checkbox since it may depend on UI internals
vi.mock('../ui/Checkbox', () => ({
  Checkbox: (props: { id?: string; checked?: boolean; onChange?: (v: boolean) => void }) => (
    <input
      type="checkbox"
      id={props.id}
      checked={props.checked}
      onChange={(e) => props.onChange?.(e.currentTarget.checked)}
    />
  ),
}))

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

  it('calls onResolve(true) when Approve is clicked', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    const approveBtn = container.querySelector('button:last-child') as HTMLButtonElement
    expect(approveBtn.textContent).toContain('Approve')
    approveBtn.click()
    expect(onResolve).toHaveBeenCalledWith(true, false)
  })

  it('calls onResolve(false) when Deny is clicked', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest()} onResolve={onResolve} />,
      container
    )
    const buttons = container.querySelectorAll('button')
    // Find the Deny button
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

  it('auto-expands for critical risk and hides always-allow', () => {
    const onResolve = vi.fn()
    dispose = render(
      () => <ApprovalDock request={makeRequest({ riskLevel: 'critical' })} onResolve={onResolve} />,
      container
    )
    expect(container.textContent).toContain('Critical operation')
    // Should not have always-allow checkbox
    const checkbox = container.querySelector('#dock-always-allow')
    expect(checkbox).toBeNull()
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
})
