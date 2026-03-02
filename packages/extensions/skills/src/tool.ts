/**
 * load_skill tool — allows the agent to load a skill by name.
 */

import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'
import type { Skill } from './types.js'

export function createLoadSkillTool(skills: Skill[]) {
  return defineTool({
    name: 'load_skill',
    description: 'Load a skill by name. Returns the skill content for the agent to use.',
    schema: z.object({
      name: z.string().describe('Name of the skill to load'),
    }),
    async execute(input) {
      const skill = skills.find(
        (s) => s.name === input.name || s.name.toLowerCase() === input.name.toLowerCase()
      )
      if (!skill) {
        const available = skills.map((s) => s.name).join(', ')
        return {
          success: false,
          output: '',
          error: `Skill "${input.name}" not found. Available: ${available || 'none'}`,
        }
      }
      return { success: true, output: skill.content }
    },
  })
}
