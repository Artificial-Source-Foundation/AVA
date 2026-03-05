import { rustAgent, rustTools } from '../../../src/services/rust-bridge'
import type { JsonValue, RustSession, ToolResult } from '../../../src/types/rust-ipc'

export class AppHarness {
  private readonly mockMode: boolean

  constructor(options?: { mock?: boolean }) {
    this.mockMode = options?.mock ?? true
  }

  async executeTool(name: string, args: Record<string, JsonValue>): Promise<ToolResult> {
    if (this.mockMode) {
      return this.mockToolExecution(name, args)
    }
    return rustTools.execute(name, args)
  }

  async runAgent(goal: string): Promise<RustSession> {
    if (this.mockMode) {
      return {
        id: `mock-${Date.now()}`,
        goal,
        completed: true,
        messages: [{ role: 'assistant', content: `Completed goal: ${goal}` }],
      }
    }
    return rustAgent.run(goal)
  }

  async readFile(path: string): Promise<string> {
    const result = await this.executeTool('read_file', { path })
    return result.content
  }

  async createFile(path: string, content: string): Promise<void> {
    await this.executeTool('create_file', { path, content })
  }

  private async mockToolExecution(
    name: string,
    args: Record<string, JsonValue>
  ): Promise<ToolResult> {
    if (name === 'read_file') {
      return { content: `mock file: ${String(args.path ?? '')}`, is_error: false }
    }
    if (name === 'create_file') {
      return { content: 'file created', is_error: false }
    }
    if (name === 'bash') {
      return { content: 'mock bash execution complete', is_error: false }
    }
    return { content: `mocked tool: ${name}`, is_error: false }
  }
}
