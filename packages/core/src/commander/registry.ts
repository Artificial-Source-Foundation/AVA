/**
 * Worker Registry
 * Manages worker registration, lookup, and phone book generation
 *
 * Based on Gemini CLI's AgentRegistry pattern with getDirectoryContext()
 */

import type { WorkerDefinition } from './types.js'

// ============================================================================
// Worker Registry
// ============================================================================

/**
 * Registry for specialized worker agents
 *
 * The registry manages worker definitions and generates the "phone book"
 * context that gets injected into the commander's system prompt.
 */
export class WorkerRegistry {
  private workers = new Map<string, WorkerDefinition>()

  /**
   * Register a worker definition
   */
  register(definition: WorkerDefinition): void {
    if (this.workers.has(definition.name)) {
      console.warn(`Worker "${definition.name}" already registered, overwriting`)
    }
    this.workers.set(definition.name, definition)
  }

  /**
   * Register multiple workers at once
   */
  registerAll(definitions: WorkerDefinition[]): void {
    for (const definition of definitions) {
      this.register(definition)
    }
  }

  /**
   * Get a worker by name
   */
  get(name: string): WorkerDefinition | undefined {
    return this.workers.get(name)
  }

  /**
   * Check if a worker exists
   */
  has(name: string): boolean {
    return this.workers.has(name)
  }

  /**
   * Get all registered workers
   */
  getAllWorkers(): WorkerDefinition[] {
    return Array.from(this.workers.values())
  }

  /**
   * Get all worker names
   */
  getWorkerNames(): string[] {
    return Array.from(this.workers.keys())
  }

  /**
   * Get the number of registered workers
   */
  get size(): number {
    return this.workers.size
  }

  /**
   * Remove a worker from the registry
   */
  unregister(name: string): boolean {
    return this.workers.delete(name)
  }

  /**
   * Clear all workers from the registry
   */
  clear(): void {
    this.workers.clear()
  }

  /**
   * Generate the "phone book" context for the commander's system prompt
   *
   * This teaches the commander which workers are available and when to use them.
   * Based on Gemini CLI's getDirectoryContext() pattern.
   */
  getDirectoryContext(): string {
    if (this.workers.size === 0) {
      return ''
    }

    const lines: string[] = ['## Available Workers', '']
    lines.push(
      'You can delegate specialized tasks to workers. Each worker has specific expertise and tool access.',
      ''
    )

    // List each worker with its capabilities
    for (const worker of this.getAllWorkers()) {
      lines.push(`### ${worker.displayName} (\`delegate_${worker.name}\`)`)
      lines.push('')
      lines.push(worker.description)
      lines.push('')
      lines.push(`**Available tools:** ${worker.tools.join(', ')}`)
      lines.push(`**Max turns:** ${worker.maxTurns ?? 10}`)
      lines.push('')
    }

    // Add delegation guidelines
    lines.push('## Delegation Guidelines', '')
    lines.push('- **Always delegate** specialized tasks to the appropriate worker')
    lines.push('- Use `delegate_coder` for any code writing or modification')
    lines.push('- Use `delegate_tester` for writing or running tests')
    lines.push('- Use `delegate_reviewer` for code review (read-only analysis)')
    lines.push('- Use `delegate_researcher` for information gathering')
    lines.push('- Use `delegate_debugger` for debugging and error fixing')
    lines.push('')
    lines.push('- Review worker output before combining results')
    lines.push('- Workers cannot delegate to other workers (recursion prevented)')
    lines.push("- Worker failures won't crash your execution - handle gracefully")
    lines.push('')

    return lines.join('\n')
  }

  /**
   * Generate a brief summary of available workers (for prompts)
   */
  getSummary(): string {
    const workers = this.getAllWorkers()
    if (workers.length === 0) {
      return 'No workers available.'
    }

    return workers.map((w) => `- ${w.displayName} (${w.name}): ${w.description}`).join('\n')
  }
}

// ============================================================================
// Factory Functions
// ============================================================================

/**
 * Create a new worker registry
 */
export function createWorkerRegistry(): WorkerRegistry {
  return new WorkerRegistry()
}
