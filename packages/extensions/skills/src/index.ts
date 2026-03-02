/**
 * Skills extension — auto-invoked knowledge modules.
 *
 * Skills activate based on file globs and project type.
 * Auto-discovers SKILL.md files from .ava/skills/, .claude/skills/, .agents/skills/.
 * Listens for skill registration and agent turn events.
 */

import type { Disposable, ExtensionAPI } from '@ava/core-v2/extensions'
import { discoverSkills } from './loader.js'
import { matchSkills } from './matcher.js'
import { createLoadSkillTool } from './tool.js'
import type { Skill, SkillConfig } from './types.js'
import { DEFAULT_SKILL_CONFIG } from './types.js'

export function activate(api: ExtensionAPI): Disposable {
  const config = {
    ...DEFAULT_SKILL_CONFIG,
    ...api.getSettings<Partial<SkillConfig>>('skills'),
  }
  const skills: Skill[] = []
  const disposables: Disposable[] = []

  // Register load_skill tool (shares the skills array reference)
  disposables.push(api.registerTool(createLoadSkillTool(skills)))

  // Listen for skill registration from plugins
  disposables.push(
    api.on('skills:register', (data) => {
      const skill = data as Skill
      skills.push(skill)
      api.log.debug(`Skill registered: ${skill.name}`)
    })
  )

  // Auto-discover SKILL.md files on session open
  disposables.push(
    api.on('session:opened', (data) => {
      const { workingDirectory } = data as { sessionId: string; workingDirectory: string }
      void discoverSkills(workingDirectory, api.platform.fs).then((discovered) => {
        for (const skill of discovered) {
          skills.push(skill)
          api.log.debug(`Skill discovered: ${skill.name} (${skill.source})`)
        }
        if (discovered.length > 0) {
          api.emit('skills:discovered', { count: discovered.length, skills: discovered })
        }
      })
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
