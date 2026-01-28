/**
 * Delta9 CLI Setup Command
 *
 * Interactive installation wizard for Delta9 agents and configuration.
 * Inspired by oh-my-opencode's beautiful TUI setup.
 *
 * Features:
 * - Interactive model provider selection
 * - Agent configuration
 * - Automatic opencode.json updates
 * - Support for both TUI and non-TUI modes
 */

import * as p from '@clack/prompts'
import color from 'picocolors'
import fs from 'node:fs'
import path from 'node:path'
import os from 'node:os'
import type { CLICommandResult } from '../types.js'

// =============================================================================
// Types
// =============================================================================

interface SetupOptions {
  force?: boolean
  dryRun?: boolean
  noTui?: boolean
}

interface SetupConfig {
  // Model providers
  hasClaude: boolean
  hasOpenAI: boolean
  hasGemini: boolean
  hasCopilot: boolean
  // Agent selection
  installAllAgents: boolean
  selectedAgents: string[]
}

// =============================================================================
// Constants
// =============================================================================

const SYMBOLS = {
  check: color.green('✓'),
  cross: color.red('✗'),
  arrow: color.cyan('→'),
  bullet: color.dim('•'),
  info: color.blue('ℹ'),
  warn: color.yellow('⚠'),
  star: color.yellow('★'),
}

// Agent definitions to add to opencode.json
const AGENT_DEFINITIONS: Record<string, Record<string, unknown>> = {
  commander: {
    description: 'Commander - Strategic AI Coordination (Delta9)',
    prompt: '{file:~/.config/opencode/agents/commander.md}',
    temperature: 0.7,
  },
  operator: {
    description: 'Operator - Task Execution (Delta9)',
    prompt: '{file:~/.config/opencode/agents/operator.md}',
    mode: 'subagent',
    temperature: 0.3,
  },
  validator: {
    description: 'Validator - Quality Gate (Delta9)',
    prompt: '{file:~/.config/opencode/agents/validator.md}',
    mode: 'subagent',
    temperature: 0.1,
    tools: { write: false, edit: false },
  },
  scout: {
    description: 'RECON - Codebase reconnaissance (Delta9)',
    prompt: '{file:~/.config/opencode/agents/scout.md}',
    mode: 'subagent',
    temperature: 0.1,
  },
  intel: {
    description: 'SIGINT - Research and documentation (Delta9)',
    prompt: '{file:~/.config/opencode/agents/intel.md}',
    mode: 'subagent',
    temperature: 0.2,
    tools: { write: false, edit: false },
  },
  strategist: {
    description: 'TACCOM - Tactical advisor (Delta9)',
    prompt: '{file:~/.config/opencode/agents/strategist.md}',
    mode: 'subagent',
    temperature: 0.4,
    tools: { write: false, edit: false, bash: false },
  },
  patcher: {
    description: 'SURGEON - Quick fixes (Delta9)',
    prompt: '{file:~/.config/opencode/agents/patcher.md}',
    mode: 'subagent',
    temperature: 0.1,
  },
  qa: {
    description: 'SENTINEL - Testing (Delta9)',
    prompt: '{file:~/.config/opencode/agents/qa.md}',
    mode: 'subagent',
    temperature: 0.2,
  },
  scribe: {
    description: 'SCRIBE - Documentation (Delta9)',
    prompt: '{file:~/.config/opencode/agents/scribe.md}',
    mode: 'subagent',
    temperature: 0.3,
  },
  uiOps: {
    description: 'FACADE - Frontend & Visual Analysis (Delta9)',
    prompt: '{file:~/.config/opencode/agents/uiOps.md}',
    mode: 'subagent',
    temperature: 0.4,
  },
}

const CORE_AGENTS = ['commander', 'operator', 'validator']

// =============================================================================
// Utility Functions
// =============================================================================

function findAgentsDir(): string | null {
  const possiblePaths = [
    path.resolve(import.meta.dirname ?? __dirname, '../../../agents'),
    path.resolve(import.meta.dirname ?? __dirname, '../../agents'),
    path.resolve(import.meta.dirname ?? __dirname, '../../../agents'),
  ]

  for (const p of possiblePaths) {
    if (fs.existsSync(p) && fs.statSync(p).isDirectory()) {
      return p
    }
  }

  return null
}

function detectExistingConfig(): { isInstalled: boolean; agents: string[] } {
  const homeDir = os.homedir()
  const opencodeJsonPath = path.join(homeDir, '.config/opencode/opencode.json')

  if (!fs.existsSync(opencodeJsonPath)) {
    return { isInstalled: false, agents: [] }
  }

  try {
    const config = JSON.parse(fs.readFileSync(opencodeJsonPath, 'utf-8'))
    const plugins = (config.plugin as string[]) || []
    const agents = Object.keys((config.agent as Record<string, unknown>) || {})
    const delta9Agents = agents.filter((a) => Object.keys(AGENT_DEFINITIONS).includes(a))

    return {
      isInstalled: plugins.includes('delta9'),
      agents: delta9Agents,
    }
  } catch {
    return { isInstalled: false, agents: [] }
  }
}

function formatProvider(name: string, enabled: boolean, detail?: string): string {
  const status = enabled ? SYMBOLS.check : color.dim('○')
  const label = enabled ? color.white(name) : color.dim(name)
  const suffix = detail ? color.dim(` (${detail})`) : ''
  return `  ${status} ${label}${suffix}`
}

function formatConfigSummary(config: SetupConfig): string {
  const lines: string[] = []

  lines.push(color.bold(color.white('Configuration Summary')))
  lines.push('')

  lines.push(color.bold('Model Providers:'))
  lines.push(formatProvider('Claude', config.hasClaude, 'Recommended for Commander'))
  lines.push(formatProvider('OpenAI', config.hasOpenAI, 'GPT for Strategic Advisor fallback'))
  lines.push(formatProvider('Gemini', config.hasGemini, 'For UI/frontend tasks'))
  lines.push(formatProvider('GitHub Copilot', config.hasCopilot, 'Fallback provider'))
  lines.push('')

  lines.push(color.bold('Agents:'))
  const agentCount = config.installAllAgents
    ? Object.keys(AGENT_DEFINITIONS).length
    : config.selectedAgents.length
  lines.push(`  ${SYMBOLS.info} ${agentCount} agents will be installed`)

  if (!config.installAllAgents && config.selectedAgents.length > 0) {
    lines.push(`  ${SYMBOLS.bullet} ${config.selectedAgents.join(', ')}`)
  }

  return lines.join('\n')
}

// Strip ANSI escape codes from string
// eslint-disable-next-line no-control-regex
const ANSI_PATTERN = /\x1b\[[0-9;]*m/g

function printBox(content: string, title?: string): void {
  const lines = content.split('\n')
  const maxWidth =
    Math.max(...lines.map((l) => l.replace(ANSI_PATTERN, '').length), title?.length ?? 0) + 4
  const border = color.dim('─'.repeat(maxWidth))

  console.log()
  if (title) {
    console.log(
      color.dim('┌─') +
        color.bold(` ${title} `) +
        color.dim('─'.repeat(maxWidth - title.length - 4)) +
        color.dim('┐')
    )
  } else {
    console.log(color.dim('┌') + border + color.dim('┐'))
  }

  for (const line of lines) {
    const stripped = line.replace(ANSI_PATTERN, '')
    const padding = maxWidth - stripped.length
    console.log(color.dim('│') + ` ${line}${' '.repeat(padding - 1)}` + color.dim('│'))
  }

  console.log(color.dim('└') + border + color.dim('┘'))
  console.log()
}

// =============================================================================
// Interactive TUI Mode
// =============================================================================

async function runTuiMode(): Promise<SetupConfig | null> {
  // Model provider selection
  const claude = await p.select({
    message: 'Do you have a Claude Pro/Max subscription?',
    options: [
      { value: 'yes' as const, label: 'Yes', hint: 'Recommended - Claude Opus 4.5 for Commander' },
      { value: 'no' as const, label: 'No', hint: 'Will use fallback models' },
    ],
    initialValue: 'yes' as const,
  })

  if (p.isCancel(claude)) {
    p.cancel('Setup cancelled.')
    return null
  }

  const openai = await p.select({
    message: 'Do you have an OpenAI API key or ChatGPT Plus?',
    options: [
      { value: 'no' as const, label: 'No', hint: 'Strategic Advisors will use other fallbacks' },
      { value: 'yes' as const, label: 'Yes', hint: 'GPT-4 for high-IQ debugging' },
    ],
    initialValue: 'no' as const,
  })

  if (p.isCancel(openai)) {
    p.cancel('Setup cancelled.')
    return null
  }

  const gemini = await p.select({
    message: 'Do you have Google Gemini access?',
    options: [
      { value: 'no' as const, label: 'No', hint: 'UI agents will use Claude' },
      { value: 'yes' as const, label: 'Yes', hint: 'Great for UI/frontend tasks' },
    ],
    initialValue: 'no' as const,
  })

  if (p.isCancel(gemini)) {
    p.cancel('Setup cancelled.')
    return null
  }

  const copilot = await p.select({
    message: 'Do you have GitHub Copilot?',
    options: [
      { value: 'no' as const, label: 'No', hint: 'Only configured providers will be used' },
      { value: 'yes' as const, label: 'Yes', hint: 'Fallback option for operators' },
    ],
    initialValue: 'no' as const,
  })

  if (p.isCancel(copilot)) {
    p.cancel('Setup cancelled.')
    return null
  }

  // Agent selection
  const agentChoice = await p.select({
    message: 'Which agents do you want to install?',
    options: [
      {
        value: 'all' as const,
        label: 'All agents (recommended)',
        hint: '10 agents: Commander, Operators, Council, Support',
      },
      {
        value: 'core' as const,
        label: 'Core only',
        hint: '3 agents: Commander, Operator, Validator',
      },
      { value: 'custom' as const, label: 'Custom selection', hint: 'Choose specific agents' },
    ],
    initialValue: 'all' as const,
  })

  if (p.isCancel(agentChoice)) {
    p.cancel('Setup cancelled.')
    return null
  }

  let selectedAgents: string[] = []

  if (agentChoice === 'custom') {
    const agents = await p.multiselect({
      message: 'Select agents to install:',
      options: [
        { value: 'commander', label: 'Commander', hint: 'Lead planner & orchestrator' },
        { value: 'operator', label: 'Operator', hint: 'Task executor' },
        { value: 'validator', label: 'Validator', hint: 'QA verification gate' },
        { value: 'scout', label: 'Scout (RECON)', hint: 'Codebase reconnaissance' },
        { value: 'intel', label: 'Intel (SIGINT)', hint: 'Research & documentation' },
        { value: 'strategist', label: 'Strategist (TACCOM)', hint: 'Tactical advisor' },
        { value: 'patcher', label: 'Patcher (SURGEON)', hint: 'Quick surgical fixes' },
        { value: 'qa', label: 'QA (SENTINEL)', hint: 'Testing specialist' },
        { value: 'scribe', label: 'Scribe', hint: 'Documentation writer' },
        { value: 'uiOps', label: 'UI Ops (FACADE)', hint: 'Frontend & visual analysis' },
      ],
      required: true,
      initialValues: CORE_AGENTS,
    })

    if (p.isCancel(agents)) {
      p.cancel('Setup cancelled.')
      return null
    }

    selectedAgents = agents as string[]
  } else if (agentChoice === 'core') {
    selectedAgents = CORE_AGENTS
  }

  return {
    hasClaude: claude === 'yes',
    hasOpenAI: openai === 'yes',
    hasGemini: gemini === 'yes',
    hasCopilot: copilot === 'yes',
    installAllAgents: agentChoice === 'all',
    selectedAgents,
  }
}

// =============================================================================
// Installation Logic
// =============================================================================

async function performInstall(
  config: SetupConfig,
  options: SetupOptions
): Promise<{ success: boolean; actions: string[]; warnings: string[]; errors: string[] }> {
  const { force = false, dryRun = false } = options
  const homeDir = os.homedir()
  const opencodeDir = path.join(homeDir, '.config/opencode')
  const agentsDir = path.join(opencodeDir, 'agents')
  const opencodeJsonPath = path.join(opencodeDir, 'opencode.json')
  const delta9ConfigDir = path.join(homeDir, '.config/delta9')

  const actions: string[] = []
  const warnings: string[] = []
  const errors: string[] = []

  // Find source agents directory
  const sourceAgentsDir = findAgentsDir()
  if (!sourceAgentsDir) {
    errors.push('Could not find agents directory. Please ensure Delta9 is properly installed.')
    return { success: false, actions, warnings, errors }
  }

  // Determine which agents to install
  const agentsToInstall = config.installAllAgents
    ? Object.keys(AGENT_DEFINITIONS)
    : config.selectedAgents

  const filesToCopy = agentsToInstall.map((a) => `${a}.md`)

  // Step 1: Create directories
  if (!fs.existsSync(agentsDir)) {
    if (dryRun) {
      actions.push(`Would create directory: ${agentsDir}`)
    } else {
      fs.mkdirSync(agentsDir, { recursive: true })
      actions.push(`Created directory: ${agentsDir}`)
    }
  }

  if (!fs.existsSync(delta9ConfigDir)) {
    if (dryRun) {
      actions.push(`Would create directory: ${delta9ConfigDir}`)
    } else {
      fs.mkdirSync(delta9ConfigDir, { recursive: true })
      actions.push(`Created directory: ${delta9ConfigDir}`)
    }
  }

  // Step 2: Copy agent files
  for (const file of filesToCopy) {
    const sourcePath = path.join(sourceAgentsDir, file)
    const destPath = path.join(agentsDir, file)

    if (!fs.existsSync(sourcePath)) {
      warnings.push(`Source file not found: ${file}`)
      continue
    }

    const exists = fs.existsSync(destPath)
    if (exists && !force) {
      warnings.push(`File exists (use --force to overwrite): ${file}`)
      continue
    }

    if (dryRun) {
      actions.push(`Would copy: ${file} → ${destPath}`)
    } else {
      fs.copyFileSync(sourcePath, destPath)
      actions.push(`Copied: ${file}`)
    }
  }

  // Step 3: Update opencode.json
  let opencodeJson: Record<string, unknown> = {}
  if (fs.existsSync(opencodeJsonPath)) {
    try {
      opencodeJson = JSON.parse(fs.readFileSync(opencodeJsonPath, 'utf-8'))
    } catch {
      errors.push(`Could not parse opencode.json: ${opencodeJsonPath}`)
    }
  }

  // Merge agent definitions
  const existingAgents = (opencodeJson.agent as Record<string, unknown>) || {}
  let agentsUpdated = 0

  for (const name of agentsToInstall) {
    const agentConfig = AGENT_DEFINITIONS[name]
    if (!agentConfig) continue

    const exists = name in existingAgents
    if (exists && !force) {
      warnings.push(`Agent exists (use --force to overwrite): ${name}`)
      continue
    }

    if (dryRun) {
      actions.push(`Would add agent: ${name}`)
    } else {
      existingAgents[name] = agentConfig
      agentsUpdated++
    }
  }

  if (!dryRun && agentsUpdated > 0) {
    opencodeJson.agent = existingAgents

    // Add plugin if not present
    const plugins = (opencodeJson.plugin as string[]) || []
    if (!plugins.includes('delta9')) {
      plugins.push('delta9')
      opencodeJson.plugin = plugins
      actions.push('Added delta9 to plugins list')
    }

    fs.writeFileSync(opencodeJsonPath, JSON.stringify(opencodeJson, null, 2))
    actions.push(`Updated opencode.json with ${agentsUpdated} agents`)
  }

  // Step 4: Write Delta9 config with provider info
  if (!dryRun) {
    const delta9Config = {
      version: '0.1.0',
      providers: {
        claude: config.hasClaude,
        openai: config.hasOpenAI,
        gemini: config.hasGemini,
        copilot: config.hasCopilot,
      },
      agents: agentsToInstall,
      installedAt: new Date().toISOString(),
    }
    fs.writeFileSync(
      path.join(delta9ConfigDir, 'config.json'),
      JSON.stringify(delta9Config, null, 2)
    )
    actions.push('Wrote Delta9 configuration')
  }

  return { success: errors.length === 0, actions, warnings, errors }
}

// =============================================================================
// Main Command
// =============================================================================

export async function setupCommand(options: SetupOptions = {}): Promise<CLICommandResult> {
  const { dryRun = false, noTui = false } = options

  const detected = detectExistingConfig()
  const isUpdate = detected.isInstalled

  // Non-TUI mode (for scripts/CI)
  if (noTui) {
    const config: SetupConfig = {
      hasClaude: true,
      hasOpenAI: false,
      hasGemini: false,
      hasCopilot: false,
      installAllAgents: true,
      selectedAgents: [],
    }

    const result = await performInstall(config, options)
    return {
      success: result.success,
      message: formatNonTuiResult(result, dryRun, isUpdate),
      data: result,
    }
  }

  // TUI mode
  p.intro(color.bgMagenta(color.white(isUpdate ? ' ∆ Delta9 Update ' : ' ∆ Delta9 Setup ')))

  if (isUpdate) {
    p.log.info(`Existing installation detected with ${detected.agents.length} agents`)
  }

  // Run interactive setup
  const config = await runTuiMode()
  if (!config) {
    return { success: false, message: 'Setup cancelled.' }
  }

  // Show configuration summary
  printBox(formatConfigSummary(config), 'Configuration')

  // Confirm installation
  if (!dryRun) {
    const confirm = await p.confirm({
      message: 'Proceed with installation?',
      initialValue: true,
    })

    if (p.isCancel(confirm) || !confirm) {
      p.cancel('Setup cancelled.')
      return { success: false, message: 'Setup cancelled by user.' }
    }
  }

  // Perform installation with spinner
  const s = p.spinner()
  s.start(dryRun ? 'Previewing changes...' : 'Installing Delta9...')

  const result = await performInstall(config, options)

  if (result.success) {
    s.stop(color.green(dryRun ? 'Preview complete!' : 'Installation complete!'))
  } else {
    s.stop(color.red('Installation failed'))
  }

  // Show results
  if (result.actions.length > 0) {
    console.log()
    console.log(color.bold('Actions:'))
    for (const action of result.actions) {
      console.log(`  ${SYMBOLS.check} ${action}`)
    }
  }

  if (result.warnings.length > 0) {
    console.log()
    console.log(color.bold(color.yellow('Warnings:')))
    for (const warning of result.warnings) {
      console.log(`  ${SYMBOLS.warn} ${warning}`)
    }
  }

  if (result.errors.length > 0) {
    console.log()
    console.log(color.bold(color.red('Errors:')))
    for (const error of result.errors) {
      console.log(`  ${SYMBOLS.cross} ${error}`)
    }
  }

  // Success message
  if (result.success && !dryRun) {
    console.log()
    printBox(
      `${color.bold('Next Steps:')}\n` +
        `  1. Restart OpenCode for changes to take effect\n` +
        `  2. Run ${color.cyan('delta9 health')} to verify installation\n` +
        `  3. Start a mission with ${color.cyan('@commander')}`,
      'Ready to Deploy'
    )

    if (!config.hasClaude) {
      console.log()
      console.log(color.bgYellow(color.black(' Note ')))
      console.log(color.yellow('  Commander works best with Claude Opus 4.5.'))
      console.log(color.yellow('  Consider subscribing to Claude Pro/Max for optimal performance.'))
      console.log()
    }

    console.log(`${SYMBOLS.star} ${color.green('Delta9 is ready for deployment!')}`)
    console.log()
  }

  p.outro(
    dryRun
      ? color.dim('Dry run complete - no changes made')
      : color.green('∆ Mission Control Online')
  )

  return {
    success: result.success,
    message: result.success
      ? dryRun
        ? 'Dry run complete'
        : 'Delta9 installed successfully'
      : 'Installation failed',
    data: result,
  }
}

function formatNonTuiResult(
  result: { success: boolean; actions: string[]; warnings: string[]; errors: string[] },
  dryRun: boolean,
  isUpdate: boolean
): string {
  let message = dryRun
    ? '**Dry Run - No changes made**\n\n'
    : `**Delta9 ${isUpdate ? 'Update' : 'Setup'} Complete**\n\n`

  if (result.actions.length > 0) {
    message += '**Actions:**\n' + result.actions.map((a) => `- ${a}`).join('\n') + '\n\n'
  }

  if (result.warnings.length > 0) {
    message += '**Warnings:**\n' + result.warnings.map((w) => `- ${w}`).join('\n') + '\n\n'
  }

  if (result.errors.length > 0) {
    message += '**Errors:**\n' + result.errors.map((e) => `- ${e}`).join('\n') + '\n\n'
  }

  if (!dryRun && result.errors.length === 0) {
    message += '**Next Steps:**\n'
    message += '1. Restart OpenCode for changes to take effect\n'
    message += '2. Run `delta9 health` to verify installation\n'
  }

  return message
}
