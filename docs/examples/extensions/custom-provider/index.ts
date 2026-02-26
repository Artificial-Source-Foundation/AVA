/**
 * Example: Custom LLM Provider Extension
 *
 * Demonstrates how to register a custom LLM provider.
 * This example shows a simple echo provider for testing.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import type { LLMClient } from '@ava/core-v2/llm'

class EchoClient implements LLMClient {
  async *stream(messages: Array<{ role: string; content: string }>) {
    // Echo back the last user message
    const lastMessage = messages.findLast((m) => m.role === 'user')
    const content = lastMessage?.content ?? 'No message provided'

    yield { content: `Echo: ${content}` }
    yield { done: true as const }
  }
}

export function activate(api: ExtensionAPI): Disposable {
  const disposable = api.registerProvider('echo', () => new EchoClient())
  api.log.info('Echo provider registered')
  return disposable
}
