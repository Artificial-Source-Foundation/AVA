import type {
  ToolMiddleware,
  ToolMiddlewareContext,
  ToolMiddlewareResult,
} from '@ava/core-v2/extensions'

export const READ_ONLY_TOOLS = new Set([
  'read_file',
  'glob',
  'grep',
  'ls',
  'websearch',
  'webfetch',
  'memory_read',
  'memory_list',
  'recall',
  'plan_enter',
  'lsp_hover',
  'lsp_definition',
  'lsp_references',
  'lsp_diagnostics',
  'lsp_workspace_symbols',
  'todoread',
  'question',
])

export function createSmartApproveMiddleware(): ToolMiddleware {
  return {
    name: 'smart-approve',
    priority: 2,
    async before(ctx: ToolMiddlewareContext): Promise<ToolMiddlewareResult | undefined> {
      if (!READ_ONLY_TOOLS.has(ctx.toolName)) return undefined

      return {
        args: {
          ...ctx.args,
          approved: true,
        },
      }
    },
  }
}
