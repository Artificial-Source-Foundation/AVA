import {
  BarChart3,
  Code2,
  Cpu,
  Monitor,
  Palette,
  Puzzle,
  Server,
  type Settings,
  ShieldCheck,
  Sparkles,
  Zap,
} from 'lucide-solid'

export type SettingsTab =
  | 'general'
  | 'appearance'
  | 'providers'
  | 'advanced'
  | 'permissions-trust'
  | 'mcp'
  | 'plugins'
  | 'skills'
  | 'llm'
  | 'usage'

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
    label: 'General',
    tabs: [
      {
        id: 'general',
        label: 'General',
        icon: Monitor,
        keywords: [
          'language',
          'updates',
          'startup',
          'default',
          'behavior',
          'scroll',
          'notification',
          'send key',
          'word wrap',
        ],
      },
    ],
  },
  {
    label: 'Models',
    tabs: [
      {
        id: 'providers',
        label: 'Providers',
        icon: Zap,
        keywords: ['api', 'key', 'openai', 'anthropic', 'gemini', 'connection'],
      },
      {
        id: 'usage',
        label: 'Usage',
        icon: BarChart3,
        keywords: ['usage', 'quota', 'subscription', 'credits', 'plan', 'limit', 'balance'],
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
    label: 'Tools',
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
        label: 'Skills & Rules',
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
          'rule',
          'coding-rule',
          'always',
          'auto',
          'command',
          'slash',
          'custom',
          'toml',
        ],
      },
    ],
  },
  {
    label: 'Permissions',
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
    label: 'Appearance',
    tabs: [
      {
        id: 'appearance',
        label: 'Appearance',
        icon: Palette,
        keywords: ['theme', 'font', 'color', 'dark', 'light', 'density', 'glass'],
      },
    ],
  },
  {
    label: 'Advanced',
    tabs: [
      {
        id: 'advanced',
        label: 'Advanced',
        icon: Code2,
        keywords: [
          'advanced',
          'agent',
          'preset',
          'capability',
          'assistant',
          'automation',
          'debug',
          'logs',
          'devtools',
          'developer',
          'version',
          'license',
          'credits',
        ],
      },
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

  // General behavior
  {
    label: 'Send message with',
    description: 'Enter or Ctrl+Enter',
    tab: 'general',
    tabLabel: 'General',
  },
  { label: 'Auto-scroll to new messages', tab: 'general', tabLabel: 'General' },
  { label: 'Auto-title sessions', tab: 'general', tabLabel: 'General' },
  {
    label: 'Line numbers',
    description: 'Code block line numbers',
    tab: 'general',
    tabLabel: 'General',
  },
  {
    label: 'Word wrap',
    description: 'Wrap long lines in code blocks',
    tab: 'general',
    tabLabel: 'General',
  },
  {
    label: 'Tool response style',
    description: 'Concise or detailed',
    tab: 'general',
    tabLabel: 'General',
  },
  {
    label: 'Auto-update',
    description: 'Check for updates on startup',
    tab: 'general',
    tabLabel: 'General',
  },
  { label: 'Desktop notifications', tab: 'general', tabLabel: 'General' },
  { label: 'Sound on completion', tab: 'general', tabLabel: 'General' },
  { label: 'Notification volume', tab: 'general', tabLabel: 'General' },

  // General shortcuts
  {
    label: 'Keyboard shortcuts',
    description: 'Search, edit, and reset shortcut bindings',
    tab: 'general',
    tabLabel: 'General',
  },
  { label: 'Shortcut search', tab: 'general', tabLabel: 'General' },
  { label: 'Reset shortcuts', tab: 'general', tabLabel: 'General' },

  // Skills
  {
    label: 'Skills',
    description: 'Context-aware instruction modules',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Built-in skills',
    description: 'Language-specific prompt modules',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Custom skills',
    description: 'User-defined instruction modules',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Skill file globs',
    description: 'File patterns that activate skills',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Skill sources',
    description: 'Directories where skills are loaded from',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Rules',
    description: 'Project-specific instruction files',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Custom commands',
    description: '/slash command templates and parameters',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Allowed tools for commands',
    description: 'Restrict tool access per command',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },
  {
    label: 'Command mode',
    description: 'Normal vs plan mode',
    tab: 'skills',
    tabLabel: 'Skills & Rules',
  },

  // Providers
  {
    label: 'API keys',
    description: 'Connect provider credentials and defaults',
    tab: 'providers',
    tabLabel: 'Providers',
  },
  {
    label: 'Base URL',
    description: 'Custom endpoint for a provider',
    tab: 'providers',
    tabLabel: 'Providers',
  },
  {
    label: 'Provider connection test',
    description: 'Validate provider credentials and available models',
    tab: 'providers',
    tabLabel: 'Providers',
  },

  // MCP
  {
    label: 'MCP servers',
    description: 'Manage stdio and remote MCP integrations',
    tab: 'mcp',
    tabLabel: 'MCP Servers',
  },
  {
    label: 'Add MCP server',
    description: 'Register a new MCP server connection',
    tab: 'mcp',
    tabLabel: 'MCP Servers',
  },
  {
    label: 'Server transport',
    description: 'stdio, command, args, and connection details',
    tab: 'mcp',
    tabLabel: 'MCP Servers',
  },

  // Plugins
  {
    label: 'Plugins',
    description: 'Install, enable, and inspect plugins',
    tab: 'plugins',
    tabLabel: 'Plugins',
  },
  {
    label: 'Plugin permissions',
    description: 'Review requested plugin access before install',
    tab: 'plugins',
    tabLabel: 'Plugins',
  },
  {
    label: 'Plugin dev mode',
    description: 'Watch, reload, and inspect local plugin development',
    tab: 'plugins',
    tabLabel: 'Plugins',
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

  // Advanced
  {
    label: 'Developer mode',
    description: 'Developer console and log verbosity',
    tab: 'advanced',
    tabLabel: 'Advanced',
  },
  {
    label: 'Log level',
    description: 'DEBUG, INFO, WARN, ERROR',
    tab: 'advanced',
    tabLabel: 'Advanced',
  },
  {
    label: 'Console output',
    description: 'Live log viewer',
    tab: 'advanced',
    tabLabel: 'Advanced',
  },
  {
    label: 'File logs',
    description: 'Persistent logs across sessions',
    tab: 'advanced',
    tabLabel: 'Advanced',
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
