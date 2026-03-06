/**
 * Memory type configuration and formatting utilities
 */

import { Bookmark, Code2, FileText, MessageSquare, Sparkles } from 'lucide-solid'
import type { MemoryItemType } from '../../../types'

export const memoryTypeConfig: Record<
  MemoryItemType,
  { color: string; bg: string; icon: typeof MessageSquare; label: string }
> = {
  conversation: {
    color: 'var(--accent)',
    bg: 'var(--accent-subtle)',
    icon: MessageSquare,
    label: 'Conversation',
  },
  file: {
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
    icon: FileText,
    label: 'File',
  },
  code: {
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
    icon: Code2,
    label: 'Code',
  },
  knowledge: {
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
    icon: Sparkles,
    label: 'Knowledge',
  },
  checkpoint: {
    color: 'var(--text-muted)',
    bg: 'var(--surface-raised)',
    icon: Bookmark,
    label: 'Checkpoint',
  },
}

export const formatTokens = (tokens: number): string => {
  if (tokens >= 1000000) return `${(tokens / 1000000).toFixed(1)}M`
  if (tokens >= 1000) return `${(tokens / 1000).toFixed(1)}K`
  return tokens.toString()
}
