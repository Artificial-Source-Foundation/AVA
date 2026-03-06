/**
 * Agent Icon Registry
 * Maps icon names to Lucide icon components for agent presets.
 */

import {
  Bug,
  Building,
  Code,
  Compass,
  Crown,
  Eye,
  FileText,
  GitBranch,
  Layers,
  Layout,
  ListTodo,
  Rocket,
  Search,
  Server,
  Shield,
  Terminal,
  TestTube,
  Zap,
} from 'lucide-solid'
import type { Component } from 'solid-js'

type IconComponent = Component<{ class?: string }>

export const AGENT_ICONS: Record<string, IconComponent> = {
  Code: Code as IconComponent,
  Compass: Compass as IconComponent,
  Crown: Crown as IconComponent,
  Layout: Layout as IconComponent,
  Server: Server as IconComponent,
  Shield: Shield as IconComponent,
  Layers: Layers as IconComponent,
  TestTube: TestTube as IconComponent,
  Eye: Eye as IconComponent,
  Search: Search as IconComponent,
  Bug: Bug as IconComponent,
  Building: Building as IconComponent,
  ListTodo: ListTodo as IconComponent,
  Rocket: Rocket as IconComponent,
  GitBranch: GitBranch as IconComponent,
  Terminal: Terminal as IconComponent,
  FileText: FileText as IconComponent,
  Zap: Zap as IconComponent,
}
