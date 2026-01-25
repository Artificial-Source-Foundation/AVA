/**
 * Delta9 Checkpoint Tools
 *
 * Tools for managing mission checkpoints and rollback.
 */

import { tool, type ToolDefinition } from '@opencode-ai/plugin'
import {
  CheckpointManager,
  describeCheckpoint,
} from '../mission/checkpoints.js'
import { MissionState } from '../mission/state.js'

// Use the tool's built-in schema
const s = tool.schema

// =============================================================================
// Tool Definitions
// =============================================================================

/**
 * Create checkpoint tools
 */
export function createCheckpointTools(cwd: string): Record<string, ToolDefinition> {
  const checkpointManager = new CheckpointManager(cwd)
  const missionState = new MissionState(cwd)

  /**
   * Create a new checkpoint
   */
  const checkpoint_create = tool({
    description: `Create a new checkpoint to save the current state of the mission.

Checkpoints allow you to:
- Save progress at key milestones
- Rollback if something goes wrong
- Create restore points before risky operations

The checkpoint includes a git commit of all changed files.`,

    args: {
      name: s.string().describe('Human-readable checkpoint name (e.g., "auth-complete", "before-refactor")'),
      description: s.string().optional().describe('Optional description of what this checkpoint captures'),
      files: s.array(s.string()).optional().describe('Specific files to include (default: all changed files)'),
    },

    async execute(args, _ctx) {
      // Load mission to get ID
      const mission = missionState.load()
      if (!mission) {
        return JSON.stringify({
          success: false,
          error: 'No active mission. Create a mission first.',
        })
      }

      // Check if git is initialized
      if (!checkpointManager.isGitInitialized()) {
        return JSON.stringify({
          success: false,
          error: 'Git is not initialized. Run "git init" first.',
        })
      }

      // Create checkpoint
      const checkpoint = checkpointManager.create(args.name, {
        missionId: mission.id,
        description: args.description,
        files: args.files,
        auto: false,
      })

      if (!checkpoint) {
        return JSON.stringify({
          success: false,
          error: 'Failed to create checkpoint. Check git status.',
        })
      }

      return JSON.stringify({
        success: true,
        checkpoint: {
          id: checkpoint.id,
          name: checkpoint.name,
          gitCommit: checkpoint.gitCommit.slice(0, 8),
          filesCount: checkpoint.files.length,
          createdAt: checkpoint.createdAt,
        },
        message: `Checkpoint "${args.name}" created successfully`,
      })
    },
  })

  /**
   * List all checkpoints
   */
  const checkpoint_list = tool({
    description: `List all checkpoints for the current mission.

Shows:
- Checkpoint name and ID
- Creation time
- Git commit reference
- Number of files captured`,

    args: {
      all: s.boolean().optional().describe('Show checkpoints from all missions (default: current mission only)'),
    },

    async execute(args, _ctx) {
      const mission = missionState.load()
      const missionId = args.all ? undefined : mission?.id

      const checkpoints = checkpointManager.list(missionId)

      if (checkpoints.length === 0) {
        return JSON.stringify({
          success: true,
          checkpoints: [],
          message: 'No checkpoints found',
        })
      }

      const formatted = checkpoints.map(c => ({
        id: c.id,
        name: c.name,
        gitCommit: c.gitCommit.slice(0, 8),
        filesCount: c.files.length,
        createdAt: c.createdAt,
        auto: c.auto,
        missionId: c.missionId,
      }))

      return JSON.stringify({
        success: true,
        checkpoints: formatted,
        count: checkpoints.length,
      })
    },
  })

  /**
   * Restore to a checkpoint
   */
  const checkpoint_restore = tool({
    description: `Restore the project to a previous checkpoint.

⚠️ WARNING: This will:
- Discard all changes since the checkpoint
- Reset git to the checkpoint commit
- A backup commit is created before restore

Use with caution - this cannot be undone easily.`,

    args: {
      id: s.string().optional().describe('Checkpoint ID to restore'),
      name: s.string().optional().describe('Checkpoint name to restore (alternative to ID)'),
    },

    async execute(args, _ctx) {
      if (!args.id && !args.name) {
        return JSON.stringify({
          success: false,
          error: 'Provide either checkpoint ID or name',
        })
      }

      // Find checkpoint
      let checkpoint = args.id
        ? checkpointManager.get(args.id)
        : checkpointManager.getByName(args.name!)

      if (!checkpoint) {
        return JSON.stringify({
          success: false,
          error: `Checkpoint not found: ${args.id || args.name}`,
        })
      }

      // Restore
      const result = checkpointManager.restore(checkpoint.id)

      if (!result.success) {
        return JSON.stringify({
          success: false,
          error: result.error,
        })
      }

      return JSON.stringify({
        success: true,
        restored: {
          id: checkpoint.id,
          name: checkpoint.name,
          gitCommit: checkpoint.gitCommit.slice(0, 8),
          filesRestored: result.filesRestored.length,
        },
        message: `Restored to checkpoint "${checkpoint.name}"`,
        warning: 'All changes since this checkpoint have been discarded',
      })
    },
  })

  /**
   * Delete a checkpoint
   */
  const checkpoint_delete = tool({
    description: `Delete a checkpoint.

This removes the checkpoint metadata but does NOT delete the git commit.
The commit remains in git history.`,

    args: {
      id: s.string().optional().describe('Checkpoint ID to delete'),
      name: s.string().optional().describe('Checkpoint name to delete (alternative to ID)'),
      all: s.boolean().optional().describe('Delete all checkpoints for current mission'),
    },

    async execute(args, _ctx) {
      if (args.all) {
        const mission = missionState.load()
        if (!mission) {
          return JSON.stringify({
            success: false,
            error: 'No active mission',
          })
        }

        const deleted = checkpointManager.deleteAll(mission.id)
        return JSON.stringify({
          success: true,
          deletedCount: deleted,
          message: `Deleted ${deleted} checkpoint(s)`,
        })
      }

      if (!args.id && !args.name) {
        return JSON.stringify({
          success: false,
          error: 'Provide checkpoint ID, name, or use --all',
        })
      }

      // Find checkpoint
      const checkpoint = args.id
        ? checkpointManager.get(args.id)
        : checkpointManager.getByName(args.name!)

      if (!checkpoint) {
        return JSON.stringify({
          success: false,
          error: `Checkpoint not found: ${args.id || args.name}`,
        })
      }

      const deleted = checkpointManager.delete(checkpoint.id)

      return JSON.stringify({
        success: deleted,
        message: deleted
          ? `Deleted checkpoint "${checkpoint.name}"`
          : 'Failed to delete checkpoint',
      })
    },
  })

  /**
   * Get checkpoint details
   */
  const checkpoint_get = tool({
    description: `Get detailed information about a specific checkpoint.

Shows:
- Full checkpoint metadata
- Git commit details
- List of files captured
- Creation context`,

    args: {
      id: s.string().optional().describe('Checkpoint ID'),
      name: s.string().optional().describe('Checkpoint name (alternative to ID)'),
    },

    async execute(args, _ctx) {
      if (!args.id && !args.name) {
        return JSON.stringify({
          success: false,
          error: 'Provide either checkpoint ID or name',
        })
      }

      const checkpoint = args.id
        ? checkpointManager.get(args.id)
        : checkpointManager.getByName(args.name!)

      if (!checkpoint) {
        return JSON.stringify({
          success: false,
          error: `Checkpoint not found: ${args.id || args.name}`,
        })
      }

      return JSON.stringify({
        success: true,
        checkpoint,
        humanReadable: describeCheckpoint(checkpoint),
      })
    },
  })

  return {
    checkpoint_create,
    checkpoint_list,
    checkpoint_restore,
    checkpoint_delete,
    checkpoint_get,
  }
}

// =============================================================================
// Type Export
// =============================================================================

export type CheckpointTools = ReturnType<typeof createCheckpointTools>
