/**
 * load_skill tool — allows the agent to load a skill by name.
 *
 * When called, injects the skill content as a prompt section (priority 150)
 * so it persists beyond the tool result. Loadable skills: agent and manual
 * activation modes (auto/always are already injected automatically).
 */

import { defineTool } from '@ava/core-v2/tools'
import * as z from 'zod'
import { addPromptSection } from '../../prompts/src/builder.js'
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

      // Only allow loading agent/manual skills — auto/always are already injected
      const activation = skill.activation ?? 'auto'
      if (activation === 'auto' || activation === 'always') {
        return {
          success: true,
          output: `Skill "${skill.name}" is already active (activation: ${activation}). Content:\n\n${skill.content}`,
        }
      }

      // Inject as prompt section so it persists beyond this tool result
      addPromptSection({
        name: `skill:${skill.name}`,
        priority: 150,
        content: `<skill name="${skill.name}">\n${skill.content}\n</skill>`,
      })

      return { success: true, output: skill.content }
    },
  })
}
