/**
 * Diff Review Panel — helpers, types, and config
 *
 * Shared data for the DiffReviewPanel component.
 */

import { FileEdit, FilePlus2, FolderOpen, Trash2 } from 'lucide-solid'
import type { Component } from 'solid-js'
import type { FileOperationType } from '../../../types'

// ============================================================================
// Icon & color mappings
// ============================================================================

export const opIcons: Record<
  FileOperationType,
  Component<{ class?: string; style?: Record<string, string> }>
> = {
  read: FolderOpen,
  write: FilePlus2,
  edit: FileEdit,
  delete: Trash2,
}

export const opColors: Record<FileOperationType, string> = {
  read: 'var(--accent)',
  write: 'var(--success)',
  edit: 'var(--warning)',
  delete: 'var(--error)',
}

// ============================================================================
// Path helpers
// ============================================================================

export function getFileName(path: string): string {
  return path.split('/').pop() || path
}

export function getDirectory(path: string): string {
  const parts = path.split('/')
  if (parts.length <= 1) return ''
  parts.pop()
  // Show last 2 segments for context
  return parts.slice(-2).join('/')
}

// ============================================================================
// Types
// ============================================================================

export interface DiffReviewPanelProps {
  compact?: boolean
}

export type HunkReviewStatus = 'pending' | 'accepted' | 'rejected'

export interface HunkReviewItem {
  id: string
  path: string
  status: HunkReviewStatus
  content: string
}

export interface DiffHunksUpdatedEvent {
  sessionId: string
  items: HunkReviewItem[]
  summary: {
    total: number
    pending: number
    accepted: number
    rejected: number
  }
}
