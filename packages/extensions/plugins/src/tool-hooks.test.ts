import { createMockExtensionAPI } from '@ava/core-v2/__test-utils__/mock-extension-api'
import type { ToolDefinition } from '@ava/core-v2/llm'
import { describe, expect, it } from 'vitest'
import { activateToolHooks, pluginToolHooksApi } from './tool-hooks.js'

describe('plugin tool hooks', () => {
  it('allows plugins to mutate tool descriptions', async () => {
    const { api } = createMockExtensionAPI()
    const activation = activateToolHooks(api)
    const hookDisposable = pluginToolHooksApi.addHook('tool:describe', (tools) =>
      tools.map((tool) => ({ ...tool, description: `${tool.description} [enhanced]` }))
    )

    const tools: ToolDefinition[] = [
      {
        name: 'read_file',
        description: 'Read file',
        input_schema: { type: 'object', properties: {} },
      },
    ]
    const result = await api.callHook('tool:describe', tools, tools)
    expect(result.output[0]?.description).toContain('[enhanced]')

    hookDisposable.dispose()
    activation.dispose()
  })
})
