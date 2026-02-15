#!/usr/bin/env node

/**
 * AVA CLI Entry Point
 *
 * Usage:
 *   ava              - Interactive TUI mode (future)
 *   ava --acp        - ACP agent mode for Toad/Zed
 *   ava auth         - Manage authentication
 *   ava --version    - Show version
 *   ava --help       - Show help
 */

import * as os from 'node:os'
import * as path from 'node:path'
import { setPlatform } from '@ava/core'
import { createNodePlatform } from '@ava/platform-node'
import { startAcpAgent } from './acp/agent.js'
import { runAuthCommand } from './commands/auth.js'

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

  // ACP mode
  if (args.includes('--acp')) {
    await startAcpAgent()
    return
  }

  // Auth command
  if (args[0] === 'auth') {
    await runAuthCommand(args.slice(1))
    return
  }

  // Default: Show help (TUI not implemented yet)
  console.log('AVA CLI')
  console.log('')
  console.log('TUI mode not yet implemented. Use --acp for ACP agent mode.')
  console.log('Run `ava --help` for more information.')
}

function printHelp() {
  console.log(`
AVA CLI - Multi-Agent AI Coding Assistant

USAGE:
  ava [OPTIONS]
  ava <command> [args]

COMMANDS:
  auth            Manage authentication (OAuth login/logout)

OPTIONS:
  --acp           Run as ACP agent (for Toad, Zed, etc.)
  --version, -v   Show version
  --help, -h      Show this help

AUTHENTICATION:
  ava auth login anthropic    Connect Claude Pro/Max subscription
  ava auth login openai       Connect ChatGPT Plus/Pro subscription
  ava auth status             Show authentication status
  ava auth logout <provider>  Disconnect a provider

EXAMPLES:
  # Connect Claude subscription for OAuth
  ava auth login anthropic

  # Run as ACP agent
  ava --acp

  # Check version
  ava --version

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
