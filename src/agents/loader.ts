/**
 * Delta9 Dynamic Agent Loader
 *
 * Loads agent definitions from markdown files with YAML frontmatter.
 * Allows user-defined agents in .delta9/agents/ directory.
 *
 * Pattern from: froggy markdown agents with YAML frontmatter
 *
 * Agent File Format:
 * ```markdown
 * ---
 * name: custom-reviewer
 * role: code_reviewer
 * model: claude-sonnet-4-20250514
 * tools: [read_file, grep, lint]
 * constraints:
 *   - Never approve without tests
 *   - Check for security issues
 * ---
 *
 * # System Prompt
 *
 * You are a code reviewer focused on quality and security...
 * ```
 */

import { existsSync, readdirSync, readFileSync, mkdirSync } from 'node:fs'
import { join } from 'node:path'
import { getNamedLogger } from '../lib/logger.js'
import { getDelta9Dir } from '../lib/paths.js'

const log = getNamedLogger('agent-loader')

// =============================================================================
// Types
// =============================================================================

/**
 * Dynamic agent configuration loaded from file
 */
export interface DynamicAgentConfig {
  /** Agent name (from frontmatter or filename) */
  name: string
  /** Agent role for routing */
  role: string
  /** Model to use (optional - inherits if not specified) */
  model?: string
  /** System prompt content */
  systemPrompt: string
  /** List of allowed tool names */
  tools?: string[]
  /** Constraints/rules for the agent */
  constraints?: string[]
  /** Description for agent discovery */
  description?: string
  /** File path the config was loaded from */
  filePath: string
  /** When the config was loaded */
  loadedAt: number
  /** Custom metadata */
  metadata?: Record<string, unknown>
}

/**
 * Agent loader configuration
 */
export interface AgentLoaderConfig {
  /** Project directory */
  cwd: string
  /** Agents directory name (default: 'agents') */
  agentsDirName?: string
  /** Watch for file changes */
  watchForChanges?: boolean
}

// =============================================================================
// Frontmatter Parsing
// =============================================================================

/**
 * Parse YAML frontmatter from markdown content
 */
function parseFrontmatter(content: string): { meta: Record<string, unknown>; body: string } {
  const frontmatterRegex = /^---\n([\s\S]*?)\n---\n?([\s\S]*)$/
  const match = content.match(frontmatterRegex)

  if (!match) {
    return { meta: {}, body: content }
  }

  const yamlContent = match[1]
  const body = match[2].trim()

  // Simple YAML parsing (handles strings, numbers, booleans, and arrays)
  const meta: Record<string, unknown> = {}
  const lines = yamlContent.split('\n')

  let currentKey = ''
  let currentArray: string[] | null = null

  for (const line of lines) {
    // Check for array item
    if (line.match(/^\s+-\s+/)) {
      if (currentArray) {
        const value = line.replace(/^\s+-\s+/, '').trim()
        currentArray.push(value)
      }
      continue
    }

    // Save pending array
    if (currentArray && currentKey) {
      meta[currentKey] = currentArray
      currentArray = null
    }

    const colonIndex = line.indexOf(':')
    if (colonIndex > 0) {
      currentKey = line.substring(0, colonIndex).trim()
      let value: unknown = line.substring(colonIndex + 1).trim()

      // Check for inline array [a, b, c]
      if (typeof value === 'string' && value.startsWith('[') && value.endsWith(']')) {
        value = value
          .slice(1, -1)
          .split(',')
          .map((s) => s.trim())
          .filter((s) => s.length > 0)
        meta[currentKey] = value
        continue
      }

      // Check for start of multiline array
      if (value === '') {
        currentArray = []
        continue
      }

      // Parse booleans and numbers
      if (value === 'true') value = true
      else if (value === 'false') value = false
      else if (/^\d+$/.test(value as string)) value = parseInt(value as string, 10)
      else if (/^\d+\.\d+$/.test(value as string)) value = parseFloat(value as string)

      meta[currentKey] = value
    }
  }

  // Save final pending array
  if (currentArray && currentKey) {
    meta[currentKey] = currentArray
  }

  return { meta, body }
}

// =============================================================================
// Agent Loader
// =============================================================================

/**
 * Dynamic agent loader
 *
 * Loads agents from .delta9/agents/ directory.
 * Supports hot-reloading when files change.
 */
export class AgentLoader {
  private agents: Map<string, DynamicAgentConfig> = new Map()
  private config: Required<AgentLoaderConfig>
  private agentsDir: string

  constructor(config: AgentLoaderConfig) {
    this.config = {
      cwd: config.cwd,
      agentsDirName: config.agentsDirName ?? 'agents',
      watchForChanges: config.watchForChanges ?? false,
    }
    this.agentsDir = join(getDelta9Dir(this.config.cwd), this.config.agentsDirName)
  }

  // ===========================================================================
  // Loading
  // ===========================================================================

  /**
   * Load all agents from the agents directory
   */
  loadFromDirectory(): number {
    if (!existsSync(this.agentsDir)) {
      log.debug(`Agents directory does not exist: ${this.agentsDir}`)
      return 0
    }

    const files = readdirSync(this.agentsDir).filter(
      (f) => f.endsWith('.md') || f.endsWith('.markdown')
    )

    let loaded = 0
    for (const file of files) {
      const filePath = join(this.agentsDir, file)
      const agent = this.loadAgent(filePath)
      if (agent) {
        this.agents.set(agent.name, agent)
        loaded++
      }
    }

    log.info(`Loaded ${loaded} dynamic agents from ${this.agentsDir}`)
    return loaded
  }

  /**
   * Load a single agent from file
   */
  loadAgent(filePath: string): DynamicAgentConfig | null {
    if (!existsSync(filePath)) {
      log.warn(`Agent file not found: ${filePath}`)
      return null
    }

    try {
      const content = readFileSync(filePath, 'utf-8')
      const { meta, body } = parseFrontmatter(content)

      // Extract name from frontmatter or filename
      const filename = filePath.split('/').pop()?.replace(/\.(md|markdown)$/, '') ?? 'unknown'
      const name = (meta.name as string) ?? filename

      const agent: DynamicAgentConfig = {
        name,
        role: (meta.role as string) ?? 'general',
        model: meta.model as string | undefined,
        systemPrompt: body,
        tools: meta.tools as string[] | undefined,
        constraints: meta.constraints as string[] | undefined,
        description: meta.description as string | undefined,
        filePath,
        loadedAt: Date.now(),
        metadata: meta.metadata as Record<string, unknown> | undefined,
      }

      log.debug(`Loaded agent: ${name} from ${filePath}`)
      return agent
    } catch (error) {
      log.error(`Failed to load agent from ${filePath}: ${error instanceof Error ? error.message : String(error)}`)
      return null
    }
  }

  /**
   * Reload a specific agent
   */
  reloadAgent(name: string): DynamicAgentConfig | null {
    const existing = this.agents.get(name)
    if (!existing) return null

    const reloaded = this.loadAgent(existing.filePath)
    if (reloaded) {
      this.agents.set(name, reloaded)
    }
    return reloaded
  }

  /**
   * Reload all agents
   */
  reloadAll(): number {
    this.agents.clear()
    return this.loadFromDirectory()
  }

  // ===========================================================================
  // Retrieval
  // ===========================================================================

  /**
   * Get an agent by name
   */
  getAgent(name: string): DynamicAgentConfig | null {
    return this.agents.get(name) ?? null
  }

  /**
   * Get agents by role
   */
  getAgentsByRole(role: string): DynamicAgentConfig[] {
    return Array.from(this.agents.values()).filter((a) => a.role === role)
  }

  /**
   * List all loaded agents
   */
  listAgents(): string[] {
    return Array.from(this.agents.keys())
  }

  /**
   * Get all agents
   */
  getAllAgents(): DynamicAgentConfig[] {
    return Array.from(this.agents.values())
  }

  /**
   * Check if an agent exists
   */
  hasAgent(name: string): boolean {
    return this.agents.has(name)
  }

  // ===========================================================================
  // Management
  // ===========================================================================

  /**
   * Unload an agent
   */
  unloadAgent(name: string): boolean {
    return this.agents.delete(name)
  }

  /**
   * Clear all loaded agents
   */
  clear(): void {
    this.agents.clear()
  }

  /**
   * Get the agents directory path
   */
  getAgentsDir(): string {
    return this.agentsDir
  }

  /**
   * Ensure the agents directory exists
   */
  ensureAgentsDir(): void {
    if (!existsSync(this.agentsDir)) {
      mkdirSync(this.agentsDir, { recursive: true })
      log.debug(`Created agents directory: ${this.agentsDir}`)
    }
  }

  // ===========================================================================
  // Stats
  // ===========================================================================

  /**
   * Get loader statistics
   */
  getStats(): {
    totalAgents: number
    agentsByRole: Record<string, number>
    agentsDir: string
  } {
    const agentsByRole: Record<string, number> = {}

    for (const agent of this.agents.values()) {
      agentsByRole[agent.role] = (agentsByRole[agent.role] ?? 0) + 1
    }

    return {
      totalAgents: this.agents.size,
      agentsByRole,
      agentsDir: this.agentsDir,
    }
  }
}

// =============================================================================
// Singleton & Factory
// =============================================================================

let loaderInstance: AgentLoader | null = null

/**
 * Get or create the agent loader
 */
export function getAgentLoader(cwd: string): AgentLoader {
  if (!loaderInstance || loaderInstance.getAgentsDir() !== join(getDelta9Dir(cwd), 'agents')) {
    loaderInstance = new AgentLoader({ cwd })
    loaderInstance.loadFromDirectory()
  }
  return loaderInstance
}

/**
 * Clear the agent loader (for testing)
 */
export function clearAgentLoader(): void {
  if (loaderInstance) {
    loaderInstance.clear()
    loaderInstance = null
  }
}

/**
 * Create a new agent loader (for testing)
 */
export function createAgentLoader(config: AgentLoaderConfig): AgentLoader {
  return new AgentLoader(config)
}

// =============================================================================
// Utility Functions
// =============================================================================

/**
 * Format agent config for use in prompts
 */
export function formatAgentPrompt(agent: DynamicAgentConfig): string {
  const parts: string[] = []

  // Add system prompt
  parts.push(agent.systemPrompt)

  // Add constraints if any
  if (agent.constraints && agent.constraints.length > 0) {
    parts.push('\n## Constraints\n')
    for (const constraint of agent.constraints) {
      parts.push(`- ${constraint}`)
    }
  }

  return parts.join('\n')
}

/**
 * Get available tools for an agent
 */
export function getAgentTools(agent: DynamicAgentConfig): string[] {
  return agent.tools ?? []
}

/**
 * Check if agent has access to a specific tool
 */
export function agentHasTool(agent: DynamicAgentConfig, toolName: string): boolean {
  if (!agent.tools) return true // No restrictions
  return agent.tools.includes(toolName)
}
