import {
  Bot,
  Brain,
  Code2,
  Cpu,
  Info,
  Keyboard,
  Monitor,
  Palette,
  Puzzle,
  Server,
  type Settings,
  ShieldCheck,
  Sliders,
  Users,
  Zap,
} from 'lucide-solid'

export type SettingsTab =
  | 'general'
  | 'appearance'
  | 'behavior'
  | 'shortcuts'
  | 'providers'
  | 'agents'
  | 'permissions-trust'
  | 'mcp'
  | 'plugins'
  | 'skills-commands'
  | 'llm'
  | 'team'
  | 'developer'
  | 'about'

export interface TabConfig {
  id: SettingsTab
  label: string
  icon: typeof Settings
  keywords: string[]
}

export interface TabGroup {
  label: string
  tabs: TabConfig[]
}

export const tabGroups: TabGroup[] = [
  {
    label: 'Desktop',
    tabs: [
      {
        id: 'general',
        label: 'General',
        icon: Monitor,
        keywords: ['language', 'updates', 'startup', 'default'],
      },
      {
        id: 'appearance',
        label: 'Appearance',
        icon: Palette,
        keywords: ['theme', 'font', 'color', 'dark', 'light', 'density', 'glass'],
      },
      {
        id: 'behavior',
        label: 'Behavior',
        icon: Sliders,
        keywords: ['auto', 'save', 'scroll', 'confirm', 'notification'],
      },
      {
        id: 'shortcuts',
        label: 'Shortcuts',
        icon: Keyboard,
        keywords: ['keyboard', 'hotkey', 'keybinding', 'keys', 'shortcut'],
      },
    ],
  },
  {
    label: 'AI',
    tabs: [
      {
        id: 'providers',
        label: 'Providers',
        icon: Zap,
        keywords: ['api', 'key', 'openai', 'anthropic', 'google', 'connection'],
      },
      {
        id: 'agents',
        label: 'Agents',
        icon: Bot,
        keywords: ['agent', 'team', 'worker', 'preset', 'capability'],
      },
      {
        id: 'team',
        label: 'Team',
        icon: Users,
        keywords: [
          'team',
          'praxis',
          'multi-agent',
          'director',
          'lead',
          'worker',
          'scout',
          'delegation',
        ],
      },
      {
        id: 'llm',
        label: 'Generation',
        icon: Cpu,
        keywords: [
          'llm',
          'generation',
          'temperature',
          'tokens',
          'top-p',
          'reasoning',
          'compaction',
          'weak',
          'editor',
          'model',
          'limits',
          'instructions',
        ],
      },
    ],
  },
  {
    label: 'Extensions',
    tabs: [
      {
        id: 'mcp',
        label: 'MCP Servers',
        icon: Server,
        keywords: ['server', 'protocol', 'stdio', 'transport'],
      },
      {
        id: 'plugins',
        label: 'Plugins',
        icon: Puzzle,
        keywords: ['extension', 'install', 'community', 'marketplace'],
      },
      {
        id: 'skills-commands',
        label: 'Skills & Commands',
        icon: Brain,
        keywords: [
          'skill',
          'domain',
          'prompt',
          'glob',
          'pattern',
          'microagent',
          'rule',
          'coding-rule',
          'always',
          'auto',
          'activation',
          'command',
          'slash',
          'custom',
          'toml',
        ],
      },
    ],
  },
  {
    label: 'Security',
    tabs: [
      {
        id: 'permissions-trust',
        label: 'Permissions & Trust',
        icon: ShieldCheck,
        keywords: [
          'permission',
          'approve',
          'deny',
          'allow',
          'tool',
          'rules',
          'safety',
          'folder',
          'directory',
          'trust',
          'path',
          'boundary',
        ],
      },
    ],
  },
  {
    label: '',
    tabs: [
      {
        id: 'developer',
        label: 'Developer',
        icon: Code2,
        keywords: ['debug', 'logs', 'devtools', 'advanced'],
      },
      { id: 'about', label: 'About', icon: Info, keywords: ['version', 'license', 'credits'] },
    ],
  },
]

export const ALL_CAPABILITIES = [
  'code-generation',
  'debugging',
  'refactoring',
  'code-review',
  'git-status',
  'commit-messages',
  'branch-management',
  'merge-resolution',
  'command-execution',
  'process-management',
  'environment-setup',
  'readme',
  'api-docs',
  'comments',
  'tutorials',
  'quick-answers',
  'simple-tasks',
  'web-search',
  'file-management',
  'testing',
  'security-analysis',
  'performance-optimization',
]
