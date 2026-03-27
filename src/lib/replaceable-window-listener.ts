const LISTENER_REGISTRY_KEY = '__avaReplaceableWindowListeners__'

type ListenerCleanup = () => void

type WindowWithListenerRegistry = Window & {
  [LISTENER_REGISTRY_KEY]?: Map<string, ListenerCleanup>
}

export function installReplaceableWindowListener(
  key: string,
  install: (target: Window) => ListenerCleanup
): void {
  if (typeof window === 'undefined') return

  const host = window as WindowWithListenerRegistry
  const registry = host[LISTENER_REGISTRY_KEY] ?? new Map<string, ListenerCleanup>()
  host[LISTENER_REGISTRY_KEY] = registry

  registry.get(key)?.()
  registry.set(key, install(window))
}
