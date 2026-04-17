import { createSignal } from 'solid-js'
import { render } from 'solid-js/web'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const readLatestBackendLogsMock = vi.fn<(lines: number) => Promise<string>>()
const clipboardWriteTextMock = vi.fn<(text: string) => Promise<void>>()
const updateSettingsMock = vi.fn()
const setDebugDevModeMock = vi.fn()

const mockAgentState = {
  currentRunId: null as string | null,
  lastError: null as string | null,
  progressMessage: null as string | null,
  eventTimeline: [] as Array<Record<string, unknown>>,
  isRunning: false,
  pendingApproval: false,
  pendingQuestion: false,
  pendingPlan: false,
}

const [mockSettings] = createSignal({
  devMode: true,
  logLevel: 'info',
})

const [mockDevLogs] = createSignal([])

vi.mock('../../../hooks/useAgent', () => ({
  useAgent: () => ({
    currentRunId: () => mockAgentState.currentRunId,
    lastError: () => mockAgentState.lastError,
    progressMessage: () => mockAgentState.progressMessage,
    eventTimeline: () => mockAgentState.eventTimeline,
    isRunning: () => mockAgentState.isRunning,
    pendingApproval: () => mockAgentState.pendingApproval,
    pendingQuestion: () => mockAgentState.pendingQuestion,
    pendingPlan: () => mockAgentState.pendingPlan,
  }),
}))

vi.mock('../../../lib/debug-log', () => ({
  setDebugDevMode: (...args: unknown[]) => setDebugDevModeMock(...args),
}))

vi.mock('../../../services/dev-console', () => ({
  getDevLogs: () => mockDevLogs,
  clearDevLogs: vi.fn(),
}))

vi.mock('../../../services/logger', () => ({
  getBackendLogFilePath: () => '/tmp/logs/desktop-backend.log',
  getLogDirectory: () => '/tmp/logs',
  readLatestBackendLogs: (lines: number) => readLatestBackendLogsMock(lines),
}))

vi.mock('../../../stores/settings', () => ({
  useSettings: () => ({
    settings: mockSettings,
    updateSettings: (...args: unknown[]) => updateSettingsMock(...args),
  }),
}))

vi.mock('./developer/dev-helpers', () => ({
  formatTime: () => '00:00:00.000',
  levelLabel: {
    log: 'LOG',
    info: 'INF',
    warn: 'WRN',
    error: 'ERR',
  },
}))

import { DeveloperTab } from './DeveloperTab'

function deferred<T>() {
  let resolve!: (value: T | PromiseLike<T>) => void
  let reject!: (reason?: unknown) => void
  const promise = new Promise<T>((res, rej) => {
    resolve = res
    reject = rej
  })
  return { promise, resolve, reject }
}

function clickButtonByText(label: string): void {
  const button = Array.from(document.querySelectorAll('button')).find((candidate) =>
    candidate.textContent?.includes(label)
  )
  if (!(button instanceof HTMLButtonElement)) {
    throw new Error(`Could not find button with label containing: ${label}`)
  }
  button.click()
}

function getRegionText(ariaLabel: string): string {
  const region = document.querySelector(`[aria-label="${ariaLabel}"]`)
  if (!(region instanceof HTMLElement)) {
    throw new Error(`Could not find region with aria-label: ${ariaLabel}`)
  }
  return region.textContent ?? ''
}

async function flush(): Promise<void> {
  await Promise.resolve()
  await Promise.resolve()
  await new Promise((resolve) => setTimeout(resolve, 0))
}

describe('DeveloperTab diagnostics', () => {
  let dispose: (() => void) | undefined

  beforeEach(() => {
    vi.clearAllMocks()
    mockAgentState.currentRunId = null
    mockAgentState.lastError = null
    mockAgentState.progressMessage = null
    mockAgentState.eventTimeline = []
    mockAgentState.isRunning = false
    mockAgentState.pendingApproval = false
    mockAgentState.pendingQuestion = false
    mockAgentState.pendingPlan = false
    readLatestBackendLogsMock.mockResolvedValue('backend line')
    clipboardWriteTextMock.mockResolvedValue(undefined)
    Object.defineProperty(navigator, 'clipboard', {
      configurable: true,
      value: {
        writeText: clipboardWriteTextMock,
      },
    })
  })

  afterEach(() => {
    vi.useRealTimers()
    dispose?.()
    dispose = undefined
    document.body.innerHTML = ''
  })

  it('loads backend log tail on mount when dev mode is already enabled', async () => {
    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <DeveloperTab />, container)

    await Promise.resolve()
    await Promise.resolve()

    expect(readLatestBackendLogsMock).toHaveBeenCalledWith(120)
    expect(readLatestBackendLogsMock).toHaveBeenCalledTimes(1)
    expect(setDebugDevModeMock).not.toHaveBeenCalled()
  })

  it('refreshes backend logs from the diagnostics refresh control', async () => {
    const pendingRefresh = deferred<string>()
    readLatestBackendLogsMock
      .mockResolvedValueOnce('backend mount line')
      .mockReturnValueOnce(pendingRefresh.promise)

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <DeveloperTab />, container)
    await flush()

    expect(getRegionText('Latest backend log tail scrollable region')).toContain(
      'backend mount line'
    )

    clickButtonByText('Refresh')
    await flush()

    expect(readLatestBackendLogsMock).toHaveBeenCalledTimes(2)
    expect(readLatestBackendLogsMock).toHaveBeenNthCalledWith(1, 120)
    expect(readLatestBackendLogsMock).toHaveBeenNthCalledWith(2, 120)
    expect(getRegionText('Latest backend log tail scrollable region')).toContain(
      'backend mount line'
    )

    pendingRefresh.resolve('backend refreshed line')
    await flush()

    expect(getRegionText('Latest backend log tail scrollable region')).toContain(
      'backend refreshed line'
    )
  })

  it('ignores stale backend log refresh completions that resolve out-of-order', async () => {
    const slowRefresh = deferred<string>()
    const fastRefresh = deferred<string>()
    readLatestBackendLogsMock
      .mockResolvedValueOnce('backend mount line')
      .mockReturnValueOnce(slowRefresh.promise)
      .mockReturnValueOnce(fastRefresh.promise)

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <DeveloperTab />, container)
    await flush()

    clickButtonByText('Refresh')
    clickButtonByText('Refresh')
    await flush()

    fastRefresh.resolve('latest backend line')
    await flush()

    expect(getRegionText('Latest backend log tail scrollable region')).toContain(
      'latest backend line'
    )

    slowRefresh.resolve('stale backend line')
    await flush()

    expect(getRegionText('Latest backend log tail scrollable region')).toContain(
      'latest backend line'
    )
    expect(getRegionText('Latest backend log tail scrollable region')).not.toContain(
      'stale backend line'
    )
  })

  it('copies a meaningful diagnostics payload to clipboard', async () => {
    readLatestBackendLogsMock.mockResolvedValueOnce('backend diagnostics line')

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <DeveloperTab />, container)
    await flush()

    clickButtonByText('Copy Diagnostics')
    await flush()

    expect(clipboardWriteTextMock).toHaveBeenCalledTimes(1)
    const payload = JSON.parse(clipboardWriteTextMock.mock.calls[0]?.[0] ?? '{}') as {
      runState?: string
      isRunning?: boolean
      eventCount?: number
      logDirectory?: string
      backendLogFile?: string
      backendLogTail?: string | null
      recentAgentEvents?: unknown[]
    }

    expect(payload.runState).toBe('idle')
    expect(payload.isRunning).toBe(false)
    expect(payload.eventCount).toBe(0)
    expect(payload.logDirectory).toBe('/tmp/logs')
    expect(payload.backendLogFile).toBe('/tmp/logs/desktop-backend.log')
    expect(payload.backendLogTail).toBe('backend diagnostics line')
    expect(payload.recentAgentEvents).toEqual([])
    expect(container.textContent).toContain('Copied!')
  })

  it('resets diagnostics copied state after the configured timer delay', async () => {
    vi.useFakeTimers()

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <DeveloperTab />, container)
    await Promise.resolve()
    await Promise.resolve()

    clickButtonByText('Copy Diagnostics')
    await Promise.resolve()

    expect(container.textContent).toContain('Copied!')

    vi.advanceTimersByTime(1499)
    await Promise.resolve()
    expect(container.textContent).toContain('Copied!')

    vi.advanceTimersByTime(1)
    await Promise.resolve()
    expect(container.textContent).toContain('Copy Diagnostics')
  })

  it('renders backend log fallback safely when loading backend logs fails', async () => {
    readLatestBackendLogsMock
      .mockResolvedValueOnce('(failed to read backend logs)')
      .mockResolvedValueOnce('backend recovered line')

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <DeveloperTab />, container)
    await flush()

    expect(readLatestBackendLogsMock).toHaveBeenCalledTimes(1)
    expect(getRegionText('Latest backend log tail scrollable region')).toContain(
      '(failed to read backend logs)'
    )

    clickButtonByText('Refresh')
    await flush()

    expect(readLatestBackendLogsMock).toHaveBeenCalledTimes(2)
    expect(getRegionText('Latest backend log tail scrollable region')).toContain(
      'backend recovered line'
    )
  })

  it('serializes populated diagnostics payload with recent event tail', async () => {
    mockAgentState.currentRunId = 'run-populated'
    mockAgentState.isRunning = true
    mockAgentState.progressMessage = 'streaming response'
    mockAgentState.lastError = 'tool timeout'
    mockAgentState.eventTimeline = [
      ...Array.from({ length: 11 }, (_, index) => ({
        type: 'progress',
        run_id: 'run-populated',
        message: `step ${index}`,
        timestamp: index,
      })),
      {
        type: 'tool_call',
        runId: 'run-populated',
        name: 'read',
        timestamp: 11,
      },
      {
        type: 'tool_result',
        runId: 'run-populated',
        tool_name: 'grep',
        message: 'done',
        timestamp: 12,
      },
    ]
    readLatestBackendLogsMock.mockResolvedValueOnce('backend populated line')

    const container = document.createElement('div')
    document.body.append(container)

    dispose = render(() => <DeveloperTab />, container)
    await flush()

    const payload = JSON.parse(getRegionText('Diagnostics payload scrollable region')) as {
      runId?: string
      runState?: string
      isRunning?: boolean
      progressMessage?: string | null
      lastError?: string | null
      eventCount?: number
      backendLogTail?: string | null
      recentAgentEvents?: Array<{ summary?: string }>
    }

    expect(payload.runId).toBe('run-populated')
    expect(payload.runState).toBe('running')
    expect(payload.isRunning).toBe(true)
    expect(payload.progressMessage).toBe('streaming response')
    expect(payload.lastError).toBe('tool timeout')
    expect(payload.eventCount).toBe(13)
    expect(payload.backendLogTail).toBe('backend populated line')
    expect(payload.recentAgentEvents).toHaveLength(12)
    expect(payload.recentAgentEvents?.[0]?.summary).toContain('step 1')
    expect(payload.recentAgentEvents?.[10]?.summary).toContain('tool=read')
    expect(payload.recentAgentEvents?.[11]?.summary).toContain('tool=grep')
  })
})
