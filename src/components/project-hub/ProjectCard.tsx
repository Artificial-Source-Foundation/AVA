/**
 * Project Card
 *
 * Displays a single project in the Recent Projects grid.
 * Shows folder icon, project name, path, git branch, session count, and last active time.
 */

import { Clock, Folder, GitBranch, MessageCircle } from 'lucide-solid'
import type { Component } from 'solid-js'
import { Show } from 'solid-js'
import type { ProjectWithStats } from '../../types'

export interface ProjectCardProps {
  project: ProjectWithStats
  isActive: boolean
  onClick: () => void
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
  const parts = directory.replace(/\\/g, '/').split('/')
  if (parts.length <= 4) return directory
  return `~/${parts.slice(-2).join('/')}`
}

export const ProjectCard: Component<ProjectCardProps> = (props) => {
  return (
    <button
      type="button"
      onClick={() => props.onClick()}
      class="
        flex flex-col gap-3
        rounded-[14px] p-5
        bg-[#18181B]
        border transition-all duration-150
        text-left cursor-pointer
        hover:bg-[#1c1c1f]
        min-w-[220px] max-w-[320px] flex-1
      "
      classList={{
        'border-[#A78BFA]/[0.19] hover:border-[#A78BFA]/30': props.isActive,
        'border-[#27272A] hover:border-[#3f3f46]': !props.isActive,
      }}
    >
      {/* Top: icon + name */}
      <div class="flex items-center gap-2.5">
        <Folder
          class="w-4 h-4 flex-shrink-0"
          classList={{
            'text-[#A78BFA]': props.isActive,
            'text-[#52525B]': !props.isActive,
          }}
        />
        <span class="text-[15px] font-semibold text-white truncate">{props.project.name}</span>
      </div>

      {/* Path */}
      <span
        class="text-[11px] text-[#52525B] truncate"
        style={{ 'font-family': "'JetBrains Mono', monospace" }}
      >
        {shortenPath(props.project.directory)}
      </span>

      {/* Meta row */}
      <div class="flex items-center gap-4 text-[11px] text-[#71717A]">
        <Show when={props.project.git?.branch}>
          <span class="flex items-center gap-1">
            <GitBranch class="w-3 h-3" />
            {props.project.git!.branch}
          </span>
        </Show>

        <span class="flex items-center gap-1">
          <MessageCircle class="w-3 h-3" />
          {props.project.sessionCount} session{props.project.sessionCount !== 1 ? 's' : ''}
        </span>

        <span class="flex items-center gap-1">
          <Clock class="w-3 h-3" />
          {formatLastActive(props.project.lastOpenedAt)}
        </span>
      </div>
    </button>
  )
}
