/**
 * Project Types
 * Types for workspace/project management
 */

// ============================================================================
// Core Types
// ============================================================================

/** Project identifier */
export type ProjectId = string

/** Project icon configuration */
export interface ProjectIcon {
  /** URL to icon image (data URI or HTTP) */
  url?: string
  /** User override emoji or icon name */
  override?: string
  /** Accent color for project (hex or CSS color) */
  color?: string
}

/** Git repository information */
export interface ProjectGitInfo {
  /** Current branch name */
  branch?: string
  /** Root commit SHA (stable project identifier) */
  rootCommit?: string
  /** Remote origin URL */
  remoteUrl?: string
}

// ============================================================================
// Project Interface
// ============================================================================

/** Core project interface */
export interface Project {
  /** Unique project identifier */
  id: ProjectId
  /** Project display name (defaults to directory name) */
  name: string
  /** Absolute path to project root (git root if available) */
  directory: string
  /** Optional project icon */
  icon?: ProjectIcon
  /** Git repository information */
  git?: ProjectGitInfo
  /** Timestamp when project was first opened */
  createdAt: number
  /** Timestamp when project was last modified */
  updatedAt: number
  /** Timestamp when project was last opened */
  lastOpenedAt?: number
  /** Is marked as favorite */
  isFavorite?: boolean
}

/** Project with computed stats for display */
export interface ProjectWithStats extends Project {
  /** Number of sessions in this project */
  sessionCount: number
  /** Total messages across all sessions */
  totalMessages: number
}

// ============================================================================
// Input Types
// ============================================================================

/** Input for creating a new project */
export interface CreateProjectInput {
  /** Directory path (will detect git root) */
  directory: string
  /** Optional custom name */
  name?: string
  /** Optional icon configuration */
  icon?: ProjectIcon
}

/** Input for updating a project */
export interface UpdateProjectInput {
  /** New name */
  name?: string
  /** New icon */
  icon?: ProjectIcon
  /** Update git info */
  git?: ProjectGitInfo
  /** Toggle favorite */
  isFavorite?: boolean
  /** Update last opened timestamp */
  lastOpenedAt?: number
}

// ============================================================================
// Detection Types
// ============================================================================

/** Result from project detection */
export interface DetectedProject {
  /** Git root directory (or original directory if not a git repo) */
  rootDirectory: string
  /** Original directory that was opened */
  cwd: string
  /** Whether this is a git repository */
  isGitRepo: boolean
  /** Current branch name (if git) */
  branch?: string
  /** Root commit SHA (if git) */
  rootCommit?: string
  /** Suggested project name (directory name) */
  suggestedName: string
}

// ============================================================================
// Store Types
// ============================================================================

/** Project store interface */
export interface ProjectStore {
  /** Create a new project */
  create(input: CreateProjectInput): Promise<Project>
  /** Get project by ID */
  get(id: ProjectId): Promise<Project | null>
  /** Get project by directory path */
  getByDirectory(directory: string): Promise<Project | null>
  /** Get or create project for a directory */
  getOrCreate(directory: string): Promise<Project>
  /** Update project */
  update(id: ProjectId, updates: UpdateProjectInput): Promise<void>
  /** Delete project (sessions move to default) */
  delete(id: ProjectId): Promise<void>
  /** List all projects with stats */
  listWithStats(): Promise<ProjectWithStats[]>
}
