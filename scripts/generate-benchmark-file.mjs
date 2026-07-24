import { mkdir, writeFile } from 'node:fs/promises'
import { resolve } from 'node:path'

const MiB = 1024 * 1024
const requestedSizes = process.argv.slice(2).length > 0 ? process.argv.slice(2).map(Number) : [1, 10, 50]
const targetDirectory = resolve('.benchmark-fixtures')
const line = '# Loomark benchmark\n\nA deterministic Markdown paragraph for loader validation.\n\n'

await mkdir(targetDirectory, { recursive: true })
for (const size of requestedSizes) {
  if (!Number.isInteger(size) || size <= 0) throw new Error(`Invalid MiB size: ${size}`)
  const requiredBytes = size * MiB
  const repetitions = Math.floor(requiredBytes / Buffer.byteLength(line))
  const remainder = requiredBytes - repetitions * Buffer.byteLength(line)
  const content = line.repeat(repetitions) + 'x'.repeat(remainder)
  const path = resolve(targetDirectory, `loomark-${size}MiB.md`)
  await writeFile(path, content, 'utf8')
  console.log(`${path}: ${Buffer.byteLength(content)} bytes`)
}
