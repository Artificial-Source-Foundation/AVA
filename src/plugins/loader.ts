/**
 * Plugin Loader
 *
 * Manages plugin lifecycle and registration.
 */

import { pathToFileURL } from 'node:url'
import type {
  Plugin,
  PluginSource,
  LoadedPlugin,
  PluginState,
  PluginRegistry,
  PluginServices,
  PluginHookEvent,
  PluginEvent,
  PluginEventType,
  HookContext,
  HookResult,
  PluginTool,
  PluginAgent,
  PluginCommand,
} from './types.js'
import { pluginMetadataSchema } from './types.js'

// =============================================================================
// Plugin Loader
// =============================================================================

export class PluginLoader {
  private registry: PluginRegistry = {
    plugins: new Map(),
    order: [],
    hooksByEvent: new Map(),
    toolsById: new Map(),
    agentsById: new Map(),
    commandsByName: new Map(),
  }

  private eventHandlers: Array<(event: PluginEvent) => void> = []
  private getMissionFn?: (id: string) => unknown

  // ===========================================================================
  // Plugin Loading
  // ===========================================================================

  /**
   * Load a plugin from a source
   */
  async loadPlugin(source: PluginSource): Promise<string> {
    let plugin: Plugin

    try {
      switch (source.type) {
        case 'local':
          plugin = await this.loadLocalPlugin(source.path!)
          break
        case 'npm':
          plugin = await this.loadNpmPlugin(source.package!, source.version)
          break
        case 'url':
          plugin = await this.loadUrlPlugin(source.path!)
          break
        case 'inline':
          plugin = source.plugin!
          break
        default:
          throw new Error(`Unknown plugin source type: ${source.type}`)
      }

      return await this.registerPlugin(plugin)
    } catch (error) {
      const message = error instanceof Error ? error.message : String(error)
      throw new Error(`Failed to load plugin: ${message}`)
    }
  }

  /**
   * Load plugin from local file
   */
  private async loadLocalPlugin(path: string): Promise<Plugin> {
    const fileUrl = pathToFileURL(path).href
    const module = await import(fileUrl)
    return module.default || module
  }

  /**
   * Load plugin from npm package
   */
  private async loadNpmPlugin(packageName: string, _version?: string): Promise<Plugin> {
    // In a real implementation, this would use dynamic import
    // For now, we assume the package is already installed
    const module = await import(packageName)
    return module.default || module
  }

  /**
   * Load plugin from URL
   */
  private async loadUrlPlugin(url: string): Promise<Plugin> {
    const response = await fetch(url)
    if (!response.ok) {
      throw new Error(`Failed to fetch plugin from ${url}: ${response.status}`)
    }

    const code = await response.text()

    // Create a data URL and import it
    const dataUrl = `data:text/javascript;base64,${Buffer.from(code).toString('base64')}`
    const module = await import(dataUrl)
    return module.default || module
  }

  /**
   * Register a plugin
   */
  private async registerPlugin(plugin: Plugin): Promise<string> {
    // Validate metadata
    const metadata = pluginMetadataSchema.parse(plugin.metadata)
    const pluginId = metadata.id

    // Check for duplicate
    if (this.registry.plugins.has(pluginId)) {
      throw new Error(`Plugin already loaded: ${pluginId}`)
    }

    // Check dependencies
    for (const dep of metadata.dependencies) {
      if (!this.registry.plugins.has(dep)) {
        throw new Error(`Missing dependency: ${dep} required by ${pluginId}`)
      }
    }

    // Create loaded plugin entry
    const loaded: LoadedPlugin = {
      plugin,
      state: 'loaded',
      loadedAt: new Date().toISOString(),
      config: { ...plugin.defaultConfig },
    }

    // Register
    this.registry.plugins.set(pluginId, loaded)
    this.registry.order.push(pluginId)

    // Register hooks
    if (plugin.hooks) {
      for (const hook of plugin.hooks) {
        this.registerHook(pluginId, hook)
      }
    }

    // Register tools
    if (plugin.tools) {
      for (const tool of plugin.tools) {
        this.registerTool(pluginId, tool)
      }
    }

    // Register agents
    if (plugin.agents) {
      for (const agent of plugin.agents) {
        this.registerAgent(pluginId, agent)
      }
    }

    // Register commands
    if (plugin.commands) {
      for (const command of plugin.commands) {
        this.registerCommand(pluginId, command)
      }
    }

    this.emitEvent('plugin.loaded', pluginId, { metadata })

    return pluginId
  }

  /**
   * Register a hook
   */
  private registerHook(pluginId: string, hook: { event: PluginHookEvent; priority?: number; handler: (context: HookContext) => Promise<void | HookResult> }): void {
    const existing = this.registry.hooksByEvent.get(hook.event) || []
    existing.push({ pluginId, hook })

    // Sort by priority (lower = earlier)
    existing.sort((a, b) => (a.hook.priority || 100) - (b.hook.priority || 100))

    this.registry.hooksByEvent.set(hook.event, existing)
  }

  /**
   * Register a tool
   */
  private registerTool(pluginId: string, tool: PluginTool): void {
    const toolId = `${pluginId}:${tool.id}`
    if (this.registry.toolsById.has(toolId)) {
      throw new Error(`Tool already registered: ${toolId}`)
    }
    this.registry.toolsById.set(toolId, { pluginId, tool })
  }

  /**
   * Register an agent
   */
  private registerAgent(pluginId: string, agent: PluginAgent): void {
    const agentId = `${pluginId}:${agent.id}`
    if (this.registry.agentsById.has(agentId)) {
      throw new Error(`Agent already registered: ${agentId}`)
    }
    this.registry.agentsById.set(agentId, { pluginId, agent })
  }

  /**
   * Register a command
   */
  private registerCommand(pluginId: string, command: PluginCommand): void {
    if (this.registry.commandsByName.has(command.name)) {
      throw new Error(`Command already registered: ${command.name}`)
    }
    this.registry.commandsByName.set(command.name, { pluginId, command })
  }

  // ===========================================================================
  // Plugin Initialization
  // ===========================================================================

  /**
   * Initialize a plugin
   */
  async initializePlugin(pluginId: string): Promise<void> {
    const loaded = this.registry.plugins.get(pluginId)
    if (!loaded) {
      throw new Error(`Plugin not found: ${pluginId}`)
    }

    if (loaded.state !== 'loaded') {
      throw new Error(`Plugin ${pluginId} is not in loaded state: ${loaded.state}`)
    }

    loaded.state = 'initializing'

    try {
      if (loaded.plugin.initialize) {
        await loaded.plugin.initialize(this.createServices(pluginId))
      }

      loaded.state = 'active'
      this.emitEvent('plugin.initialized', pluginId, {})
    } catch (error) {
      loaded.state = 'error'
      loaded.error = error instanceof Error ? error.message : String(error)
      this.emitEvent('plugin.error', pluginId, { error: loaded.error })
      throw error
    }
  }

  /**
   * Initialize all loaded plugins
   */
  async initializeAll(): Promise<void> {
    for (const pluginId of this.registry.order) {
      const loaded = this.registry.plugins.get(pluginId)
      if (loaded?.state === 'loaded') {
        await this.initializePlugin(pluginId)
      }
    }
  }

  // ===========================================================================
  // Plugin Unloading
  // ===========================================================================

  /**
   * Unload a plugin
   */
  async unloadPlugin(pluginId: string): Promise<void> {
    const loaded = this.registry.plugins.get(pluginId)
    if (!loaded) {
      throw new Error(`Plugin not found: ${pluginId}`)
    }

    // Check for dependents
    for (const [id, other] of this.registry.plugins) {
      if (other.plugin.metadata.dependencies.includes(pluginId)) {
        throw new Error(`Cannot unload ${pluginId}: required by ${id}`)
      }
    }

    // Cleanup
    if (loaded.plugin.cleanup) {
      await loaded.plugin.cleanup()
    }

    // Remove hooks
    for (const [event, hooks] of this.registry.hooksByEvent) {
      this.registry.hooksByEvent.set(
        event,
        hooks.filter((h) => h.pluginId !== pluginId)
      )
    }

    // Remove tools
    for (const [toolId, entry] of this.registry.toolsById) {
      if (entry.pluginId === pluginId) {
        this.registry.toolsById.delete(toolId)
      }
    }

    // Remove agents
    for (const [agentId, entry] of this.registry.agentsById) {
      if (entry.pluginId === pluginId) {
        this.registry.agentsById.delete(agentId)
      }
    }

    // Remove commands
    for (const [name, entry] of this.registry.commandsByName) {
      if (entry.pluginId === pluginId) {
        this.registry.commandsByName.delete(name)
      }
    }

    // Remove from registry
    this.registry.plugins.delete(pluginId)
    this.registry.order = this.registry.order.filter((id) => id !== pluginId)

    this.emitEvent('plugin.unloaded', pluginId, {})
  }

  /**
   * Disable a plugin
   */
  disablePlugin(pluginId: string): void {
    const loaded = this.registry.plugins.get(pluginId)
    if (!loaded) {
      throw new Error(`Plugin not found: ${pluginId}`)
    }

    loaded.state = 'disabled'
    this.emitEvent('plugin.disabled', pluginId, {})
  }

  /**
   * Enable a disabled plugin
   */
  enablePlugin(pluginId: string): void {
    const loaded = this.registry.plugins.get(pluginId)
    if (!loaded) {
      throw new Error(`Plugin not found: ${pluginId}`)
    }

    if (loaded.state === 'disabled') {
      loaded.state = 'active'
    }
  }

  // ===========================================================================
  // Hook Execution
  // ===========================================================================

  /**
   * Execute hooks for an event
   */
  async executeHooks(event: PluginHookEvent, context: Omit<HookContext, 'event' | 'services'>): Promise<HookResult> {
    const hooks = this.registry.hooksByEvent.get(event) || []
    let combinedData = { ...context.data }

    for (const { pluginId, hook } of hooks) {
      const loaded = this.registry.plugins.get(pluginId)
      if (!loaded || loaded.state !== 'active') {
        continue
      }

      try {
        const fullContext: HookContext = {
          event,
          ...context,
          data: combinedData,
          services: this.createServices(pluginId),
        }

        const result = await hook.handler(fullContext)

        if (result) {
          if (result.abort) {
            return result
          }

          if (result.data) {
            combinedData = { ...combinedData, ...result.data }
          }

          if (result.continue === false) {
            break
          }
        }
      } catch (error) {
        // Log but continue
        console.error(`Hook error in plugin ${pluginId}:`, error)
      }
    }

    return { data: combinedData }
  }

  // ===========================================================================
  // Tool Execution
  // ===========================================================================

  /**
   * Execute a plugin tool
   */
  async executeTool(toolId: string, params: unknown): Promise<unknown> {
    const entry = this.registry.toolsById.get(toolId)
    if (!entry) {
      throw new Error(`Tool not found: ${toolId}`)
    }

    const loaded = this.registry.plugins.get(entry.pluginId)
    if (!loaded || loaded.state !== 'active') {
      throw new Error(`Plugin not active: ${entry.pluginId}`)
    }

    // Validate parameters
    const validated = entry.tool.parameters.parse(params)

    return entry.tool.execute(validated)
  }

  // ===========================================================================
  // Command Execution
  // ===========================================================================

  /**
   * Execute a plugin command
   */
  async executeCommand(name: string, args: unknown): Promise<unknown> {
    const entry = this.registry.commandsByName.get(name)
    if (!entry) {
      throw new Error(`Command not found: ${name}`)
    }

    const loaded = this.registry.plugins.get(entry.pluginId)
    if (!loaded || loaded.state !== 'active') {
      throw new Error(`Plugin not active: ${entry.pluginId}`)
    }

    // Validate arguments
    const validated = entry.command.args ? entry.command.args.parse(args) : args

    return entry.command.execute(validated, this.createServices(entry.pluginId))
  }

  // ===========================================================================
  // Services
  // ===========================================================================

  /**
   * Create services for a plugin
   */
  private createServices(pluginId: string): PluginServices {
    return {
      log: (level, message, data) => {
        console.log(`[${pluginId}] [${level.toUpperCase()}] ${message}`, data || '')
      },

      getConfig: <T>(key: string) => {
        const loaded = this.registry.plugins.get(pluginId)
        return loaded?.config[key] as T | undefined
      },

      setConfig: <T>(key: string, value: T) => {
        const loaded = this.registry.plugins.get(pluginId)
        if (loaded) {
          loaded.config[key] = value
        }
      },

      emit: (event, data) => {
        // Emit to general event handlers
        for (const handler of this.eventHandlers) {
          handler({
            type: 'plugin.loaded' as PluginEventType, // Generic event
            timestamp: new Date().toISOString(),
            pluginId,
            data: { event, payload: data },
          })
        }
      },

      on: (event, handler) => {
        const wrapper = (e: PluginEvent) => {
          if (e.data.event === event) {
            handler(e.data.payload)
          }
        }
        this.eventHandlers.push(wrapper)
        return () => {
          const index = this.eventHandlers.indexOf(wrapper)
          if (index >= 0) {
            this.eventHandlers.splice(index, 1)
          }
        }
      },

      getMission: (id) => {
        return this.getMissionFn?.(id)
      },

      getPlugin: (id) => {
        return this.registry.plugins.get(id)?.plugin
      },
    }
  }

  /**
   * Set mission getter function
   */
  setMissionGetter(fn: (id: string) => unknown): void {
    this.getMissionFn = fn
  }

  // ===========================================================================
  // Event System
  // ===========================================================================

  private emitEvent(type: PluginEventType, pluginId: string, data: Record<string, unknown>): void {
    const event: PluginEvent = {
      type,
      timestamp: new Date().toISOString(),
      pluginId,
      data,
    }

    for (const handler of this.eventHandlers) {
      try {
        handler(event)
      } catch {
        // Ignore handler errors
      }
    }
  }

  onEvent(handler: (event: PluginEvent) => void): () => void {
    this.eventHandlers.push(handler)
    return () => {
      const index = this.eventHandlers.indexOf(handler)
      if (index >= 0) {
        this.eventHandlers.splice(index, 1)
      }
    }
  }

  // ===========================================================================
  // Getters
  // ===========================================================================

  /**
   * Get a loaded plugin
   */
  getPlugin(pluginId: string): LoadedPlugin | undefined {
    return this.registry.plugins.get(pluginId)
  }

  /**
   * List all plugins
   */
  listPlugins(): LoadedPlugin[] {
    return this.registry.order.map((id) => this.registry.plugins.get(id)!).filter(Boolean)
  }

  /**
   * Get plugin state
   */
  getPluginState(pluginId: string): PluginState | undefined {
    return this.registry.plugins.get(pluginId)?.state
  }

  /**
   * List tools
   */
  listTools(): Array<{ id: string; pluginId: string; tool: PluginTool }> {
    return Array.from(this.registry.toolsById.entries()).map(([id, entry]) => ({
      id,
      ...entry,
    }))
  }

  /**
   * List agents
   */
  listAgents(): Array<{ id: string; pluginId: string; agent: PluginAgent }> {
    return Array.from(this.registry.agentsById.entries()).map(([id, entry]) => ({
      id,
      ...entry,
    }))
  }

  /**
   * List commands
   */
  listCommands(): Array<{ name: string; pluginId: string; command: PluginCommand }> {
    return Array.from(this.registry.commandsByName.entries()).map(([name, entry]) => ({
      name,
      ...entry,
    }))
  }

  /**
   * Get registry stats
   */
  getStats(): {
    totalPlugins: number
    activePlugins: number
    totalTools: number
    totalAgents: number
    totalCommands: number
    totalHooks: number
  } {
    const plugins = Array.from(this.registry.plugins.values())

    let totalHooks = 0
    for (const hooks of this.registry.hooksByEvent.values()) {
      totalHooks += hooks.length
    }

    return {
      totalPlugins: plugins.length,
      activePlugins: plugins.filter((p) => p.state === 'active').length,
      totalTools: this.registry.toolsById.size,
      totalAgents: this.registry.agentsById.size,
      totalCommands: this.registry.commandsByName.size,
      totalHooks,
    }
  }
}
