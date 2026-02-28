/**
 * Deploy Command Plugin
 *
 * Demonstrates: registerCommand() with argument parsing
 * Registers a /deploy slash command that simulates deployment workflows.
 */

import type { Disposable, ExtensionAPI, SlashCommand } from '@ava/core-v2/extensions'

type DeployTarget = 'staging' | 'production' | 'preview'

const VALID_TARGETS: DeployTarget[] = ['staging', 'production', 'preview']

function isValidTarget(target: string): target is DeployTarget {
  return VALID_TARGETS.includes(target as DeployTarget)
}

export function activate(api: ExtensionAPI): Disposable {
  const deployCommand: SlashCommand = {
    name: 'deploy',
    description:
      'Deploy the current project. Usage: /deploy [staging|production|preview] [--dry-run]',

    async execute(args, _ctx) {
      const parts = args.trim().split(/\s+/).filter(Boolean)
      const dryRun = parts.includes('--dry-run')
      const targetArg = parts.find((p) => !p.startsWith('--'))
      const target: DeployTarget = targetArg && isValidTarget(targetArg) ? targetArg : 'staging'

      if (targetArg && !isValidTarget(targetArg)) {
        return `Unknown deploy target: "${targetArg}". Valid targets: ${VALID_TARGETS.join(', ')}`
      }

      const steps = [
        `Deploying to ${target}${dryRun ? ' (dry run)' : ''}...`,
        `[1/4] Running pre-deploy checks...`,
        `[2/4] Building project...`,
        `[3/4] Uploading artifacts to ${target}...`,
        `[4/4] ${dryRun ? 'Dry run complete — no changes applied.' : `Deployment to ${target} successful!`}`,
      ]

      api.log.info(`Deploy triggered: target=${target}, dryRun=${dryRun}`)
      return steps.join('\n')
    },
  }

  const disposable = api.registerCommand(deployCommand)
  api.log.info('Deploy command registered')
  return disposable
}
