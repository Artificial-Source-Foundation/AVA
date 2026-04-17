import { beforeEach, describe, expect, it, vi } from 'vitest'

const invokeMock = vi.fn()
const appDataDirMock = vi.fn()

vi.mock('@tauri-apps/api/core', () => ({
  invoke: (...args: unknown[]) => invokeMock(...args),
}))

vi.mock('@tauri-apps/api/path', () => ({
  appDataDir: () => appDataDirMock(),
}))

describe('readLatestBackendLogs', () => {
  beforeEach(() => {
    vi.clearAllMocks()
    vi.resetModules()
  })

  it('returns a stable fallback when backend logger is not initialized', async () => {
    const { readLatestBackendLogs } = await import('./logger')

    await expect(readLatestBackendLogs(120)).resolves.toBe('(backend logger not initialized)')
  })

  it('returns service failure sentinel when backend log reads fail', async () => {
    appDataDirMock.mockResolvedValue('/tmp/')
    invokeMock.mockImplementation((command: string) => {
      if (command === 'read_latest_logs') {
        return Promise.reject(new Error('backend unavailable'))
      }
      return Promise.resolve(undefined)
    })

    const logger = await import('./logger')

    await logger.initLogger()

    await expect(logger.readLatestBackendLogs(120)).resolves.toBe('(failed to read backend logs)')
    expect(invokeMock).toHaveBeenCalledWith('read_latest_logs', {
      path: '/tmp/logs/desktop-backend.log',
      lines: 120,
    })

    await logger.destroyLogger()
  })
})
