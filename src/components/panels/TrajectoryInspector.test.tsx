import { render } from 'solid-js/web'

/** Local AgentEvent type (replaces @ava/core-v2/agent import) */
interface AgentEvent {
  type: string
  [key: string]: unknown
}
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { TrajectoryInspector } from './TrajectoryInspector'

let mockTimeline: AgentEvent[] = []

vi.mock('../../hooks/useAgent', () => ({
  useAgent: () => ({
    eventTimeline: () => mockTimeline,
  }),
}))

describe('TrajectoryInspector', () => {
  let container: HTMLDivElement
  let dispose: (() => void) | undefined

  beforeEach(() => {
    mockTimeline = [
      { type: 'turn:start', agentId: 'agent-a', turn: 1 } as AgentEvent,
      {
        type: 'tool:start',
        agentId: 'agent-a',
        toolName: 'read',
        args: { path: 'a.ts' },
      } as AgentEvent,
      {
        type: 'tool:finish',
        agentId: 'agent-a',
        toolName: 'read',
        success: true,
        durationMs: 12,
        output: 'ok',
      } as AgentEvent,
      { type: 'error', agentId: 'agent-a', error: 'boom' } as AgentEvent,
    ]
    container = document.createElement('div')
    document.body.appendChild(container)
  })

  afterEach(() => {
    dispose?.()
    container.remove()
    vi.restoreAllMocks()
  })

  it('renders timeline from mock events', () => {
    dispose = render(() => <TrajectoryInspector sessionId="s1" />, container)
    expect(container.textContent).toContain('Trajectory')
    expect(container.textContent).toContain('Turn 1 Start')
    expect(container.textContent).toContain('Tool Start: read')
    expect(container.textContent).toContain('Tool Finish: read')
  })

  it('filters by event type', () => {
    dispose = render(() => <TrajectoryInspector sessionId="s1" />, container)
    const select = container.querySelector('select') as HTMLSelectElement
    select.value = 'tool:start'
    select.dispatchEvent(new Event('input', { bubbles: true }))

    expect(container.textContent).toContain('Tool Start: read')
    expect(container.textContent).not.toContain('Turn 1 Start')
  })

  it('expands and collapses event details', () => {
    dispose = render(() => <TrajectoryInspector sessionId="s1" />, container)
    const cards = Array.from(container.querySelectorAll('button')).filter((node) =>
      node.textContent?.includes('Tool Start: read')
    )

    expect(cards).toHaveLength(1)
    cards[0]!.click()
    expect(container.querySelector('pre')?.textContent).toContain('"toolName": "read"')

    cards[0]!.click()
    expect(container.querySelector('pre')).toBeNull()
  })

  it('exports valid JSON', () => {
    const createObjectURL = vi.fn<(blob: Blob) => string>(() => 'blob:mock-url')
    const revokeObjectURL = vi.fn()
    const clickSpy = vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {})
    vi.stubGlobal('URL', {
      createObjectURL,
      revokeObjectURL,
    })

    dispose = render(() => <TrajectoryInspector sessionId="s1" />, container)
    const exportButton = Array.from(container.querySelectorAll('button')).find((node) =>
      node.textContent?.includes('Export JSON')
    )
    exportButton?.click()

    expect(createObjectURL).toHaveBeenCalledOnce()
    const firstCall = createObjectURL.mock.calls[0]
    expect(firstCall).toBeDefined()
    const blob = firstCall?.[0]
    expect(blob).toBeInstanceOf(Blob)
    expect(clickSpy).toHaveBeenCalledOnce()
    expect(revokeObjectURL).toHaveBeenCalledWith('blob:mock-url')
  })
})
