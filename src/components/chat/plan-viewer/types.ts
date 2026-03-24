import type { PlanStepAction } from '../../../types/rust-ipc'

export type EditorMode = 'selection' | 'comment' | 'redline' | 'quickLabel'
export type InputMethod = 'drag' | 'pinpoint'

export const PLAN_ACCENT = '#8B5CF6'
export const PLAN_ACCENT_SUBTLE = 'rgba(139, 92, 246, 0.12)'

export const ACTION_CONFIG: Record<
  PlanStepAction,
  { bg: string; text: string; border: string; label: string }
> = {
  research: {
    bg: 'rgba(6, 182, 212, 0.10)',
    text: '#06B6D4',
    border: 'rgba(6, 182, 212, 0.25)',
    label: 'Research',
  },
  implement: {
    bg: 'rgba(59, 130, 246, 0.10)',
    text: '#3B82F6',
    border: 'rgba(59, 130, 246, 0.25)',
    label: 'Implement',
  },
  test: {
    bg: 'rgba(34, 197, 94, 0.10)',
    text: '#22C55E',
    border: 'rgba(34, 197, 94, 0.25)',
    label: 'Test',
  },
  review: {
    bg: 'rgba(245, 158, 11, 0.10)',
    text: '#F59E0B',
    border: 'rgba(245, 158, 11, 0.25)',
    label: 'Review',
  },
}

export interface QuickLabel {
  id: string
  emoji: string
  text: string
  color: string
  tip?: string
}

export const QUICK_LABELS: QuickLabel[] = [
  { id: 'clarify', emoji: '\u2753', text: 'Clarify this', color: '#EAB308' },
  {
    id: 'missing-overview',
    emoji: '\uD83D\uDDFA\uFE0F',
    text: 'Missing overview',
    color: '#A855F7',
    tip: 'Add a high-level summary of what this section covers',
  },
  {
    id: 'verify',
    emoji: '\uD83D\uDD0D',
    text: 'Verify this',
    color: '#F97316',
    tip: 'Double-check this claim or assumption',
  },
  {
    id: 'example',
    emoji: '\uD83D\uDD2C',
    text: 'Give me an example',
    color: '#06B6D4',
    tip: 'Add a concrete example to illustrate this',
  },
  {
    id: 'patterns',
    emoji: '\uD83E\uDDEC',
    text: 'Match existing patterns',
    color: '#14B8A6',
    tip: 'Align with patterns already used in the codebase',
  },
  {
    id: 'alternatives',
    emoji: '\uD83D\uDD04',
    text: 'Consider alternatives',
    color: '#EC4899',
    tip: 'Evaluate other approaches before committing',
  },
  {
    id: 'regression',
    emoji: '\uD83D\uDCC9',
    text: 'Ensure no regression',
    color: '#F59E0B',
    tip: 'Verify this change does not break existing functionality',
  },
  {
    id: 'out-of-scope',
    emoji: '\uD83D\uDEAB',
    text: 'Out of scope',
    color: '#EF4444',
    tip: 'This is outside the scope of the current plan',
  },
  { id: 'needs-tests', emoji: '\uD83E\uDDEA', text: 'Needs tests', color: '#3B82F6' },
  { id: 'nice', emoji: '\uD83D\uDC4D', text: 'Nice approach', color: '#22C55E' },
]

export interface SelectionInfo {
  text: string
  top: number
  left: number
}

export function generateId(): string {
  return `ann-${Date.now()}-${Math.random().toString(36).slice(2, 8)}`
}
