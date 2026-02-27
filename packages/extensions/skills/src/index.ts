/**
 * Skills extension — auto-invoked knowledge modules.
 *
 * Skills activate based on file globs and project type.
 * Listens for skill registration and agent turn events.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
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

  // Listen for skill registration from plugins
  disposables.push(
    api.on('skills:register', (data) => {
      const skill = data as Skill
      skills.push(skill)
      api.log.debug(`Skill registered: ${skill.name}`)
    })
  )

  // On each turn, match skills against current files
  disposables.push(
    api.on('agent:turn-start', (data) => {
      const { files } = data as { sessionId: string; files?: string[] }
      if (!files?.length || skills.length === 0) return

      const matches = matchSkills(skills, files, config)
      if (matches.length > 0) {
        api.emit('skills:matched', { matches })
      }
    })
  )

  api.log.debug('Skills extension activated')

  return {
    dispose() {
      for (const d of disposables) d.dispose()
      skills.length = 0
    },
  }
}
