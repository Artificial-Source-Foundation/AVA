/**
 * Approval Dock — types and config
 *
 * Tool type icons, risk level config, and shared types.
 * Extracted from ApprovalDock.tsx.
 */

import {
  AlertTriangle,
  CheckCircle2,
  FileEdit,
  Globe,
  Shield,
  ShieldAlert,
  ShieldX,
  Terminal,
} from 'lucide-solid'
import type { Component } from 'solid-js'

// ============================================================================
// Types
// ============================================================================

export type IconComponent = Component<{ class?: string; style?: { color?: string } }>

export interface ToolConfig {
  icon: IconComponent
  label: string
  color: string
  bg: string
}

export interface RiskConfig {
  icon: IconComponent
  label: string
  color: string
  bg: string
}

// ============================================================================
// Tool Type Config
// ============================================================================

export const toolTypeConfig: Record<string, ToolConfig> = {
  file: {
    icon: FileEdit as IconComponent,
    label: 'File Operation',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  command: {
    icon: Terminal as IconComponent,
    label: 'Shell Command',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  browser: {
    icon: Globe as IconComponent,
    label: 'Browser Action',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
  mcp: {
    icon: Shield as IconComponent,
    label: 'MCP Tool',
    color: 'var(--info)',
    bg: 'var(--info-subtle)',
  },
}

// ============================================================================
// Risk Config
// ============================================================================

export const riskConfig: Record<string, RiskConfig> = {
  low: {
    icon: CheckCircle2 as IconComponent,
    label: 'Low',
    color: 'var(--success)',
    bg: 'var(--success-subtle)',
  },
  medium: {
    icon: ShieldAlert as IconComponent,
    label: 'Medium',
    color: 'var(--warning)',
    bg: 'var(--warning-subtle)',
  },
  high: {
    icon: AlertTriangle as IconComponent,
    label: 'High',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
  critical: {
    icon: ShieldX as IconComponent,
    label: 'Critical',
    color: 'var(--error)',
    bg: 'var(--error-subtle)',
  },
}
