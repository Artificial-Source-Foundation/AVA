import type {
  AgentMode,
  Disposable,
  ExtensionAPI,
  ToolMiddlewareContext,
} from '@ava/core-v2/extensions'

const DEFAULT_N = 1
const QUALITY_N = 3

function getConfiguredN(api: ExtensionAPI): number {
  const settings = api.getSettings('agentModes') as { bestOfN?: number } | undefined
  const value = settings?.bestOfN
  if (typeof value !== 'number' || Number.isNaN(value)) {
    return DEFAULT_N
  }
  return Math.max(1, Math.floor(value))
}

export const bestOfNAgentMode: AgentMode = {
  name: 'best-of-n',
  description: 'Samples multiple candidate actions and selects the highest scoring one.',
  systemPrompt: (base) => {
    const n = base.includes('quality') ? QUALITY_N : DEFAULT_N
    if (n <= 1) {
      return 'Use default single-sample behavior unless best-of-n is enabled by settings.'
    }
    return `Generate ${n} candidate actions, score for success quality and cost, and execute only the best candidate.`
  },
}

export function registerBestOfNMode(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []

  disposables.push(api.registerAgentMode(bestOfNAgentMode))

  disposables.push(
    api.addToolMiddleware({
      name: 'best-of-n-sampler',
      priority: 15,
      async before(context: ToolMiddlewareContext) {
        const n = getConfiguredN(api)
        if (n <= 1) {
          return undefined
        }

        context.ctx.metadata?.({
          title: 'Best of N',
          metadata: {
            enabled: true,
            n,
            tool: context.toolName,
          },
        })

        return undefined
      },
    })
  )

  disposables.push(
    api.registerHook('tool:beforeExecute', async (payload) => {
      const n = getConfiguredN(api)
      if (n <= 1 || typeof payload !== 'object' || payload === null) {
        return payload
      }

      return {
        ...(payload as Record<string, unknown>),
        bestOfN: { enabled: true, n },
      }
    })
  )

  return {
    dispose() {
      for (const disposable of disposables.reverse()) {
        disposable.dispose()
      }
    },
  }
}
