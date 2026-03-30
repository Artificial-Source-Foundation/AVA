/**
 * Project Card
 *
 * Two variants:
 * - "active": Current project — large card with blue accent border,
 *   44px blue folder icon, 15px name, full metadata row with "Active now".
 * - "default": Recent project — compact card with 32px gray folder icon,
 *   13px name, smaller metadata.
 *
 * Matches the Pencil design spec (KVAdT).
 */

import { Folder, GitBranch } from 'lucide-solid'
import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import type { Project, ProjectWithStats } from '../../types'

export interface ProjectCardProps {
  project: Project | ProjectWithStats
  variant: 'active' | 'default'
  onClick: () => void
}

function hasStats(p: Project | ProjectWithStats): p is ProjectWithStats {
  return 'sessionCount' in p
}

function formatLastActive(timestamp: number | undefined): string {
  if (!timestamp) return 'never'
  const minutes = Math.max(1, Math.floor((Date.now() - timestamp) / 60_000))
  if (minutes < 60) return `${minutes}m ago`
  const hours = Math.floor(minutes / 60)
  if (hours < 24) return `${hours}h ago`
  const days = Math.floor(hours / 24)
  if (days < 30) return `${days}d ago`
  const months = Math.floor(days / 30)
  return `${months}mo ago`
}

function shortenPath(directory: string): string {
  const home = directory.replace(/\\/g, '/').replace(/^\/home\/[^/]+/, '~')
  return home
}

export const ProjectCard: Component<ProjectCardProps> = (props) => {
  const isActive = () => props.variant === 'active'

  return (
    <button
      type="button"
      onClick={() => props.onClick()}
      class="ph-card"
      classList={{
        'ph-card--active': isActive(),
        'ph-card--default': !isActive(),
      }}
    >
      {/* Top row: icon + info */}
      <div class="ph-card-top">
        {/* Folder icon frame */}
        <div
          class="ph-card-folder"
          classList={{
            'ph-card-folder--active': isActive(),
            'ph-card-folder--default': !isActive(),
          }}
        >
          <Folder
            class="ph-card-folder-icon"
            classList={{
              'ph-card-folder-icon--active': isActive(),
              'ph-card-folder-icon--default': !isActive(),
            }}
          />
        </div>

        {/* Name + path */}
        <div class="ph-card-info">
          <span
            class="ph-card-name"
            classList={{
              'ph-card-name--active': isActive(),
              'ph-card-name--default': !isActive(),
            }}
          >
            {props.project.name}
          </span>
          <span
            class="ph-card-path"
            classList={{
              'ph-card-path--active': isActive(),
              'ph-card-path--default': !isActive(),
            }}
          >
            {shortenPath(props.project.directory)}
          </span>
        </div>
      </div>

      {/* Metadata row */}
      <div
        class="ph-card-meta"
        classList={{
          'ph-card-meta--active': isActive(),
          'ph-card-meta--default': !isActive(),
        }}
      >
        <Show when={props.project.git?.branch}>
          <span class="ph-card-meta-item">
            <GitBranch class="ph-card-meta-icon" />
            {props.project.git!.branch}
          </span>
          <span class="ph-card-meta-dot">{'\u00B7'}</span>
        </Show>

        <Show when={hasStats(props.project)}>
          <span class="ph-card-meta-item">
            {(props.project as ProjectWithStats).sessionCount} session
            {(props.project as ProjectWithStats).sessionCount !== 1 ? 's' : ''}
          </span>
          <span class="ph-card-meta-dot">{'\u00B7'}</span>
        </Show>

        <Show
          when={isActive()}
          fallback={
            <span class="ph-card-meta-item">{formatLastActive(props.project.lastOpenedAt)}</span>
          }
        >
          <span class="ph-card-meta-active">Active now</span>
        </Show>
      </div>
    </button>
  )
}
