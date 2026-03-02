/**
 * Memory tools smoke test — all 4 memory tools.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { MemoryStore } from './store.js'
import { createMemoryTools } from './tools.js'

describe('Memory tools smoke test', () => {
  function setup() {
    const { api } = createMockExtensionAPI()
    const store = new MemoryStore(api.storage)
    const tools = createMemoryTools(store)
    return { tools, store }
  }

  it('creates 4 memory tools', () => {
    const { tools } = setup()
    expect(tools).toHaveLength(4)
    const names = tools.map((t) => t.definition.name)
    expect(names).toContain('memory_write')
    expect(names).toContain('memory_read')
    expect(names).toContain('memory_list')
    expect(names).toContain('memory_delete')
  })

  it('all tools have valid definitions', () => {
    const { tools } = setup()
    for (const tool of tools) {
      expect(tool.definition.name).toBeTruthy()
      expect(tool.definition.description).toBeTruthy()
      expect(tool.definition.input_schema).toBeTruthy()
      expect(tool.definition.input_schema.type).toBe('object')
    }
  })

  it('memory_write → memory_read → memory_list → memory_delete cycle', async () => {
    const { tools } = setup()
    const [writeTool, readTool, listTool, deleteTool] = tools

    // Write
    const writeResult = await writeTool.execute({ key: 'test-key', value: 'test-value' })
    expect(writeResult.success).toBe(true)
    expect(writeResult.output).toContain('test-key')

    // Read
    const readResult = await readTool.execute({ key: 'test-key' })
    expect(readResult.success).toBe(true)
    expect(readResult.output).toContain('test-value')

    // List
    const listResult = await listTool.execute({})
    expect(listResult.success).toBe(true)
    expect(listResult.output).toContain('test-key')

    // Delete
    const deleteResult = await deleteTool.execute({ key: 'test-key' })
    expect(deleteResult.success).toBe(true)

    // Verify deleted
    const readAgain = await readTool.execute({ key: 'test-key' })
    expect(readAgain.success).toBe(false)
  })

  it('memory_read returns error for missing key', async () => {
    const { tools } = setup()
    const readTool = tools[1]
    const result = await readTool.execute({ key: 'nonexistent' })
    expect(result.success).toBe(false)
  })

  it('memory_list filters by category', async () => {
    const { tools } = setup()
    const [writeTool, , listTool] = tools

    await writeTool.execute({ key: 'k1', value: 'v1', category: 'project' })
    await writeTool.execute({ key: 'k2', value: 'v2', category: 'debug' })

    const projectList = await listTool.execute({ category: 'project' })
    expect(projectList.output).toContain('k1')
    expect(projectList.output).not.toContain('k2')
  })
})
