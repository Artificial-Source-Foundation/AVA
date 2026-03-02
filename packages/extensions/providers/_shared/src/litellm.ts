/**
 * LiteLLM-specific utilities.
 *
 * Some LiteLLM backends require at least one tool definition in the request
 * body, even when the model does not support tool use. This helper injects
 * a no-op tool when the tools array is empty.
 */

const NOOP_TOOL = {
  type: 'function' as const,
  function: {
    name: '_noop',
    description: 'No-op placeholder tool (required by LiteLLM proxy)',
    parameters: {
      type: 'object',
      properties: {},
      required: [],
    },
  },
}

/**
 * Inject a no-op tool definition if the request body has no tools.
 * Returns the body unchanged if tools already exist.
 */
export function injectNoopToolIfNeeded(body: Record<string, unknown>): Record<string, unknown> {
  const tools = body.tools as unknown[] | undefined

  if (tools && tools.length > 0) return body

  return {
    ...body,
    tools: [NOOP_TOOL],
  }
}
