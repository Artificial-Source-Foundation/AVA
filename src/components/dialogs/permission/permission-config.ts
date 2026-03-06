/**
 * Permission Configuration Data
 *
 * Type definitions and configuration objects for the permission system.
 * Extracted from PermissionDialog.tsx to keep each module under 300 lines.
 */

import {
  AlertTriangle,
  FileEdit,
  FolderOpen,
  Globe,
  Shield,
  ShieldAlert,
  Terminal,
} from 'lucide-solid'
import type { Component } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

export type PermissionType =
  | 'file_read'
  | 'file_write'
  | 'file_delete'
  | 'command_execute'
  | 'network_request'
  | 'system_access'

export type PermissionScope = 'once' | 'session' | 'always'

export interface PermissionRequest {
  id: string
  type: PermissionType
  resource: string
  description?: string
  command?: string
  riskLevel: 'low' | 'medium' | 'high'
}

export type IconComponent = Component<{ class?: string; style?: { color?: string } }>

export interface PermissionConfig {
  icon: IconComponent
  label: string
  description: string
  color: string
  bg: string
}

// ============================================================================
// Config Data
// ============================================================================

export const permissionConfig: Record<PermissionType, PermissionConfig> = {
  file_read: {
    icon: FolderOpen as IconComponent,
    label: 'Read File',
    description: 'Access file contents for analysis',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
  file_write: {
    icon: FileEdit as IconComponent,
    label: 'Write File',
    description: 'Create or modify file contents',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  file_delete: {
    icon: AlertTriangle as IconComponent,
    label: 'Delete File',
    description: 'Permanently remove files from disk',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
  command_execute: {
    icon: Terminal as IconComponent,
    label: 'Execute Command',
    description: 'Run a shell command',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  network_request: {
    icon: Globe as IconComponent,
    label: 'Network Request',
    description: 'Make an external API call',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
  system_access: {
    icon: Shield as IconComponent,
    label: 'System Access',
    description: 'Access system resources',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
}

export const riskConfig = {
  low: {
    icon: Shield as IconComponent,
    label: 'Low Risk',
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
  },
  medium: {
    icon: ShieldAlert as IconComponent,
    label: 'Medium Risk',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  high: {
    icon: AlertTriangle as IconComponent,
    label: 'High Risk',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
}
