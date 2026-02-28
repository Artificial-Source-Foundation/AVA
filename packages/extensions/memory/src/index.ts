/**
 * Memory extension — persistent cross-session memory.
 *
 * Provides 4 tools (memory_read/write/list/delete) and injects
 * stored memories into the system prompt via the prompt:build event.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { MemoryStore } from './store.js'
import { createMemoryTools } from './tools.js'

export function activate(api: ExtensionAPI): Disposable {
  const store = new MemoryStore(api.storage)
  const disposables: Disposable[] = []

  // Register memory tools
  const tools = createMemoryTools(store)
  for (const tool of tools) {
    disposables.push(api.registerTool(tool))
  }

  // Inject memories into system prompt
  disposables.push(
    api.on('prompt:build', (data) => {
      const ctx = data as { sections: string[] }
      void store.buildPromptSection().then((section) => {
        if (section) ctx.sections.push(section)
      })
    })
  )

  api.log.debug(`Memory extension activated (${tools.length} tools)`)

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
