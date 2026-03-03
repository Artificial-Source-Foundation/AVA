import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'

const { onEventMock } = vi.hoisted(() => ({
  onEventMock: vi.fn(),
}))

vi.mock('@ava/core-v2/extensions', () => ({
  onEvent: onEventMock,
}))

import {
  isNotificationMessage,
  isRequestMessage,
  isResponseMessage,
  MCPToolServer,
} from './server.js'

describe('mcp server mode', () => {
  beforeEach(() => {
    onEventMock.mockReset()
    onEventMock.mockReturnValue({ dispose: vi.fn() })
  })

  afterEach(async () => {
    vi.restoreAllMocks()
  })

  it('starts and stops without transports when disabled options are empty', async () => {
    const server = new MCPToolServer({})
    await server.start()
    await server.stop()
    expect(onEventMock).toHaveBeenCalledTimes(2)
  })

  it('starts stdio mode when configured', async () => {
    const stdinOn = vi.spyOn(process.stdin, 'on')
    const stdinOff = vi.spyOn(process.stdin, 'off')
    const server = new MCPToolServer({ stdio: true })
    await server.start()
    expect(stdinOn).toHaveBeenCalledWith('data', expect.any(Function))
    await server.stop()
    expect(stdinOff).toHaveBeenCalledWith('data', expect.any(Function))
  })

  it('identifies request messages', () => {
    expect(isRequestMessage({ jsonrpc: '2.0', id: 1, method: 'initialize' })).toBe(true)
    expect(isRequestMessage({ jsonrpc: '2.0', method: 'notify' })).toBe(false)
  })

  it('identifies response messages', () => {
    expect(isResponseMessage({ jsonrpc: '2.0', id: 1, result: {} })).toBe(true)
    expect(isResponseMessage({ jsonrpc: '2.0', id: 1, method: 'tools/list' })).toBe(false)
  })

  it('identifies notification messages', () => {
    expect(
      isNotificationMessage({ jsonrpc: '2.0', method: 'notifications/tools/list_changed' })
    ).toBe(true)
    expect(isNotificationMessage({ jsonrpc: '2.0', id: 1, method: 'initialize' })).toBe(false)
  })
})
