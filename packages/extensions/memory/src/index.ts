/**
 * Memory extension — persistent cross-session memory.
 *
 * Provides 4 tools (memory_read/write/list/delete) and injects
 * stored memories into the system prompt via the prompt:build event.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { registerAutoLearn } from './auto-learn.js'
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
    api.on('prompt:build', async (data) => {
      const ctx = data as { sections: string[] }
      const section = await store.buildPromptSection()
      if (section) ctx.sections.push(section)
    })
  )

  // Register auto-learning from agent sessions
  disposables.push(registerAutoLearn(api))
  disposables.push(
    api.on('memory:auto-learned', async (data) => {
      try {
        const event = data as {
          key?: string
          value?: string
          category?: 'learned-patterns' | 'user-preferences' | 'project-conventions'
        }
        if (!event.key || !event.value) return

        const category =
          event.category === 'user-preferences'
            ? 'preferences'
            : event.category === 'project-conventions'
              ? 'project'
              : 'context'

        await store.write(event.key, event.value, category)
      } catch (error) {
        api.log.warn(`Failed to persist auto-learned memory: ${String(error)}`)
      }
    })
  )

  // Register periodic memory persistence via scheduler
  api.emit('scheduler:register', {
    id: 'memory-persist',
    interval: 5 * 60 * 1000,
    handler: () => store.flush(),
  })
  api.log.debug('Memory persistence scheduled (every 5 minutes)')

  api.log.debug(`Memory extension activated (${tools.length} tools, auto-learn enabled)`)

  return {
    dispose() {
      for (const d of disposables) d.dispose()
    },
  }
}
