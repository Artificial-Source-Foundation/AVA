#!/usr/bin/env node
/**
 * Delta9 CLI
 *
 * Command-line interface for Delta9 plugin management and diagnostics.
 *
 * Available Commands:
 *   status   - Mission overview dashboard
 *   history  - Event log viewer
 *   health   - Environment diagnostics (like oh-my-opencode doctor)
 *   abort    - Abort current mission
 *   resume   - Resume aborted/paused mission
 *
 * Usage:
 *   npx delta9 status [--verbose] [--format=summary|json|table]
 *   npx delta9 history [--limit=20] [--type=mission] [--format=timeline|json|table]
 *   npx delta9 health [--verbose] [--format=summary|json]
 *   npx delta9 abort [--reason=<reason>] [--force]
 *   npx delta9 resume [checkpoint-id] [--no-reset-failed]
 */

import { Command } from 'commander'
import { createRequire } from 'module'
import { statusCommand } from './commands/status.js'
import { historyCommand } from './commands/history.js'
import { healthCommand } from './commands/health.js'
import { abortCommand } from './commands/abort.js'
import { resumeCommand } from './commands/resume.js'
import { colorize } from './types.js'

// =============================================================================
// Version Loading
// =============================================================================

const require = createRequire(import.meta.url)
let version = '0.1.0'

try {
  const pkg = require('../../package.json') as { version: string }
  version = pkg.version
} catch {
  // Use default version
}

// =============================================================================
// ASCII Banner
// =============================================================================

const BANNER = `
${colorize('╔═══════════════════════════════════════════╗', 'cyan')}
${colorize('║', 'cyan')}   ${colorize('DELTA9', 'bold')} - Strategic AI Coordination       ${colorize('║', 'cyan')}
${colorize('║', 'cyan')}   Commander + Council + Operators        ${colorize('║', 'cyan')}
${colorize('╚═══════════════════════════════════════════╝', 'cyan')}
`

// =============================================================================
// CLI Setup
// =============================================================================

const program = new Command()

program
  .name('delta9')
  .description('Delta9 CLI - Strategic AI Coordination for Mission-Critical Development')
  .version(version, '-v, --version', 'Display version')
  .addHelpText('beforeAll', BANNER)
  .showHelpAfterError(true)
  .exitOverride((err) => {
    // Handle commander errors gracefully
    if (
      err.code === 'commander.helpDisplayed' ||
      err.code === 'commander.help' ||
      err.code === 'commander.version'
    ) {
      process.exit(0)
    }
    if (
      err.code === 'commander.missingArgument' ||
      err.code === 'commander.missingMandatoryOptionValue' ||
      err.code === 'commander.unknownOption' ||
      err.code === 'commander.invalidArgument'
    ) {
      process.exit(1)
    }
    process.exit(err.exitCode || 1)
  })

// =============================================================================
// Status Command
// =============================================================================

program
  .command('status')
  .description('Display mission overview dashboard')
  .option('--verbose', 'Show detailed task information')
  .option('--format <format>', 'Output format: summary, json, table', 'summary')
  .option('--cwd <path>', 'Project directory (default: current)')
  .action(async (options) => {
    try {
      await statusCommand({
        verbose: options.verbose,
        format: options.format as 'summary' | 'json' | 'table',
        cwd: options.cwd,
      })
    } catch (error) {
      console.error(
        colorize('Error:', 'red'),
        error instanceof Error ? error.message : String(error)
      )
      process.exit(1)
    }
  })

// =============================================================================
// History Command
// =============================================================================

program
  .command('history')
  .description('View event log with filtering')
  .option('-n, --limit <number>', 'Number of events to show', '20')
  .option('-t, --type <type>', 'Filter by event type prefix (e.g., mission, task)')
  .option(
    '-c, --category <category>',
    'Filter by category (mission, task, council, agent, validation, learning, file, system)'
  )
  .option('-s, --session <id>', 'Filter by session ID')
  .option('--format <format>', 'Output format: timeline, json, table', 'timeline')
  .option('--cwd <path>', 'Project directory (default: current)')
  .action(async (options) => {
    try {
      await historyCommand({
        limit: parseInt(options.limit, 10),
        type: options.type,
        category: options.category,
        session: options.session,
        format: options.format as 'timeline' | 'json' | 'table',
        cwd: options.cwd,
      })
    } catch (error) {
      console.error(
        colorize('Error:', 'red'),
        error instanceof Error ? error.message : String(error)
      )
      process.exit(1)
    }
  })

// =============================================================================
// Health Command
// =============================================================================

program
  .command('health')
  .alias('doctor')
  .description('Run environment diagnostics and health checks')
  .option('--verbose', 'Include detailed checks')
  .option('--format <format>', 'Output format: summary, json', 'summary')
  .option('--cwd <path>', 'Project directory (default: current)')
  .action(async (options) => {
    try {
      await healthCommand({
        verbose: options.verbose,
        format: options.format as 'summary' | 'json',
        cwd: options.cwd,
      })
    } catch (error) {
      console.error(
        colorize('Error:', 'red'),
        error instanceof Error ? error.message : String(error)
      )
      process.exit(1)
    }
  })

// =============================================================================
// Abort Command
// =============================================================================

program
  .command('abort')
  .description('Abort the current mission')
  .option('-r, --reason <reason>', 'Reason for aborting')
  .option('-f, --force', 'Force abort even if already aborted')
  .option('--no-checkpoint', 'Skip creating recovery checkpoint')
  .option('--format <format>', 'Output format: summary, json', 'summary')
  .option('--cwd <path>', 'Project directory (default: current)')
  .action(async (options) => {
    try {
      await abortCommand({
        reason: options.reason,
        force: options.force,
        checkpoint: options.checkpoint,
        format: options.format as 'summary' | 'json',
        cwd: options.cwd,
      })
    } catch (error) {
      console.error(
        colorize('Error:', 'red'),
        error instanceof Error ? error.message : String(error)
      )
      process.exit(1)
    }
  })

// =============================================================================
// Resume Command
// =============================================================================

program
  .command('resume [checkpoint]')
  .description('Resume an aborted or paused mission')
  .argument('[checkpoint]', 'Checkpoint ID to resume from (optional)')
  .option('--no-reset-failed', 'Do not reset failed tasks to pending')
  .option('--format <format>', 'Output format: summary, json', 'summary')
  .option('--cwd <path>', 'Project directory (default: current)')
  .action(async (checkpoint, options) => {
    try {
      await resumeCommand({
        checkpoint,
        resetFailed: options.resetFailed,
        format: options.format as 'summary' | 'json',
        cwd: options.cwd,
      })
    } catch (error) {
      console.error(
        colorize('Error:', 'red'),
        error instanceof Error ? error.message : String(error)
      )
      process.exit(1)
    }
  })

// =============================================================================
// Parse and Execute
// =============================================================================

program.parse()
