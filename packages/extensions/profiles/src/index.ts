import type { Disposable, ExtensionAPI, ToolMiddlewareContext } from '@ava/core-v2/extensions'
import type { ChatMessage } from '@ava/core-v2/llm'
import { defineTool } from '@ava/core-v2/tools'
import { z } from 'zod'
import { listProfiles, loadProfile, saveProfile } from './storage.js'
import { type AgentProfile, BUILT_IN_PROFILES } from './types.js'

let activeProfile: AgentProfile | null = null

function findBuiltIn(name: string): AgentProfile | null {
  return BUILT_IN_PROFILES.find((profile) => profile.name === name) ?? null
}

const profileListTool = defineTool({
  name: 'profile_list',
  description: 'List built-in and saved agent profiles.',
  schema: z.object({}),
  async execute() {
    const saved = await listProfiles()
    const builtIn = BUILT_IN_PROFILES.map((profile) => profile.name)
    const names = Array.from(new Set([...builtIn, ...saved])).sort((a, b) => a.localeCompare(b))

    return {
      success: true,
      output: names.join('\n'),
      metadata: { names, builtIn },
    }
  },
})

const profileLoadTool = defineTool({
  name: 'profile_load',
  description: 'Load and activate an agent profile by name.',
  schema: z.object({
    name: z.string().min(1),
  }),
  async execute(input) {
    const builtIn = findBuiltIn(input.name)
    const loaded = builtIn ?? (await loadProfile(input.name))
    if (!loaded) {
      return {
        success: false,
        output: '',
        error: `Profile '${input.name}' not found`,
      }
    }

    activeProfile = loaded

    return {
      success: true,
      output: `Loaded profile '${loaded.name}'`,
      metadata: {
        profile: loaded,
      },
    }
  },
})

const profileSaveTool = defineTool({
  name: 'profile_save',
  description: 'Save or update an agent profile.',
  schema: z.object({
    name: z.string().min(1),
    model: z.string().min(1),
    tools: z.array(z.string()),
    instructions: z.string(),
    skills: z.array(z.string()),
  }),
  async execute(input) {
    const profile: AgentProfile = {
      name: input.name,
      model: input.model,
      tools: input.tools,
      instructions: input.instructions,
      skills: input.skills,
    }

    await saveProfile(profile)

    return {
      success: true,
      output: `Saved profile '${profile.name}'`,
      metadata: { profile },
    }
  },
})

function isToolAllowed(profile: AgentProfile, toolName: string): boolean {
  if (profile.tools.includes('*')) {
    return true
  }
  return profile.tools.includes(toolName)
}

export function activate(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []

  activeProfile = null

  for (const tool of [profileListTool, profileLoadTool, profileSaveTool]) {
    disposables.push(api.registerTool(tool))
  }

  disposables.push(
    api.addToolMiddleware({
      name: 'profile-tool-filter',
      priority: 80,
      async before(context: ToolMiddlewareContext) {
        if (!activeProfile) {
          return undefined
        }
        if (isToolAllowed(activeProfile, context.toolName)) {
          return undefined
        }

        return {
          blocked: true,
          reason: `Tool '${context.toolName}' is not allowed for active profile '${activeProfile.name}'`,
        }
      },
    })
  )

  disposables.push(
    api.registerHook('history:process', async (history): Promise<ChatMessage[]> => {
      const messages = Array.isArray(history) ? (history as ChatMessage[]) : []
      if (!activeProfile) {
        return messages
      }

      const header = `[Active profile: ${activeProfile.name}] model=${activeProfile.model}`
      const instructions = `${header}\n${activeProfile.instructions}`

      const alreadyInjected = messages.some(
        (message) =>
          message.role === 'system' &&
          typeof message.content === 'string' &&
          message.content.startsWith(header)
      )
      if (alreadyInjected) {
        return messages
      }

      return [{ role: 'system', content: instructions }, ...messages]
    })
  )

  return {
    dispose() {
      activeProfile = null
      for (const disposable of disposables.reverse()) {
        disposable.dispose()
      }
    },
  }
}
