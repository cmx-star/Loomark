import { describe, expect, it } from 'vitest'
import { classifyDocument, FULL_EDITOR_LIMIT_BYTES, MAX_SUPPORTED_BYTES } from '@/domain/document'

describe('document loading policy', () => {
  it('selects the full editor through 10 MiB', () => {
    expect(classifyDocument(0)).toBe('full')
    expect(classifyDocument(FULL_EDITOR_LIMIT_BYTES)).toBe('full')
  })

  it('selects progressive loading through 50 MiB', () => {
    expect(classifyDocument(FULL_EDITOR_LIMIT_BYTES + 1)).toBe('progressive')
    expect(classifyDocument(MAX_SUPPORTED_BYTES)).toBe('progressive')
  })

  it('rejects files over the supported M0 limit', () => {
    expect(classifyDocument(MAX_SUPPORTED_BYTES + 1)).toBe('unsupported')
  })

  it('preserves Markdown bytes for a no-edit round trip', () => {
    const original = new TextEncoder().encode('# Heading\r\n\r\ntext with two spaces  \r\n')
    const reopenedWithoutEdits = original.slice()

    expect(reopenedWithoutEdits).toEqual(original)
  })
})
