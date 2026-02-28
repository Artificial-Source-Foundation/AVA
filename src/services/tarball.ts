/**
 * Tarball Service
 *
 * Fetch and extract .tar.gz files from URLs (e.g., GitHub repo tarballs).
 * Uses browser-native DecompressionStream and a minimal tar parser.
 */

type TauriFs = {
  mkdir(path: string, options?: { recursive?: boolean }): Promise<void>
  writeFile(path: string, data: Uint8Array): Promise<void>
}

/**
 * Fetch a .tar.gz from a URL and extract files to a target directory.
 * Strips the first path component (GitHub's "owner-repo-sha/" prefix).
 */
export async function fetchAndExtractTarball(
  tarballUrl: string,
  targetDir: string,
  fs: TauriFs
): Promise<void> {
  const response = await fetch(tarballUrl, {
    headers: { Accept: 'application/vnd.github+json' },
    redirect: 'follow',
  })
  if (!response.ok) {
    throw new Error(`Failed to fetch tarball: ${response.status} ${response.statusText}`)
  }

  const blob = await response.blob()
  const arrayBuffer = await blob.arrayBuffer()

  // Decompress gzip using browser-native API
  const ds = new DecompressionStream('gzip')
  const decompressedStream = new Blob([arrayBuffer]).stream().pipeThrough(ds)
  const decompressedBuffer = await new Response(decompressedStream).arrayBuffer()
  const tarData = new Uint8Array(decompressedBuffer)

  await extractTar(tarData, targetDir, fs)
}

/** Strip trailing NUL bytes from decoded tar header fields. */
const NUL = String.fromCharCode(0)
function trimNulls(s: string): string {
  const idx = s.indexOf(NUL)
  return idx >= 0 ? s.substring(0, idx) : s
}

/** Minimal tar parser -- extracts regular files, stripping the first path component. */
async function extractTar(data: Uint8Array, targetDir: string, fs: TauriFs): Promise<void> {
  let offset = 0
  const decoder = new TextDecoder()
  const createdDirs = new Set<string>()

  while (offset < data.length - 512) {
    const header = data.subarray(offset, offset + 512)

    // End-of-archive: two zero blocks
    if (header.every((b) => b === 0)) break

    // Parse filename (bytes 0-99)
    const rawName = trimNulls(decoder.decode(header.subarray(0, 100)))

    // Parse prefix (bytes 345-499) for POSIX ustar
    const prefix = trimNulls(decoder.decode(header.subarray(345, 500)))
    const fullPath = prefix ? `${prefix}/${rawName}` : rawName

    // Parse file size (bytes 124-135, octal)
    const sizeStr = trimNulls(decoder.decode(header.subarray(124, 136))).trim()
    const fileSize = parseInt(sizeStr, 8) || 0

    // Parse type flag (byte 156)
    const typeFlag = String.fromCharCode(header[156])

    offset += 512

    // Strip first path component (GitHub's "owner-repo-sha/")
    const parts = fullPath.split('/')
    const relativePath = parts.slice(1).join('/')
    if (!relativePath) {
      offset += Math.ceil(fileSize / 512) * 512
      continue
    }

    const outputPath = `${targetDir}/${relativePath}`

    if (typeFlag === '5' || rawName.endsWith('/')) {
      if (!createdDirs.has(outputPath)) {
        try {
          await fs.mkdir(outputPath, { recursive: true })
        } catch {
          /* exists */
        }
        createdDirs.add(outputPath)
      }
    } else if (typeFlag === '0' || typeFlag === NUL) {
      const parentDir = outputPath.substring(0, outputPath.lastIndexOf('/'))
      if (parentDir && !createdDirs.has(parentDir)) {
        try {
          await fs.mkdir(parentDir, { recursive: true })
        } catch {
          /* exists */
        }
        createdDirs.add(parentDir)
      }
      const fileData = data.subarray(offset, offset + fileSize)
      await fs.writeFile(outputPath, fileData)
    }

    offset += Math.ceil(fileSize / 512) * 512
  }
}
