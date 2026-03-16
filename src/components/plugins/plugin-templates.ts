/**
 * Plugin Templates — data + code generation helpers
 *
 * Template definitions and scaffold code generators for the PluginWizard.
 *
 * NOTE: The @ava/core-v2/extensions imports below appear inside template string
 * literals (generated code for plugin authors). They reference the extension API
 * that plugins use, NOT runtime imports in this module.
 */

import { Code2, type Puzzle, Terminal, Wand2, Zap } from 'lucide-solid'

export interface PluginTemplate {
  id: string
  name: string
  description: string
  icon: typeof Puzzle
  files: string[]
}

export const TEMPLATES: PluginTemplate[] = [
  {
    id: 'tool',
    name: 'Custom Tool',
    description: 'Register a new tool that agents can use during their work.',
    icon: Terminal,
    files: ['src/index.ts', 'ava-extension.json', 'package.json'],
  },
  {
    id: 'command',
    name: 'Slash Command',
    description: 'Add a /command that users can invoke from the chat input.',
    icon: Code2,
    files: ['src/index.ts', 'ava-extension.json', 'package.json'],
  },
  {
    id: 'provider',
    name: 'LLM Provider',
    description: 'Integrate a new LLM provider with the multi-model system.',
    icon: Zap,
    files: ['src/index.ts', 'src/client.ts', 'ava-extension.json', 'package.json'],
  },
  {
    id: 'skill',
    name: 'Context Skill',
    description: 'Auto-invoked instructions based on file patterns and project context.',
    icon: Wand2,
    files: ['src/index.ts', 'skill.md', 'ava-extension.json', 'package.json'],
  },
]

export type WizardStep = 'template' | 'configure' | 'preview'

export function kebabCase(s: string): string {
  return s
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-|-$/g, '')
}

export function generateManifest(
  template: PluginTemplate | undefined,
  pluginName: string,
  pluginDescription: string,
  pluginAuthor: string
): string {
  return JSON.stringify(
    {
      name: kebabCase(pluginName),
      version: '0.1.0',
      description: pluginDescription,
      author: pluginAuthor || undefined,
      main: 'src/index.ts',
      type: template?.id,
      permissions: template?.id === 'tool' ? ['fs'] : [],
    },
    null,
    2
  )
}

export function generateIndexTs(
  template: PluginTemplate | undefined,
  pluginName: string,
  pluginDescription: string
): string {
  if (!template) return ''
  const name = kebabCase(pluginName)

  switch (template.id) {
    case 'tool':
      return `import type { ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI) {
  return api.registerTool({
    definition: {
      name: '${name}',
      description: '${pluginDescription}',
      input_schema: {
        type: 'object',
        properties: {
          input: { type: 'string', description: 'Input value' },
        },
        required: ['input'],
      },
    },
    async execute(args) {
      return { success: true, output: \`Executed ${name} with: \${args.input}\` }
    },
  })
}`
    case 'command':
      return `import type { ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI) {
  return api.registerCommand({
    name: '${name}',
    description: '${pluginDescription}',
    async execute(args, ctx) {
      ctx.addMessage({ role: 'system', content: 'Running ${name}...' })
    },
  })
}`
    case 'provider':
      return `import type { ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI) {
  return api.registerProvider('${name}', () => ({
    async chat(messages, options) {
      // Implement your provider logic here
      return { content: 'Hello from ${name}!' }
    },
  }))
}`
    case 'skill':
      return `import type { ExtensionAPI } from '@ava/core-v2/extensions'

export function activate(api: ExtensionAPI) {
  // Skills are auto-invoked based on file patterns
  api.on('session:start', () => {
    api.log.info('${name} skill activated')
  })
  return { dispose() {} }
}`
    default:
      return ''
  }
}
