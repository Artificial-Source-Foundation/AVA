import type { SimpleLogger } from '@ava/core-v2/logger'
import type { IFileSystem } from '@ava/core-v2/platform'
import type { InstructionFile } from './types.js'

const SKILL_GLOB_PATTERNS = ['.claude/skills/*.md', '.agents/skills/*.md']
const SKILL_DIRECT_FILES = ['GEMINI.md', '.github/copilot-instructions.md']

function toAbsolute(cwd: string, filePath: string): string {
  return filePath.startsWith('/') ? filePath : `${cwd}/${filePath}`
}

export async function loadCrossToolSkillInstructions(
  cwd: string,
  fs: IFileSystem,
  log?: SimpleLogger
): Promise<InstructionFile[]> {
  const discovered = new Set<string>()

  for (const pattern of SKILL_GLOB_PATTERNS) {
    try {
      const files = await fs.glob(pattern, cwd)
      for (const file of files) discovered.add(toAbsolute(cwd, file))
    } catch (error) {
      log?.debug(`Skill glob scan skipped for ${pattern}: ${String(error)}`)
    }
  }

  for (const relativePath of SKILL_DIRECT_FILES) {
    const absolutePath = `${cwd}/${relativePath}`
    if (await fs.exists(absolutePath)) discovered.add(absolutePath)
  }

  const results: InstructionFile[] = []
  for (const path of discovered) {
    try {
      const content = await fs.readFile(path)
      results.push({
        path,
        content,
        scope: 'project',
        priority: 900,
      })
    } catch (error) {
      log?.debug(`Skill compatibility read failed for ${path}: ${String(error)}`)
    }
  }

  results.sort((a, b) => a.path.localeCompare(b.path))
  return results
}
