/**
 * Default Command Palette Commands
 *
 * Factory function that creates the standard set of commands
 * for the command palette.
 */

import {
  ArrowDownCircle,
  BarChart3,
  BookmarkPlus,
  Command,
  Download,
  FileText,
  Flag,
  Library,
  Megaphone,
  MessageSquare,
  Settings,
  Sparkles,
  Terminal,
  Upload,
  Wrench,
} from 'lucide-solid'
import type { CommandItem } from './types'

export const createDefaultCommands = (handlers: {
  newChat?: () => void
  openSettings?: () => void
  clearChat?: () => void
  exportChat?: () => void
  initProject?: () => void
  switchTab?: (tab: string) => void
  saveWorkflow?: () => void
  browseWorkflows?: () => void
  importWorkflows?: () => void
  exportWorkflows?: () => void
  openProjectStats?: () => void
  saveCheckpoint?: () => void
  browseTools?: () => void
}): CommandItem[] => [
  {
    id: 'new-chat',
    label: 'New Chat',
    description: 'Start a new conversation',
    icon: MessageSquare,
    category: 'Chat',
    shortcut: '\u2318N',
    action: handlers.newChat || (() => {}),
  },
  {
    id: 'clear-chat',
    label: 'Clear Chat',
    description: 'Clear current conversation',
    icon: MessageSquare,
    category: 'Chat',
    action: handlers.clearChat || (() => {}),
  },
  {
    id: 'export-chat',
    label: 'Export Chat',
    description: 'Download conversation as Markdown',
    icon: Download,
    category: 'Chat',
    shortcut: '\u2318\u21E7E',
    action: handlers.exportChat || (() => {}),
  },
  {
    id: 'init-project',
    label: 'Initialize Project',
    description: 'Analyze codebase and generate project rules',
    icon: Command,
    category: 'General',
    action: handlers.initProject || (() => {}),
  },
  {
    id: 'open-settings',
    label: 'Open Settings',
    description: 'Configure preferences',
    icon: Settings,
    category: 'General',
    shortcut: '\u2318,',
    action: handlers.openSettings || (() => {}),
  },
  {
    id: 'tab-chat',
    label: 'Go to Chat',
    description: 'Switch to Chat tab',
    icon: MessageSquare,
    category: 'Navigation',
    action: () => handlers.switchTab?.('chat'),
  },
  {
    id: 'tab-agents',
    label: 'Go to Agents',
    description: 'Switch to Agents tab',
    icon: Sparkles,
    category: 'Navigation',
    action: () => handlers.switchTab?.('agents'),
  },
  {
    id: 'tab-files',
    label: 'Go to Files',
    description: 'Switch to Files tab',
    icon: FileText,
    category: 'Navigation',
    action: () => handlers.switchTab?.('files'),
  },
  {
    id: 'tab-terminal',
    label: 'Go to Terminal',
    description: 'Switch to Terminal tab',
    icon: Terminal,
    category: 'Navigation',
    action: () => handlers.switchTab?.('terminal'),
  },
  {
    id: 'save-workflow',
    label: 'Save Session as Workflow',
    description: 'Create a reusable workflow from this session',
    icon: BookmarkPlus,
    category: 'Workflows',
    action: handlers.saveWorkflow || (() => {}),
  },
  {
    id: 'browse-workflows',
    label: 'Browse Workflows',
    description: 'View and apply saved workflows',
    icon: Library,
    category: 'Workflows',
    action: handlers.browseWorkflows || (() => {}),
  },
  {
    id: 'import-workflows',
    label: 'Import Workflows',
    description: 'Import workflows from a JSON file',
    icon: Upload,
    category: 'Workflows',
    action: handlers.importWorkflows || (() => {}),
  },
  {
    id: 'export-workflows',
    label: 'Export Workflows',
    description: 'Export all workflows as JSON',
    icon: Download,
    category: 'Workflows',
    action: handlers.exportWorkflows || (() => {}),
  },
  {
    id: 'project-stats',
    label: 'Project Stats',
    description: 'View project-level usage and cost statistics',
    icon: BarChart3,
    category: 'Session',
    action: handlers.openProjectStats || (() => {}),
  },
  {
    id: 'save-checkpoint',
    label: 'Save Checkpoint',
    description: 'Create a named snapshot of the conversation',
    icon: Flag,
    category: 'Session',
    shortcut: '\u2318\u21E7C',
    action: handlers.saveCheckpoint || (() => {}),
  },
  {
    id: 'browse-tools',
    label: 'Browse Tools',
    description: 'View all registered tools (built-in, MCP, custom)',
    icon: Wrench,
    category: 'Session',
    action: handlers.browseTools || (() => {}),
  },
  {
    id: 'whats-new',
    label: "What's New",
    description: 'View recent changes and release notes',
    icon: Megaphone,
    category: 'General',
    action: () => window.dispatchEvent(new CustomEvent('ava:open-changelog')),
  },
  {
    id: 'check-updates',
    label: 'Check for Updates',
    description: 'Check if a new version of AVA is available',
    icon: ArrowDownCircle,
    category: 'General',
    action: () => window.dispatchEvent(new CustomEvent('ava:check-update')),
  },
]
