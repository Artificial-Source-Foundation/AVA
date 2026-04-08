/**
 * Plugin Templates — data + code generation helpers
 *
 * Template definitions and scaffold code generators for the PluginWizard.
 * Templates mirror the current `@ava-ai/plugin` SDK and `plugin.toml` runtime manifest.
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
    name: 'Tool Hook',
    description: 'Inspect or modify existing tool calls with plugin hooks.',
    icon: Terminal,
    files: ['index.ts', 'plugin.toml', 'package.json'],
  },
  {
    id: 'command',
    name: 'Slash Command',
    description: 'Expose a plugin-owned app command through the host seam.',
    icon: Code2,
    files: ['index.ts', 'plugin.toml', 'package.json'],
  },
  {
    id: 'provider',
    name: 'Provider Auth Hook',
    description: 'Supply auth or request headers for an existing provider path.',
    icon: Zap,
    files: ['index.ts', 'plugin.toml', 'package.json'],
  },
  {
    id: 'skill',
    name: 'Context Skill',
    description: 'Inject skill-like instructions into the system prompt at runtime.',
    icon: Wand2,
    files: ['index.ts', 'plugin.toml', 'package.json'],
  },
]

export type WizardStep = 'template' | 'configure' | 'preview'

export function kebabCase(s: string): string {
  return s
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '-')
    .replace(/^-|-$/g, '')
}

function hookListForTemplate(templateId: string | undefined): string[] {
  switch (templateId) {
    case 'tool':
      return ['tool.before', 'tool.after']
    case 'command':
      return ['session.start']
    case 'provider':
      return ['auth', 'request.headers']
    case 'skill':
      return ['chat.system', 'session.start']
    default:
      return []
  }
}

export function generateManifest(
  template: PluginTemplate | undefined,
  pluginName: string,
  pluginDescription: string,
  pluginAuthor: string
): string {
  const name = kebabCase(pluginName)
  const subscribe = hookListForTemplate(template?.id)
  const authorLine = pluginAuthor.trim() ? `author = "${pluginAuthor.trim()}"\n` : ''

  return `[plugin]
name = "${name}"
version = "0.1.0"
description = "${pluginDescription}"
${authorLine}[runtime]
command = "node"
args = ["index.js"]

[hooks]
subscribe = [${subscribe.map((hook) => `"${hook}"`).join(', ')}]
`
}

export function generateIndexTs(
  template: PluginTemplate | undefined,
  pluginName: string,
  pluginDescription: string
): string {
  if (!template) return ''
  const name = kebabCase(pluginName)
  const envVar = `${name.toUpperCase().replace(/-/g, '_')}_TOKEN`

  switch (template.id) {
    case 'tool':
      return `import { createPlugin } from '@ava-ai/plugin'

createPlugin({
  'tool.before': async (_ctx, params) => {
    if (params.tool === 'bash') {
      console.error('[${name}] inspecting bash invocation')
    }
    return { args: params.args ?? {} }
  },
  'tool.after': async (_ctx, params) => {
    console.error('[${name}] tool completed:', params.tool)
    return {}
  },
})`

    case 'command':
      return `import { createPlugin } from '@ava-ai/plugin'

createPlugin(
  {
    'session.start': async (_ctx, params) => {
      console.error('[${name}] session started:', params.session_id ?? 'unknown')
      return undefined
    },
  },
  {
    capabilities: {
      commands: [{ name: '${name}.run', description: '${pluginDescription}' }],
    },
    commands: {
      '${name}.run': async (ctx, payload) => ({
        result: { ok: true, project: ctx.project.name, payload },
      }),
    },
  }
)`

    case 'provider':
      return `import { createPlugin } from '@ava-ai/plugin'

createPlugin({
  auth: async () => ({
    token: process.env.${envVar} ?? '',
  }),
  'request.headers': async () => ({
    headers: {
      Authorization: \`Bearer \${process.env.${envVar} ?? ''}\`,
    },
  }),
})`

    case 'skill':
      return `import { createPlugin } from '@ava-ai/plugin'

createPlugin({
  'chat.system': async () => ({
    inject: 'You are using the ${name} skill. ${pluginDescription}',
  }),
  'session.start': async (_ctx, params) => {
    console.error('[${name}] skill active for session', params.session_id ?? 'unknown')
    return undefined
  },
})`

    default:
      return ''
  }
}
