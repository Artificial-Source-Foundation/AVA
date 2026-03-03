import type {
  AgentMode,
  Disposable,
  ExtensionAPI,
  ToolMiddlewareContext,
} from '@ava/core-v2/extensions'
import type { ToolResult } from '@ava/core-v2/tools'
import { type SamplingCandidate, scoreCandidate, selectBestCandidate } from './sampler.js'

const DEFAULT_N = 1

function getConfiguredN(api: ExtensionAPI): number {
  const settings = api.getSettings('agentModes') as { bestOfN?: number } | undefined
  const value = settings?.bestOfN
  if (typeof value !== 'number' || Number.isNaN(value)) {
    return DEFAULT_N
  }
  return Math.max(1, Math.floor(value))
}

function resultToCandidate(id: string, result: ToolResult): SamplingCandidate {
  return {
    id,
    success: result.success !== false,
    output: typeof result.output === 'string' ? result.output : JSON.stringify(result.output),
    estimatedCost: 0,
  }
}

export const bestOfNAgentMode: AgentMode = {
  name: 'best-of-n',
  description: 'Samples multiple candidate actions and selects the highest scoring one.',
  systemPrompt: () => {
    return [
      'You are operating in best-of-N sampling mode.',
      'For each action, the system may run multiple candidates and select the best one.',
      'Focus on producing high-quality, correct outputs on each attempt.',
    ].join(' ')
  },
}

export function registerBestOfNMode(api: ExtensionAPI): Disposable {
  const disposables: Disposable[] = []

  disposables.push(api.registerAgentMode(bestOfNAgentMode))

  disposables.push(
    api.addToolMiddleware({
      name: 'best-of-n-sampler',
      priority: 15,
      async after(context: ToolMiddlewareContext, result: ToolResult) {
        const n = getConfiguredN(api)
        if (n <= 1) return undefined

        // Score the result using the sampler
        const candidate = resultToCandidate('result-0', result)
        const score = scoreCandidate(candidate)

        // Emit sampling event for observability
        api.emit('bestOfN:scored', {
          tool: context.toolName,
          n,
          score,
          success: candidate.success,
        })

        // Annotate the result with scoring metadata.
        // When the agent loop supports multi-execution, the middleware
        // will collect N results here and use selectBestCandidate().
        return {
          result: {
            ...result,
            metadata: {
              ...result.metadata,
              bestOfN: {
                enabled: true,
                configuredN: n,
                sampled: 1,
                score,
                selected: candidate.id,
              },
            },
          },
        }
      },
    })
  )

  // Register hook for agent loop integration.
  // When the loop calls tool:beforeExecute with multiple candidates,
  // use the sampler to select the best one.
  disposables.push(
    api.registerHook('tool:beforeExecute', async (payload) => {
      const n = getConfiguredN(api)
      if (n <= 1 || typeof payload !== 'object' || payload === null) {
        return payload
      }

      const p = payload as Record<string, unknown>
      const candidates = p.candidates as SamplingCandidate[] | undefined

      // If the loop provides multiple candidates, select the best
      if (Array.isArray(candidates) && candidates.length > 1) {
        const best = selectBestCandidate(candidates)
        api.emit('bestOfN:selected', {
          n,
          total: candidates.length,
          selectedId: best.id,
          score: scoreCandidate(best),
        })
        return { ...p, selectedCandidate: best }
      }

      return { ...p, bestOfN: { enabled: true, n } }
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
