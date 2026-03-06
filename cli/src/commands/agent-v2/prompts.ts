import * as path from 'node:path'
import { onEvent } from '@ava/core-v2/extensions'
import { importWithDistFallback } from './runtime.js'
import type { AgentV2Options, PromptsModule } from './types.js'

export async function loadPromptsModule(
  extensionsDir: string,
  cwd: string
): Promise<PromptsModule | null> {
  try {
    const promptsModule = (await importWithDistFallback(
      path.resolve(extensionsDir, 'prompts/src/builder.ts'),
      path.resolve(extensionsDir, 'dist/prompts/src/builder.js')
    )) as unknown as PromptsModule

    promptsModule.addPromptSection({
      name: 'cwd',
      priority: 100,
      content: `Working directory: ${cwd}`,
    })

    return promptsModule
  } catch {
    return null
  }
}

export function createInstructionsReadyPromise(
  promptsModule: PromptsModule | null,
  options: AgentV2Options
): Promise<void> {
  if (!promptsModule) {
    return Promise.resolve()
  }

  const pm = promptsModule
  return new Promise<void>((resolve) => {
    const timeout = setTimeout(resolve, 1000)
    onEvent('instructions:loaded', (data) => {
      clearTimeout(timeout)
      const { merged, count } = data as { merged: string; count: number }
      if (merged) {
        pm.addPromptSection({
          name: 'project-instructions',
          content: `# Project Instructions\n\n${merged}`,
          priority: 5,
        })
        if (options.verbose) {
          process.stderr.write(
            `[agent-v2] Loaded ${count} instruction file(s) into system prompt\n`
          )
        }
      }
      resolve()
    })
  })
}
