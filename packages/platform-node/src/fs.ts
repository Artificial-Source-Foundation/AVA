/**
 * Node.js File System Implementation
 */

import * as fs from 'node:fs/promises'
import type { DirEntry, FileStat, IFileSystem } from '@ava/core'
import fg from 'fast-glob'

export class NodeFileSystem implements IFileSystem {
  async readFile(filePath: string): Promise<string> {
    return fs.readFile(filePath, 'utf-8')
  }

  async readBinary(filePath: string, limit?: number): Promise<Uint8Array> {
    if (limit !== undefined && limit > 0) {
      // Read only first N bytes
      const handle = await fs.open(filePath, 'r')
      try {
        const buffer = Buffer.alloc(limit)
        const { bytesRead } = await handle.read(buffer, 0, limit, 0)
        return new Uint8Array(buffer.subarray(0, bytesRead))
      } finally {
        await handle.close()
      }
    }
    const buffer = await fs.readFile(filePath)
    return new Uint8Array(buffer)
  }

  async writeFile(filePath: string, content: string): Promise<void> {
    await fs.writeFile(filePath, content, 'utf-8')
  }

  async writeBinary(filePath: string, content: Uint8Array): Promise<void> {
    await fs.writeFile(filePath, content)
  }

  async readDir(dirPath: string): Promise<string[]> {
    const entries = await fs.readdir(dirPath)
    return entries
  }

  async readDirWithTypes(dirPath: string): Promise<DirEntry[]> {
    const entries = await fs.readdir(dirPath, { withFileTypes: true })
    return entries.map((e) => ({
      name: e.name,
      isFile: e.isFile(),
      isDirectory: e.isDirectory(),
    }))
  }

  async stat(filePath: string): Promise<FileStat> {
    const info = await fs.stat(filePath)
    return {
      isFile: info.isFile(),
      isDirectory: info.isDirectory(),
      size: info.size,
      mtime: info.mtime.getTime(),
    }
  }

  async exists(filePath: string): Promise<boolean> {
    try {
      await fs.access(filePath)
      return true
    } catch {
      return false
    }
  }

  async isFile(filePath: string): Promise<boolean> {
    try {
      const stat = await fs.stat(filePath)
      return stat.isFile()
    } catch {
      return false
    }
  }

  async isDirectory(filePath: string): Promise<boolean> {
    try {
      const stat = await fs.stat(filePath)
      return stat.isDirectory()
    } catch {
      return false
    }
  }

  async mkdir(dirPath: string): Promise<void> {
    await fs.mkdir(dirPath, { recursive: true })
  }

  async remove(filePath: string): Promise<void> {
    await fs.rm(filePath, { recursive: true, force: true })
  }

  async glob(pattern: string, cwd: string): Promise<string[]> {
    return fg(pattern, {
      cwd,
      dot: true,
      onlyFiles: true,
      ignore: ['**/node_modules/**', '**/.git/**'],
    })
  }
}
