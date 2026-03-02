#!/usr/bin/env node

/**
 * AVA CLI Entry Point
 *
 * Usage:
 *   ava                 - Interactive TUI mode (future)
 *   ava auth            - Manage authentication
 *   ava plugin          - Plugin development commands
 *   ava run "<goal>"    - Execute agent loop
 *   ava tool <name>     - Execute a single tool
 *   ava validate <files> - Run validation pipeline
 *   ava --version       - Show version
 *   ava --help          - Show help
 */

import * as os from 'node:os'
import * as path from 'node:path'
import { setPlatform } from '@ava/core-v2'
import { createNodePlatform } from '@ava/platform-node'
import { runAgentCommand } from './commands/agent.js'
import { runAgentV2Command } from './commands/agent-v2.js'
import { runAuthCommand } from './commands/auth.js'
import { runPluginCommand } from './commands/plugin.js'
import { runRunCommand } from './commands/run.js'
import { runToolCommand } from './commands/tool.js'
import { runValidateCommand } from './commands/validate.js'

const VERSION = '0.1.0'

async function main() {
  const args = process.argv.slice(2)

  // Parse arguments
  if (args.includes('--version') || args.includes('-v')) {
    console.log(`ava v${VERSION}`)
    process.exit(0)
  }

  if (args.includes('--help') || args.includes('-h')) {
    printHelp()
    process.exit(0)
  }

  // Initialize platform
  const dbPath = path.join(os.homedir(), '.ava', 'data.db')
  const platform = createNodePlatform(dbPath)
  setPlatform(platform)

  // Agent command
  if (args[0] === 'agent') {
    await runAgentCommand(args.slice(1))
    return
  }

  // Agent V2 command (core-v2 + extensions)
  if (args[0] === 'agent-v2') {
    await runAgentV2Command(args.slice(1))
    return
  }

  // Auth command
  if (args[0] === 'auth') {
    await runAuthCommand(args.slice(1))
    return
  }

  // Plugin command
  if (args[0] === 'plugin') {
    await runPluginCommand(args.slice(1))
    return
  }

  // Run command
  if (args[0] === 'run') {
    await runRunCommand(args.slice(1))
    return
  }

  // Tool command
  if (args[0] === 'tool') {
    await runToolCommand(args.slice(1))
    return
  }

  // Validate command
  if (args[0] === 'validate') {
    await runValidateCommand(args.slice(1))
    return
  }

  // Default: Show help (TUI not implemented yet)
  console.log('AVA CLI')
  console.log('')
  console.log('TUI mode not yet implemented.')
  console.log('Run `ava --help` for more information.')
}

function printHelp() {
  console.log(`
AVA CLI - Multi-Agent AI Coding Assistant

USAGE:
  ava [OPTIONS]
  ava <command> [args]

COMMANDS:
  agent           Run the AI agent (invoke the agent loop)
  run "<goal>"    Execute the agent loop
  tool            Execute individual tools
  validate        Run validation pipeline on files
  auth            Manage authentication (OAuth login/logout)
  plugin          Plugin development commands

OPTIONS:
  --version, -v   Show version
  --help, -h      Show this help

AGENT:
  ava run "Fix the bug in auth.ts"                  Run agent with default settings
  ava run "Read the README" --mock                  Use mock LLM (no API key)
  ava run "Refactor module" --provider anthropic    Specify provider
  ava run "List files" --max-turns 5 --verbose      Limit turns, verbose output

TOOLS:
  ava tool list                                     List all registered tools
  ava tool info read_file                           Show tool schema
  ava tool read_file --path README.md               Execute a tool
  ava tool glob --pattern "src/**/*.ts"             Search for files
  ava tool grep --pattern "export" --path src/      Search file contents

VALIDATION:
  ava validate src/index.ts                         Run syntax/typescript/lint
  ava validate src/ --validators syntax,typescript  Select validators
  ava validate src/index.ts --json                  JSON output

AUTHENTICATION:
  ava auth login anthropic    Connect Claude Pro/Max subscription
  ava auth login openai       Connect ChatGPT Plus/Pro subscription
  ava auth status             Show authentication status
  ava auth logout <provider>  Disconnect a provider

PLUGIN DEVELOPMENT:
  ava plugin init my-plugin                Create plugin scaffold in current directory
  ava plugin init my-plugin --dir ./plugins  Create scaffold in a custom directory
  ava plugin init my-plugin --force        Overwrite in non-empty target directory
  ava plugin dev my-plugin --dir ./plugins Run plugin dev/watch script
  ava plugin test my-plugin --dir ./plugins Run plugin test suite

AGENT V2 (core-v2 + extensions):
  ava agent-v2 run "goal"                  Run agent-v2 with a goal
  ava agent-v2 run "goal" --verbose        Stream events to stderr
  ava agent-v2 run "goal" --json           Output NDJSON events to stdout
  ava agent-v2 run "goal" --yolo           Auto-approve all tools
  ava agent-v2 run "goal" --provider openrouter --model "anthropic/claude-sonnet-4"

AGENT (legacy):
  ava agent run "goal"               Run the agent with a goal
  ava agent run "goal" --verbose     Stream events to stderr
  ava agent run "goal" --json        Output NDJSON events to stdout
  ava agent run "goal" --provider openai --model gpt-4

EXAMPLES:
  # Run agent-v2 with verbose output
  ava agent-v2 run "list files in current directory" --verbose --yolo

  # Run legacy agent
  ava agent run "list files" --verbose

  # Connect Claude subscription for OAuth
  ava auth login anthropic

  # Scaffold a plugin
  ava plugin init my-plugin

ENVIRONMENT VARIABLES:
  AVA_ANTHROPIC_API_KEY    Anthropic API key (alternative to OAuth)
  AVA_OPENROUTER_API_KEY   OpenRouter API key
  AVA_OPENAI_API_KEY       OpenAI API key (alternative to OAuth)

For more information, visit: https://github.com/g0dxn4/AVA
`)
}

main().catch((err) => {
  console.error('Fatal error:', err)
  process.exit(1)
})
