/**
 * Message Input Types & Constants
 *
 * Shared types, interfaces, and constants for the MessageInput sub-modules.
 */

import type { Shield } from 'lucide-solid'

// ---------------------------------------------------------------------------
// Vision constants
// ---------------------------------------------------------------------------
export const MAX_IMAGE_SIZE = 5 * 1024 * 1024 // 5MB
export const MAX_IMAGES = 4
export const ACCEPTED_IMAGE_TYPES = ['image/png', 'image/jpeg', 'image/gif', 'image/webp'] as const
export type SupportedImageMimeType = (typeof ACCEPTED_IMAGE_TYPES)[number]

// ---------------------------------------------------------------------------
// Paste collapse constants
// ---------------------------------------------------------------------------
export const PASTE_LINE_THRESHOLD = 5
export const PASTE_PREVIEW_LINES = 3

// ---------------------------------------------------------------------------
// File context constants
// ---------------------------------------------------------------------------
export const MAX_FILE_SIZE = 100 * 1024 // 100KB
export const MAX_FILES = 5
export const TEXT_EXTENSIONS = new Set([
  '.ts',
  '.tsx',
  '.js',
  '.jsx',
  '.json',
  '.md',
  '.txt',
  '.css',
  '.html',
  '.py',
  '.rs',
  '.go',
  '.java',
  '.c',
  '.cpp',
  '.h',
  '.yml',
  '.yaml',
  '.toml',
  '.env',
  '.sh',
  '.bash',
  '.sql',
  '.graphql',
  '.xml',
  '.svg',
])

// ---------------------------------------------------------------------------
// Data shapes
// ---------------------------------------------------------------------------
export interface PendingImage {
  data: string
  mimeType: SupportedImageMimeType
  name?: string
}

export interface PendingFile {
  name: string
  content: string
}

export interface PendingPaste {
  content: string
  lineCount: number
}

// ---------------------------------------------------------------------------
// Permission config
// ---------------------------------------------------------------------------
export interface PermissionConfigEntry {
  icon: typeof Shield
  color: string
  label: string
}
