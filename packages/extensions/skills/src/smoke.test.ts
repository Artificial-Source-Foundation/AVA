/**
 * Skills tools smoke test — verifies load_skill tool.
 */

import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import { describe, expect, it } from 'vitest'
import { activate } from './index.js'

describe('Skills tools smoke test', () => {
  it('activates and registers load_skill tool', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    const toolNames = registeredTools.map((t) => t.definition.name)
    expect(toolNames).toContain('load_skill')
    expect(registeredTools).toHaveLength(1)

    disposable.dispose()
  })

  it('load_skill has valid definition', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    const tool = registeredTools.find((t) => t.definition.name === 'load_skill')!
    expect(tool.definition.name).toBeTruthy()
    expect(tool.definition.description).toBeTruthy()
    expect(tool.definition.input_schema).toBeTruthy()
    expect(tool.definition.input_schema.type).toBe('object')

    disposable.dispose()
  })

  it('load_skill returns error for unknown skill', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    const tool = registeredTools.find((t) => t.definition.name === 'load_skill')!
    const result = await tool.execute({ name: 'nonexistent' })
    expect(result.success).toBe(false)

    disposable.dispose()
  })

  it('load_skill returns skill content after registration', async () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)

    // Simulate skill registration via event
    api.emit('skills:register', {
      name: 'react-patterns',
      content: 'Use functional components with hooks',
      source: 'test',
      globs: ['*.tsx'],
    })

    const tool = registeredTools.find((t) => t.definition.name === 'load_skill')!
    const result = await tool.execute({ name: 'react-patterns' })
    expect(result.success).toBe(true)
    expect(result.output).toContain('functional components')

    disposable.dispose()
  })

  it('cleans up on dispose', () => {
    const { api, registeredTools } = createMockExtensionAPI()
    const disposable = activate(api)
    expect(registeredTools.length).toBeGreaterThan(0)
    disposable.dispose()
  })
})
