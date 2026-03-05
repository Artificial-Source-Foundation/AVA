import type { Accessor } from 'solid-js'
import { createMemo } from 'solid-js'
import { type BackendType, resolveBackendType } from '../services/backend-switcher'
import { useSettings } from '../stores/settings'
import { useRustAgent } from './use-rust-agent'
import { useAgent } from './useAgent'

type AgentHook = ReturnType<typeof useAgent>

export function useBackend(): AgentHook & { backendType: Accessor<BackendType> } {
  const tsAgent = useAgent()
  const rustAgent = useRustAgent()
  const { settings } = useSettings()
  const backendType = createMemo(() => resolveBackendType(settings().agentBackend))
  const useRust = (): boolean => backendType() === 'rust'
  const passthroughInRust = new Set<PropertyKey>([
    'backendType',
    'isRunning',
    'isStreaming',
    'error',
    'run',
    'cancel',
    'stopAgent',
    'clearError',
  ])

  return new Proxy(tsAgent as AgentHook & { backendType: typeof backendType }, {
    get(target, prop: string | symbol) {
      if (prop === 'backendType') return backendType
      if (!useRust()) return target[prop as keyof AgentHook]

      if (prop === 'isRunning') return rustAgent.isRunning
      if (prop === 'isStreaming') return rustAgent.isRunning
      if (prop === 'error') return rustAgent.error
      if (prop === 'run') return rustAgent.run
      if (prop === 'cancel') return rustAgent.stop
      if (prop === 'stopAgent') return rustAgent.stop
      if (prop === 'clearError') return rustAgent.clearError

      if (typeof prop === 'string' && prop in target && !passthroughInRust.has(prop)) {
        throw new Error(`[use-backend] '${prop}' is not implemented for rust backend mode yet`)
      }

      return target[prop as keyof AgentHook]
    },
  })
}
