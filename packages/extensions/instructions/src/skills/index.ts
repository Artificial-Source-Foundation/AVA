/**
 * Skills extension — auto-invoked knowledge modules.
 *
 * Skills activate based on file globs and project type.
 * Auto-discovers SKILL.md files from .ava/skills/, .claude/skills/, .agents/skills/.
 * Listens for skill registration and agent turn events.
 * Injects matched skill content into the system prompt via addPromptSection().
 *
 * Activation modes:
 * - always: injected on session open, persists every turn
 * - auto: glob-matched per turn (default)
 * - agent: listed in catalog for model awareness
 * - manual: only injected by explicit configuration
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { addPromptSection } from '../../../prompts/src/builder.js'
import { discoverSkills } from './loader.js'
import { matchSkills } from './matcher.js'
import type { Skill, SkillConfig } from './types.js'
import { DEFAULT_SKILL_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_SKILL_CONFIG,
    ...api.getSettings<Partial<SkillConfig>>('skills'),
  }
  const skills: Skill[] = []
  const disposables: Disposable[] = []

  // Track prompt section cleanups for lifecycle management
  const alwaysCleanups: Array<() => void> = []
  let turnCleanups: Array<() => void> = []
  let catalogCleanup: (() => void) | null = null

  /** Inject an always-active skill into the prompt. */
  function injectAlwaysSkill(skill: Skill): void {
    const cleanup = addPromptSection({
      name: `skill:${skill.name}`,
      priority: 150,
      content: `<skill name="${skill.name}">\n${skill.content}\n</skill>`,
    })
    alwaysCleanups.push(cleanup)
  }

  /** Rebuild the agent-invocable skill catalog prompt section. */
  function rebuildCatalog(): void {
    if (catalogCleanup) catalogCleanup()
    const agentSkills = skills.filter((s) => s.activation === 'agent')
    if (agentSkills.length === 0) {
      catalogCleanup = null
      return
    }
    const lines = ['Available skills:']
    for (const s of agentSkills) {
      lines.push(`- ${s.name}: ${s.description}`)
    }
    catalogCleanup = addPromptSection({
      name: 'skill-catalog',
      priority: 145,
      content: lines.join('\n'),
    })
  }

  // Listen for skill registration from plugins
  disposables.push(
    api.on('skills:register', (data) => {
      const skill = data as Skill
      skills.push(skill)
      api.log.debug(`Skill registered: ${skill.name}`)

      // Handle always skills immediately
      if (skill.activation === 'always') {
        injectAlwaysSkill(skill)
      }
      // Rebuild catalog if agent skill added
      if (skill.activation === 'agent') {
        rebuildCatalog()
      }
    })
  )

  // Auto-discover SKILL.md files on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      void discoverSkills(workingDirectory, api.platform.fs).then((discovered) => {
        let hasAgent = false
        for (const skill of discovered) {
          skills.push(skill)
          api.log.debug(`Skill discovered: ${skill.name} (${skill.source})`)

          if (skill.activation === 'always') {
            injectAlwaysSkill(skill)
          }
          if (skill.activation === 'agent') {
            hasAgent = true
          }
        }
        if (hasAgent) rebuildCatalog()
        if (discovered.length > 0) {
          api.emit('skills:discovered', { count: discovered.length, skills: discovered })
        }
      })
    })
  )

  // On each turn, match auto skills against current files and inject into prompt
  disposables.push(
    api.on('agent:turn-start', (data) => {
      const { files } = data as { sessionId: string; files?: string[] }

      // Clean up previous turn's auto-skill sections
      for (const cleanup of turnCleanups) cleanup()
      turnCleanups = []

      if (!files?.length || skills.length === 0) return

      const matches = matchSkills(skills, files, config)
      if (matches.length > 0) {
        api.emit('skills:matched', { matches })

        for (const match of matches) {
          const cleanup = addPromptSection({
            name: `skill:${match.skill.name}`,
            priority: 150,
            content: `<skill name="${match.skill.name}">\n${match.skill.content}\n</skill>`,
          })
          turnCleanups.push(cleanup)
        }

        api.log.debug(`Injected ${matches.length} skill(s) into prompt`)
      }
    })
  )

  api.log.debug('Skills extension activated')

  return {
    dispose() {
      for (const cleanup of alwaysCleanups) cleanup()
      for (const cleanup of turnCleanups) cleanup()
      if (catalogCleanup) catalogCleanup()
      turnCleanups = []
      catalogCleanup = null
      for (const d of disposables) d.dispose()
      skills.length = 0
    },
  }
}
