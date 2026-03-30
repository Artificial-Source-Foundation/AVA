import {
  BarChart3,
  Bot,
  Brain,
  Building2,
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
  Sparkles,
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
  | 'skills'
  | 'skills-commands'
  | 'llm'
  | 'hq'
  | 'usage'
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
    label: 'APP',
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
        id: 'usage',
        label: 'Usage',
        icon: BarChart3,
        keywords: ['usage', 'quota', 'subscription', 'credits', 'plan', 'limit', 'balance'],
      },
      {
        id: 'agents',
        label: 'Agents',
        icon: Bot,
        keywords: ['agent', 'preset', 'capability', 'assistant', 'automation'],
      },
      {
        id: 'skills',
        label: 'Skills',
        icon: Sparkles,
        keywords: [
          'skill',
          'domain',
          'prompt',
          'glob',
          'pattern',
          'microagent',
          'activation',
          'instructions',
          'context',
          'file',
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
    label: 'HQ',
    tabs: [
      {
        id: 'hq',
        label: 'HQ',
        icon: Building2,
        keywords: [
          'hq',
          'director',
          'team',
          'lead',
          'worker',
          'epic',
          'kanban',
          'orchestration',
          'review',
          'cost',
        ],
      },
    ],
  },
  {
    label: 'TOOLS',
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
        label: 'Rules & Commands',
        icon: Brain,
        keywords: [
          'rule',
          'coding-rule',
          'always',
          'auto',
          'activation',
          'command',
          'slash',
          'custom',
          'toml',
          'prompt',
        ],
      },
    ],
  },
  {
    label: 'SECURITY',
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
    label: 'OTHER',
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

/** Individual settings indexed for deep search. Each entry maps to a tab. */
export interface SettingsSearchEntry {
  label: string
  description?: string
  tab: SettingsTab
  tabLabel: string
}

export const settingsSearchIndex: SettingsSearchEntry[] = [
  // General
  { label: 'Show memory panel on start', tab: 'general', tabLabel: 'General' },
  { label: 'Show agent activity panel', tab: 'general', tabLabel: 'General' },
  { label: 'Compact message layout', tab: 'general', tabLabel: 'General' },
  { label: 'Show token count', tab: 'general', tabLabel: 'General' },
  { label: 'Show model in title bar', tab: 'general', tabLabel: 'General' },
  {
    label: 'Auto-fix lint errors',
    description: 'Run linter after file changes',
    tab: 'general',
    tabLabel: 'General',
  },
  {
    label: 'Git integration',
    description: 'Auto-commit, commit prefix',
    tab: 'general',
    tabLabel: 'General',
  },
  { label: 'Auto-commit AI edits', tab: 'general', tabLabel: 'General' },
  { label: 'Commit prefix', tab: 'general', tabLabel: 'General' },
  { label: 'Watch for AI comments', tab: 'general', tabLabel: 'General' },
  { label: 'Clipboard watcher', tab: 'general', tabLabel: 'General' },
  { label: 'Export settings', tab: 'general', tabLabel: 'General' },
  { label: 'Import settings', tab: 'general', tabLabel: 'General' },
  { label: 'Clear all data', tab: 'general', tabLabel: 'General' },

  // Appearance
  {
    label: 'Color mode',
    description: 'Dark, light theme variant',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  { label: 'Theme presets', tab: 'appearance', tabLabel: 'Appearance' },
  { label: 'Accent color', tab: 'appearance', tabLabel: 'Appearance' },
  {
    label: 'Thinking display',
    description: 'Bubble, preview, hidden',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  {
    label: 'Agent activity display',
    description: 'Collapsed, expanded, hidden',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  {
    label: 'Interface scale',
    description: 'UI zoom level',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  {
    label: 'Border radius',
    description: 'Corner rounding',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  {
    label: 'UI density',
    description: 'Spacing between elements',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  { label: 'Font size', tab: 'appearance', tabLabel: 'Appearance' },
  {
    label: 'Font family',
    description: 'UI and monospace fonts',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  {
    label: 'Code theme',
    description: 'Syntax highlighting',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  {
    label: 'Accessibility',
    description: 'High contrast, reduced motion',
    tab: 'appearance',
    tabLabel: 'Appearance',
  },
  { label: 'Sidebar order', tab: 'appearance', tabLabel: 'Appearance' },

  // Behavior
  {
    label: 'Send message with',
    description: 'Enter or Ctrl+Enter',
    tab: 'behavior',
    tabLabel: 'Behavior',
  },
  { label: 'Auto-scroll to new messages', tab: 'behavior', tabLabel: 'Behavior' },
  { label: 'Auto-title sessions', tab: 'behavior', tabLabel: 'Behavior' },
  {
    label: 'Line numbers',
    description: 'Code block line numbers',
    tab: 'behavior',
    tabLabel: 'Behavior',
  },
  {
    label: 'Word wrap',
    description: 'Wrap long lines in code blocks',
    tab: 'behavior',
    tabLabel: 'Behavior',
  },
  {
    label: 'Tool response style',
    description: 'Concise or detailed',
    tab: 'behavior',
    tabLabel: 'Behavior',
  },
  {
    label: 'Auto-update',
    description: 'Check for updates on startup',
    tab: 'behavior',
    tabLabel: 'Behavior',
  },
  { label: 'Desktop notifications', tab: 'behavior', tabLabel: 'Behavior' },
  { label: 'Sound on completion', tab: 'behavior', tabLabel: 'Behavior' },
  { label: 'Notification volume', tab: 'behavior', tabLabel: 'Behavior' },

  // Skills
  {
    label: 'Skills',
    description: 'Context-aware instruction modules',
    tab: 'skills',
    tabLabel: 'Skills',
  },
  {
    label: 'Built-in skills',
    description: 'Language-specific prompt modules',
    tab: 'skills',
    tabLabel: 'Skills',
  },
  {
    label: 'Custom skills',
    description: 'User-defined instruction modules',
    tab: 'skills',
    tabLabel: 'Skills',
  },
  {
    label: 'Skill file globs',
    description: 'File patterns that activate skills',
    tab: 'skills',
    tabLabel: 'Skills',
  },
  {
    label: 'Skill sources',
    description: 'Directories where skills are loaded from',
    tab: 'skills',
    tabLabel: 'Skills',
  },

  // Generation (LLM)
  { label: 'Max tokens', tab: 'llm', tabLabel: 'Generation' },
  { label: 'Temperature', tab: 'llm', tabLabel: 'Generation' },
  { label: 'Top P', tab: 'llm', tabLabel: 'Generation' },
  {
    label: 'Secondary model',
    description: 'Cheaper model for planning and review',
    tab: 'llm',
    tabLabel: 'Generation',
  },
  {
    label: 'Editor model',
    description: 'Model for file edits and code generation',
    tab: 'llm',
    tabLabel: 'Generation',
  },
  {
    label: 'Max turns',
    description: 'Maximum turns per agent run',
    tab: 'llm',
    tabLabel: 'Generation',
  },
  {
    label: 'Max time',
    description: 'Maximum time per agent run',
    tab: 'llm',
    tabLabel: 'Generation',
  },
  {
    label: 'Custom instructions',
    description: 'System message prepended to every request',
    tab: 'llm',
    tabLabel: 'Generation',
  },
  {
    label: 'Context compaction',
    description: 'Auto-compress old messages',
    tab: 'llm',
    tabLabel: 'Generation',
  },
  { label: 'Compaction threshold', tab: 'llm', tabLabel: 'Generation' },
  {
    label: 'Model aliases',
    description: 'Short names for models',
    tab: 'llm',
    tabLabel: 'Generation',
  },

  // Permissions & Trust
  {
    label: 'Permission mode',
    description: 'Ask, auto, strict, balanced, YOLO',
    tab: 'permissions-trust',
    tabLabel: 'Permissions & Trust',
  },
  {
    label: 'Tool rules',
    description: 'Per-tool approval rules',
    tab: 'permissions-trust',
    tabLabel: 'Permissions & Trust',
  },
  { label: 'Always-approved tools', tab: 'permissions-trust', tabLabel: 'Permissions & Trust' },
  {
    label: 'Trusted folders',
    description: 'Allowed and denied directories',
    tab: 'permissions-trust',
    tabLabel: 'Permissions & Trust',
  },
  { label: 'Allowed directories', tab: 'permissions-trust', tabLabel: 'Permissions & Trust' },
  { label: 'Denied directories', tab: 'permissions-trust', tabLabel: 'Permissions & Trust' },

  // Usage
  {
    label: 'Subscription usage',
    description: 'Plan tiers, quotas, credits',
    tab: 'usage',
    tabLabel: 'Usage',
  },
  { label: 'OpenAI quota', description: 'ChatGPT Pro/Plus usage', tab: 'usage', tabLabel: 'Usage' },
  {
    label: 'Copilot quota',
    description: 'GitHub Copilot premium requests',
    tab: 'usage',
    tabLabel: 'Usage',
  },
  {
    label: 'OpenRouter credits',
    description: 'Balance remaining',
    tab: 'usage',
    tabLabel: 'Usage',
  },

  // HQ
  { label: 'Director model', tab: 'hq', tabLabel: 'HQ' },
  { label: 'Tone preference', description: 'Technical or simple', tab: 'hq', tabLabel: 'HQ' },
  { label: 'Auto review', description: 'QA review after each phase', tab: 'hq', tabLabel: 'HQ' },
  { label: 'Cost estimates', tab: 'hq', tabLabel: 'HQ' },

  // Developer
  {
    label: 'Developer mode',
    description: 'Developer console and log verbosity',
    tab: 'developer',
    tabLabel: 'Developer',
  },
  {
    label: 'Log level',
    description: 'DEBUG, INFO, WARN, ERROR',
    tab: 'developer',
    tabLabel: 'Developer',
  },
  {
    label: 'Console output',
    description: 'Live log viewer',
    tab: 'developer',
    tabLabel: 'Developer',
  },
  {
    label: 'File logs',
    description: 'Persistent logs across sessions',
    tab: 'developer',
    tabLabel: 'Developer',
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
