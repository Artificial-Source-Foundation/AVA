/**
 * File Operations Panel — helpers, types, and config
 *
 * Shared data and utilities for the FileOperationsPanel component.
 * Extracted from FileOperationsPanel.tsx.
 */

import { Eye, FileEdit, FilePlus2, type FileText, Trash2 } from 'lucide-solid'
import type { FileOperationType } from '../../../types'

// ============================================================================
// Operation Configuration
// ============================================================================

export const operationConfig: Record<
  FileOperationType,
  { color: string; bg: string; icon: typeof FileText; label: string }
> = {
  read: { color: 'var(--accent)', bg: 'var(--accent-subtle)', icon: Eye, label: 'Read' },
  write: { color: 'var(--success)', bg: 'var(--success-subtle)', icon: FilePlus2, label: 'Write' },
  edit: { color: 'var(--warning)', bg: 'var(--warning-subtle)', icon: FileEdit, label: 'Edit' },
  delete: { color: 'var(--error)', bg: 'var(--error-subtle)', icon: Trash2, label: 'Delete' },
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
  return parts.join('/')
}

// ============================================================================
// Formatting helpers
// ============================================================================

export function formatTimestamp(ts: number): string {
  const diff = Date.now() - ts
  if (diff < 60000) return 'Just now'
  if (diff < 3600000) return `${Math.floor(diff / 60000)}m ago`
  if (diff < 86400000) return `${Math.floor(diff / 3600000)}h ago`
  return new Date(ts).toLocaleDateString()
}

// ============================================================================
// Types
// ============================================================================

export interface FileOperationsPanelProps {
  compact?: boolean
}
