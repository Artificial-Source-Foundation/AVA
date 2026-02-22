/**
 * Mock LLM Client for CLI Testing
 * Implements LLMClient with predictable responses (no API key needed)
 */

import type { ChatMessage, LLMClient, ProviderConfig, StreamDelta } from '@ava/core'

/**
 * Mock LLM client that yields a single attempt_completion tool call.
 * Used with `--mock` flag for testing without an API key.
 */
export class MockLLMClient implements LLMClient {
  async *stream(
    messages: ChatMessage[],
    _config: ProviderConfig,
    _signal?: AbortSignal
  ): AsyncGenerator<StreamDelta, void, unknown> {
    // Extract the goal from the last user message
    const lastUserMessage = messages.filter((m) => m.role === 'user').pop()
    const goal = lastUserMessage?.content ?? 'unknown task'

    // Yield a thought
    yield {
      content: `Mock agent processing: "${goal}"`,
    }

    // Yield an attempt_completion tool call
    yield {
      content: '',
      toolUse: {
        type: 'tool_use',
        id: `mock-tc-${Date.now()}`,
        name: 'attempt_completion',
        input: {
          result: `[Mock] Task completed: ${goal}`,
        },
      },
      done: true,
    }
  }
}

/**
 * Set up mock environment for testing.
 * Sets a fake API key so getAuth() succeeds.
 */
export function setupMockEnvironment(): void {
  process.env.AVA_ANTHROPIC_API_KEY = 'mock-test-key'
}
