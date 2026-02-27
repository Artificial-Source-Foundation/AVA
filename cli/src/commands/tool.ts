/**
 * Tool Command
 * Execute individual tools from the CLI
 *
 * Usage:
 *   ava tool list                         - List all registered tools
 *   ava tool info <name>                  - Show tool schema/description
 *   ava tool <name> --arg value           - Execute a tool
 */

import type { ToolContext, ToolDefinition } from '@ava/core-v2'
import { executeTool, getToolDefinitions, registerCoreTools } from '@ava/core-v2'

export async function runToolCommand(args: string[]): Promise<void> {
  // Ensure core tools are registered before any tool operations
  registerCoreTools()

  const subcommand = args[0]

  if (!subcommand) {
    printToolHelp()
    return
  }

  switch (subcommand) {
    case 'list':
      listTools()
      return

    case 'info': {
      const toolName = args[1]
      if (!toolName) {
        console.error('Usage: ava tool info <name>')
        process.exit(1)
      }
      showToolInfo(toolName)
      return
    }

    default:
      // Treat subcommand as tool name
      await executeToolCommand(subcommand, args.slice(1))
  }
}

function listTools(): void {
  const definitions = getToolDefinitions()

  console.log(`\nRegistered Tools (${definitions.length}):\n`)

  // Group by category for readability
  const maxNameLen = Math.max(...definitions.map((d) => d.name.length))

  for (const def of definitions.sort((a, b) => a.name.localeCompare(b.name))) {
    const paddedName = def.name.padEnd(maxNameLen + 2)
    const desc = def.description.split('\n')[0].slice(0, 60)
    console.log(`  ${paddedName}${desc}`)
  }

  console.log('')
}

function showToolInfo(toolName: string): void {
  const definitions = getToolDefinitions()
  const def = definitions.find((d) => d.name === toolName)

  if (!def) {
    console.error(`Tool "${toolName}" not found.`)
    console.error(`Run "ava tool list" to see available tools.`)
    process.exit(1)
  }

  console.log(`\nTool: ${def.name}`)
  console.log(`Description: ${def.description}`)
  console.log('')
  console.log('Input Schema:')
  console.log(JSON.stringify(def.input_schema, null, 2))
  console.log('')
}

async function executeToolCommand(toolName: string, args: string[]): Promise<void> {
  // Verify tool exists
  const definitions = getToolDefinitions()
  if (!definitions.find((d: ToolDefinition) => d.name === toolName)) {
    console.error(`Tool "${toolName}" not found. Run "ava tool list" to see available tools.`)
    process.exit(1)
  }

  // Parse --key value pairs into params object
  const params = parseToolArgs(args)

  // Create tool context
  const ac = new AbortController()
  const ctx: ToolContext = {
    sessionId: `cli-${Date.now()}`,
    workingDirectory: (params.cwd as string) ?? process.cwd(),
    signal: ac.signal,
  }

  // Remove cwd from params (it's a context property, not a tool param)
  delete params.cwd

  process.on('SIGINT', () => ac.abort())

  const startTime = Date.now()
  try {
    const result = await executeTool(toolName, params, ctx)
    const durationMs = Date.now() - startTime

    if (result.success) {
      console.log(result.output)
      if (result.metadata) {
        console.log('')
        console.log(`Metadata: ${JSON.stringify(result.metadata)}`)
      }
      console.log('')
      console.log(`OK (${durationMs}ms)`)
    } else {
      console.error(`FAILED: ${result.output}`)
      if (result.error) {
        console.error(`Error: ${result.error}`)
      }
      console.error(`(${durationMs}ms)`)
      process.exit(1)
    }
  } catch (err) {
    const message = err instanceof Error ? err.message : String(err)
    console.error(`Error executing ${toolName}: ${message}`)
    process.exit(1)
  }
}

function parseToolArgs(args: string[]): Record<string, unknown> {
  const params: Record<string, unknown> = {}

  for (let i = 0; i < args.length; i += 1) {
    const arg = args[i]

    if (arg.startsWith('--')) {
      const key = arg.slice(2)
      const value = args[i + 1]

      if (value === undefined || value.startsWith('--')) {
        // Boolean flag
        params[key] = true
      } else {
        // Try to parse as JSON for complex values, otherwise use string
        try {
          params[key] = JSON.parse(value)
        } catch {
          params[key] = value
        }
        i += 1
      }
    }
  }

  return params
}

function printToolHelp(): void {
  console.log(`
AVA Tool - Execute individual tools

USAGE:
  ava tool list                     List all registered tools
  ava tool info <name>              Show tool schema and description
  ava tool <name> --arg value       Execute a tool with arguments

ARGUMENTS:
  Tool arguments are passed as --key value pairs.
  Values are parsed as JSON if possible, otherwise treated as strings.

EXAMPLES:
  ava tool list
  ava tool info read_file
  ava tool read_file --path README.md
  ava tool glob --pattern "src/**/*.ts"
  ava tool grep --pattern "export function" --path packages/core/src/
  ava tool bash --command "ls -la"
  ava tool ls --path src/
`)
}
