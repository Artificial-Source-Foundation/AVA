import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { ToolDefinition } from '@ava/core-v2/llm'

export type ToolDescribeHook = (
  tools: ToolDefinition[]
) => ToolDefinition[] | Promise<ToolDefinition[]>

const toolDescribeHooks = new Set<ToolDescribeHook>()

export const pluginToolHooksApi = {
  addHook(name: 'tool:describe', handler: ToolDescribeHook): Disposable {
    if (name !== 'tool:describe') {
      return { dispose() {} }
    }

    toolDescribeHooks.add(handler)
    return {
      dispose() {
        toolDescribeHooks.delete(handler)
      },
    }
  },
}

export function activateToolHooks(api: ExtensionAPI): Disposable {
  const disposable = api.registerHook<ToolDefinition[], ToolDefinition[]>(
    'tool:describe',
    async (_input, tools) => {
      let current = tools
      for (const hook of toolDescribeHooks) {
        // eslint-disable-next-line no-await-in-loop
        current = await hook(current)
      }
      return current
    }
  )

  return {
    dispose() {
      disposable.dispose()
      toolDescribeHooks.clear()
    },
  }
}
