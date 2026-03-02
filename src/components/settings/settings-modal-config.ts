import {
  Bot,
  Brain,
  Code2,
  Cpu,
  FolderLock,
  Info,
  Keyboard,
  Monitor,
  Palette,
  Puzzle,
  Server,
  type Settings,
  Shield,
  Sliders,
  Terminal,
  Zap,
} from 'lucide-solid'

export type SettingsTab =
  | 'general'
  | 'appearance'
  | 'behavior'
  | 'shortcuts'
  | 'providers'
  | 'models'
  | 'agents'
  | 'permissions'
  | 'mcp'
  | 'plugins'
  | 'skills'
  | 'commands'
  | 'trusted-folders'
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
      {
        id: 'permissions',
        label: 'Permissions',
        icon: Shield,
        keywords: ['permission', 'approve', 'deny', 'allow', 'tool', 'rules', 'safety'],
      },
      {
        id: 'trusted-folders',
        label: 'Trusted Folders',
        icon: FolderLock,
        keywords: ['folder', 'directory', 'trust', 'allow', 'deny', 'path', 'boundary'],
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
        id: 'models',
        label: 'Models',
        icon: Cpu,
        keywords: ['model', 'temperature', 'tokens', 'context', 'streaming', 'llm'],
      },
      {
        id: 'agents',
        label: 'Agents',
        icon: Bot,
        keywords: ['agent', 'team', 'worker', 'preset', 'capability'],
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
        id: 'skills',
        label: 'Skills',
        icon: Brain,
        keywords: ['skill', 'domain', 'prompt', 'glob', 'pattern', 'microagent'],
      },
      {
        id: 'commands',
        label: 'Commands',
        icon: Terminal,
        keywords: ['command', 'slash', 'custom', 'toml'],
      },
    ],
  },
  {
    label: 'Advanced',
    tabs: [
      {
        id: 'developer',
        label: 'Developer',
        icon: Code2,
        keywords: ['debug', 'logs', 'devtools', 'advanced'],
      },
    ],
  },
  {
    label: '',
    tabs: [
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
