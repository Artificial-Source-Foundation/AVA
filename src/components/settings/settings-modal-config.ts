import {
  Bot,
  Code2,
  Cpu,
  Info,
  Keyboard,
  Monitor,
  Palette,
  Puzzle,
  Server,
  type Settings,
  Sliders,
  Zap,
} from 'lucide-solid'

export type SettingsTab =
  | 'general'
  | 'appearance'
  | 'behavior'
  | 'shortcuts'
  | 'providers'
  | 'llm'
  | 'models'
  | 'mcp'
  | 'plugins'
  | 'developer'
  | 'about'

export interface TabGroup {
  label: string
  tabs: { id: SettingsTab; label: string; icon: typeof Settings }[]
}

export const tabGroups: TabGroup[] = [
  {
    label: 'Desktop',
    tabs: [
      { id: 'general', label: 'General', icon: Monitor },
      { id: 'appearance', label: 'Appearance', icon: Palette },
      { id: 'behavior', label: 'Behavior', icon: Sliders },
      { id: 'shortcuts', label: 'Shortcuts', icon: Keyboard },
    ],
  },
  {
    label: 'AI',
    tabs: [
      { id: 'providers', label: 'Providers', icon: Zap },
      { id: 'llm', label: 'LLM', icon: Cpu },
      { id: 'models', label: 'Agents', icon: Bot },
    ],
  },
  {
    label: 'Extensions',
    tabs: [
      { id: 'mcp', label: 'MCP Servers', icon: Server },
      { id: 'plugins', label: 'Plugins', icon: Puzzle },
    ],
  },
  {
    label: 'Advanced',
    tabs: [{ id: 'developer', label: 'Developer', icon: Code2 }],
  },
  {
    label: '',
    tabs: [{ id: 'about', label: 'About', icon: Info }],
  },
]

export const AVAILABLE_MODELS = [
  { id: '', label: 'Use default' },
  { id: 'claude-opus-4-6', label: 'Claude Opus 4.6' },
  { id: 'claude-sonnet-4-5-20250929', label: 'Claude Sonnet 4.5' },
  { id: 'claude-haiku-4-5-20251001', label: 'Claude Haiku 4.5' },
  { id: 'gpt-4.1', label: 'GPT-4.1' },
  { id: 'gpt-4.1-mini', label: 'GPT-4.1 Mini' },
  { id: 'gpt-4.1-nano', label: 'GPT-4.1 Nano' },
  { id: 'o3', label: 'o3' },
  { id: 'o4-mini', label: 'o4-mini' },
  { id: 'gemini-2.5-pro', label: 'Gemini 2.5 Pro' },
  { id: 'gemini-2.5-flash', label: 'Gemini 2.5 Flash' },
  { id: 'deepseek-r1', label: 'DeepSeek R1' },
  { id: 'deepseek-v3', label: 'DeepSeek V3' },
  { id: 'qwen-2.5-coder', label: 'Qwen 2.5 Coder' },
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
