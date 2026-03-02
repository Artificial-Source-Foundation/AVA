/**
 * Plan save — persists plan content to .ava/plans/ directory.
 */

import { getPlatform } from '@ava/core-v2/platform'

/**
 * Save plan content to a markdown file in `.ava/plans/`.
 * Returns the path of the saved file.
 */
export async function savePlanToFile(content: string, slug?: string): Promise<string> {
  const fs = getPlatform().fs
  const plansDir = '.ava/plans'

  // Ensure directory exists
  const dirExists = await fs.exists(plansDir)
  if (!dirExists) {
    await fs.mkdir(plansDir)
  }

  const timestamp = new Date().toISOString().replace(/[:.]/g, '-').slice(0, 19)
  const safeName = slug ? slug.replace(/[^a-zA-Z0-9_-]/g, '-').slice(0, 50) : 'plan'
  const filename = `${timestamp}-${safeName}.md`
  const filePath = `${plansDir}/${filename}`

  await fs.writeFile(filePath, content)
  return filePath
}
