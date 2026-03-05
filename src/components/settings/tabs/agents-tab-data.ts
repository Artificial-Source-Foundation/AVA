/**
 * Agents Tab — Shared Constants
 *
 * Tool categories (all 59 registered tools) and capability categories.
 * Used by agents-tab-detail.tsx for categorized chip toggles.
 */

export const TOOL_CATEGORIES = [
  {
    label: 'File',
    tools: [
      'read_file',
      'write_file',
      'create_file',
      'delete_file',
      'edit',
      'multiedit',
      'apply_patch',
    ],
  },
  { label: 'Search', tools: ['grep', 'glob', 'ls'] },
  { label: 'Shell', tools: ['bash', 'pty'] },
  { label: 'Web', tools: ['websearch', 'webfetch'] },
  { label: 'Comm', tools: ['question', 'attempt_completion'] },
  { label: 'Plan', tools: ['plan_enter', 'plan_exit', 'todoread', 'todowrite', 'task', 'batch'] },
  { label: 'Git', tools: ['create_pr', 'create_branch', 'switch_branch', 'read_issue'] },
  {
    label: 'LSP',
    tools: [
      'lsp_diagnostics',
      'lsp_hover',
      'lsp_definition',
      'lsp_references',
      'lsp_document_symbols',
      'lsp_workspace_symbols',
      'lsp_code_actions',
      'lsp_rename',
      'lsp_completions',
    ],
  },
  { label: 'Memory', tools: ['memory_read', 'memory_write', 'memory_list', 'memory_delete'] },
  { label: 'Other', tools: ['recall'] },
] as const

export const ALL_TOOLS = TOOL_CATEGORIES.flatMap((c) => c.tools)

export const CAPABILITY_CATEGORIES = [
  { label: 'Coding', items: ['code-generation', 'debugging', 'refactoring', 'code-review'] },
  {
    label: 'Git',
    items: ['git-status', 'commit-messages', 'branch-management', 'merge-resolution'],
  },
  { label: 'Terminal', items: ['command-execution', 'process-management', 'environment-setup'] },
  { label: 'Docs', items: ['readme', 'api-docs', 'comments', 'tutorials'] },
  { label: 'General', items: ['quick-answers', 'simple-tasks', 'web-search', 'file-management'] },
  { label: 'Quality', items: ['testing', 'security-analysis', 'performance-optimization'] },
  { label: 'Team', items: ['coordination', 'planning', 'delegation'] },
  {
    label: 'Special',
    items: [
      'codebase-exploration',
      'context-gathering',
      'error-diagnosis',
      'architecture-review',
      'pattern-suggestion',
      'task-planning',
      'decomposition',
      'shell-commands',
      'build-management',
      'analysis',
    ],
  },
] as const

export const ALL_CAPABILITIES = CAPABILITY_CATEGORIES.flatMap((c) => c.items)
