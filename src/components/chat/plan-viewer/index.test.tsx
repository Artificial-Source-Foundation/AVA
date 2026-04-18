import { render } from 'solid-js/web'
import { beforeEach, describe, expect, it, vi } from 'vitest'
import { PlanOverlay } from './index'

// Mock dependencies
const mockResolvePlan = vi.fn(() => Promise.resolve())
const mockExecutePlan = vi.fn()
const mockClosePlan = vi.fn()
const mockAddAnnotation = vi.fn()
const mockRemoveAnnotation = vi.fn()

// Track overlay open state and mutable annotations
let isOverlayOpen = true
let mockAnnotations: Array<{
  id: string
  type: string
  originalText: string
  commentText: string
  createdAt: number
}> = []

vi.mock('../../../hooks/useAgent', () => ({
  useAgent: () => ({
    resolvePlan: mockResolvePlan,
  }),
}))

vi.mock('../../../stores/planOverlayStore', () => ({
  usePlanOverlay: () => ({
    activePlan: () =>
      isOverlayOpen ? { summary: 'Test Plan', steps: [{ id: '1', description: 'Step 1' }] } : null,
    isOpen: () => isOverlayOpen,
    closePlan: mockClosePlan,
    executePlan: mockExecutePlan,
    annotations: () => mockAnnotations,
    addAnnotation: mockAddAnnotation,
    removeAnnotation: mockRemoveAnnotation,
    stepComments: () => ({}),
    previousPlan: () => null,
    showDiff: () => false,
    hasDiff: () => false,
    toggleDiff: vi.fn(),
  }),
}))

// Mock child components
vi.mock('./PlanHeader', () => ({
  PlanHeader: (props: { onApprove: () => void; onSendFeedback: () => void }) => (
    <div data-testid="plan-header">
      <button type="button" data-testid="approve-btn" onClick={props.onApprove}>
        Approve
      </button>
      <button type="button" data-testid="send-feedback-btn" onClick={props.onSendFeedback}>
        Send Feedback
      </button>
    </div>
  ),
}))

vi.mock('./TOCSidebar', () => ({
  TOCSidebar: () => <div data-testid="toc-sidebar">TOC</div>,
}))

vi.mock('./AnnotationToolstrip', () => ({
  AnnotationToolstrip: () => <div data-testid="toolstrip">Toolstrip</div>,
}))

vi.mock('./PlanDocument', () => ({
  PlanDocument: () => <div data-testid="plan-document">Document</div>,
  formatPlanMarkdown: () => '# Plan',
  parsePlanMarkdown: () => null,
}))

vi.mock('./AnnotationsPanel', () => ({
  AnnotationsPanel: () => <div data-testid="annotations-panel">Annotations</div>,
}))

describe('PlanOverlay resolve failure handling', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    mockResolvePlan.mockResolvedValue(undefined)
    isOverlayOpen = true
    mockAnnotations = []
  })

  it('awaits resolvePlan on approve and does not execute on failure', async () => {
    mockResolvePlan.mockRejectedValueOnce(new Error('Network error'))

    const container = document.createElement('div')
    document.body.appendChild(container)

    render(() => <PlanOverlay />, container)

    const approveBtn = document.querySelector('[data-testid="approve-btn"]')
    expect(approveBtn).not.toBeNull()
    ;(approveBtn as HTMLButtonElement).click()

    // Wait for async rejection
    await Promise.resolve()
    await Promise.resolve()

    // resolvePlan should have been called
    expect(mockResolvePlan).toHaveBeenCalledWith('approved', expect.any(Object), undefined, {})
    // But executePlan should NOT be called on failure
    expect(mockExecutePlan).not.toHaveBeenCalled()
    // Overlay should stay open (closePlan not called)
    expect(mockClosePlan).not.toHaveBeenCalled()

    document.body.removeChild(container)
  })

  it('calls executePlan on successful resolve', async () => {
    mockResolvePlan.mockResolvedValueOnce(undefined)

    const container = document.createElement('div')
    document.body.appendChild(container)

    render(() => <PlanOverlay />, container)

    const approveBtn = document.querySelector('[data-testid="approve-btn"]')
    ;(approveBtn as HTMLButtonElement).click()

    // Wait for async resolution
    await Promise.resolve()

    expect(mockResolvePlan).toHaveBeenCalledWith('approved', expect.any(Object), undefined, {})
    // executePlan SHOULD be called on success
    expect(mockExecutePlan).toHaveBeenCalled()

    document.body.removeChild(container)
  })

  it('awaits resolvePlan on reject and does not close on failure', async () => {
    mockResolvePlan.mockRejectedValueOnce(new Error('Network error'))

    // Set annotations so there's feedback to send (otherwise closePlan is called immediately)
    mockAnnotations = [
      {
        id: 'ann-1',
        type: 'comment',
        originalText: 'Test text',
        commentText: 'Test feedback',
        createdAt: Date.now(),
      },
    ]

    const container = document.createElement('div')
    document.body.appendChild(container)

    render(() => <PlanOverlay />, container)

    const feedbackBtn = document.querySelector('[data-testid="send-feedback-btn"]')
    expect(feedbackBtn).not.toBeNull()
    ;(feedbackBtn as HTMLButtonElement).click()

    // Wait for async rejection
    await Promise.resolve()
    await Promise.resolve()
    await Promise.resolve()

    // resolvePlan should have been called (attempted)
    expect(mockResolvePlan).toHaveBeenCalled()
    // Overlay should stay open on failure - closePlan should NOT be called
    expect(mockClosePlan).not.toHaveBeenCalled()

    document.body.removeChild(container)
  })
})
