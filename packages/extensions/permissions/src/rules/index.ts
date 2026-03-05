/**
 * Rules extension — path-targeted coding instructions.
 *
 * Rules activate based on file globs and inject into the system prompt.
 * Auto-discovers rule .md files from .ava/rules/, .claude/rules/, .cursor/rules/.
 * Three activation modes: always, auto (glob-matched), manual (explicit only).
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { addPromptSection } from '../../../prompts/src/builder.js'
import { discoverRules } from './loader.js'
import { matchRules } from './matcher.js'
import type { Rule, RuleConfig } from './types.js'
import { DEFAULT_RULE_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config: RuleConfig = {
    ...DEFAULT_RULE_CONFIG,
    ...api.getSettings<Partial<RuleConfig>>('rules'),
  }
  const rules: Rule[] = []
  const disposables: Disposable[] = []

  // Track prompt section cleanups
  const alwaysCleanups: Array<() => void> = []
  let autoCleanups: Array<() => void> = []

  // Listen for dynamic rule registration (e.g. from create_rule tool)
  disposables.push(
    api.on('rules:register', (data) => {
      const rule = data as Rule
      rules.push(rule)
      api.log.debug(`Rule registered: ${rule.name}`)

      // If it's an always rule, inject immediately
      if (rule.activation === 'always') {
        const cleanup = addPromptSection({
          name: `rule:${rule.name}`,
          priority: 140,
          content: `<rule name="${rule.name}">\n${rule.content}\n</rule>`,
        })
        alwaysCleanups.push(cleanup)
      }
    })
  )

  // Auto-discover rule files on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      void discoverRules(workingDirectory, api.platform.fs).then((discovered) => {
        for (const rule of discovered) {
          rules.push(rule)
          api.log.debug(`Rule discovered: ${rule.name} (${rule.source})`)

          // Inject always rules immediately
          if (rule.activation === 'always') {
            const cleanup = addPromptSection({
              name: `rule:${rule.name}`,
              priority: 140,
              content: `<rule name="${rule.name}">\n${rule.content}\n</rule>`,
            })
            alwaysCleanups.push(cleanup)
          }
        }
        if (discovered.length > 0) {
          api.emit('rules:discovered', { count: discovered.length, rules: discovered })
        }
      })
    })
  )

  // On each turn, match auto rules against current files
  disposables.push(
    api.on('agent:turn-start', (data) => {
      const { files } = data as { sessionId: string; files?: string[] }

      // Clean up previous turn's auto-rule sections
      for (const cleanup of autoCleanups) cleanup()
      autoCleanups = []

      if (!files?.length || rules.length === 0) return

      // Only match auto rules (always rules are already injected, manual are excluded)
      const autoRules = rules.filter((r) => r.activation === 'auto')
      const matches = matchRules(autoRules, files, config)

      if (matches.length > 0) {
        api.emit('rules:matched', { matches })

        for (const match of matches) {
          const cleanup = addPromptSection({
            name: `rule:${match.rule.name}`,
            priority: 140,
            content: `<rule name="${match.rule.name}">\n${match.rule.content}\n</rule>`,
          })
          autoCleanups.push(cleanup)
        }

        api.log.debug(`Injected ${matches.length} auto-rule(s) into prompt`)
      }
    })
  )

  api.log.debug('Rules extension activated')

  return {
    dispose() {
      // Clean up all prompt sections
      for (const cleanup of alwaysCleanups) cleanup()
      for (const cleanup of autoCleanups) cleanup()
      autoCleanups = []
      for (const d of disposables) d.dispose()
      rules.length = 0
    },
  }
}
