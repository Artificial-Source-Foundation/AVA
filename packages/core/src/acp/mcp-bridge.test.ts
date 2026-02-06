/**
 * ACP MCP Bridge Tests
 */

import { beforeEach, describe, expect, it, vi } from 'vitest'
import { MCPClientManager } from '../mcp/client.js'
import { AcpMCPBridge, createAcpMCPBridge } from './mcp-bridge.js'
import type { AcpMCPServerConfig } from './types.js'

// ============================================================================
// Helpers
// ============================================================================

function createMockMCPClient(): MCPClientManager {
  const client = new MCPClientManager('test', '0.1.0')

  // Mock connect and discoverTools to avoid actual network
  vi.spyOn(client, 'connect').mockResolvedValue(undefined)
  vi.spyOn(client, 'disconnect').mockResolvedValue(undefined)
  vi.spyOn(client, 'discoverTools').mockResolvedValue([])
  vi.spyOn(client, 'getAllTools').mockReturnValue([])

  return client
}

function sampleConfig(name = 'test-server'): AcpMCPServerConfig {
  return {
    name,
    transport: 'stdio',
    command: 'npx',
    args: ['@example/mcp-server'],
  }
}

// ============================================================================
// Tests
// ============================================================================

describe('AcpMCPBridge', () => {
  let bridge: AcpMCPBridge
  let mcpClient: MCPClientManager

  beforeEach(() => {
    mcpClient = createMockMCPClient()
    bridge = new AcpMCPBridge(mcpClient)
  })

  describe('connectServers', () => {
    it('should connect multiple servers', async () => {
      const configs: AcpMCPServerConfig[] = [sampleConfig('server-1'), sampleConfig('server-2')]

      const connected = await bridge.connectServers(configs)

      expect(connected).toEqual(['server-1', 'server-2'])
      expect(mcpClient.connect).toHaveBeenCalledTimes(2)
      expect(mcpClient.discoverTools).toHaveBeenCalledTimes(2)
    })

    it('should handle partial failures', async () => {
      vi.spyOn(mcpClient, 'connect')
        .mockResolvedValueOnce(undefined)
        .mockRejectedValueOnce(new Error('connection failed'))

      const configs: AcpMCPServerConfig[] = [sampleConfig('ok-server'), sampleConfig('bad-server')]

      const connected = await bridge.connectServers(configs)

      expect(connected).toEqual(['ok-server'])
    })

    it('should return empty array for all failures', async () => {
      vi.spyOn(mcpClient, 'connect').mockRejectedValue(new Error('fail'))

      const connected = await bridge.connectServers([sampleConfig('fail')])
      expect(connected).toEqual([])
    })
  })

  describe('connectServer', () => {
    it('should connect a single server', async () => {
      await bridge.connectServer(sampleConfig('single'))

      expect(bridge.isConnected('single')).toBe(true)
      expect(mcpClient.connect).toHaveBeenCalledWith(
        expect.objectContaining({
          name: 'single',
          type: 'stdio',
          command: 'npx',
        })
      )
    })

    it('should throw on connection failure', async () => {
      vi.spyOn(mcpClient, 'connect').mockRejectedValue(new Error('refused'))

      await expect(bridge.connectServer(sampleConfig('fail'))).rejects.toThrow('refused')
    })

    it('should normalize SSE config', async () => {
      await bridge.connectServer({
        name: 'sse-server',
        transport: 'sse',
        url: 'http://localhost:3000/sse',
      })

      expect(mcpClient.connect).toHaveBeenCalledWith(
        expect.objectContaining({
          name: 'sse-server',
          type: 'sse',
          url: 'http://localhost:3000/sse',
        })
      )
    })
  })

  describe('disconnectServer', () => {
    it('should disconnect a server', async () => {
      await bridge.connectServer(sampleConfig('disc'))
      expect(bridge.isConnected('disc')).toBe(true)

      await bridge.disconnectServer('disc')
      expect(bridge.isConnected('disc')).toBe(false)
    })
  })

  describe('disconnectAll', () => {
    it('should disconnect all servers', async () => {
      await bridge.connectServer(sampleConfig('a'))
      await bridge.connectServer(sampleConfig('b'))

      await bridge.disconnectAll()

      expect(bridge.getConnectedServers()).toEqual([])
    })
  })

  describe('getConnectedServers', () => {
    it('should return connected server names', async () => {
      await bridge.connectServer(sampleConfig('x'))
      await bridge.connectServer(sampleConfig('y'))

      expect(bridge.getConnectedServers()).toEqual(['x', 'y'])
    })

    it('should return empty when none connected', () => {
      expect(bridge.getConnectedServers()).toEqual([])
    })
  })

  describe('getTools', () => {
    it('should return tools from MCP client', () => {
      expect(bridge.getTools()).toEqual([])
    })
  })

  describe('getMCPClient', () => {
    it('should return the underlying client', () => {
      expect(bridge.getMCPClient()).toBe(mcpClient)
    })
  })

  describe('dispose', () => {
    it('should disconnect all on dispose', async () => {
      await bridge.connectServer(sampleConfig('z'))

      await bridge.dispose()

      expect(bridge.getConnectedServers()).toEqual([])
    })

    it('should reject operations after dispose', async () => {
      await bridge.dispose()
      await expect(bridge.connectServer(sampleConfig('post'))).rejects.toThrow('disposed')
    })

    it('should handle double dispose', async () => {
      await bridge.dispose()
      await expect(bridge.dispose()).resolves.toBeUndefined()
    })
  })

  describe('factory', () => {
    it('should create bridge with factory', () => {
      const b = createAcpMCPBridge(mcpClient)
      expect(b).toBeInstanceOf(AcpMCPBridge)
    })
  })
})
