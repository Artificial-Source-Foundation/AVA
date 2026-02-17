#!/usr/bin/env node

/**
 * AVA CLI Entry Point
 *
 * Usage:
 *   ava                 - Interactive TUI mode (future)
 *   ava auth            - Manage authentication
 *   ava plugin          - Plugin development commands
 *   ava --version       - Show version
 *   ava --help          - Show help
 */

import * as os from 'node:os'
import * as path from 'node:path'
import { setPlatform } from '@ava/core'
import { createNodePlatform } from '@ava/platform-node'
import { runAuthCommand } from './commands/auth.js'
import { runPluginCommand } from './commands/plugin.js'

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
  auth            Manage authentication (OAuth login/logout)
  plugin          Plugin development commands

OPTIONS:
  --version, -v   Show version
  --help, -h      Show this help

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

EXAMPLES:
  # Connect Claude subscription for OAuth
  ava auth login anthropic

  # Scaffold a plugin
  ava plugin init my-plugin

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
