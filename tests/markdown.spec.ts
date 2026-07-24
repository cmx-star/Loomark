import { describe, expect, it } from 'vitest'
import { renderMarkdown } from '@/domain/markdown'

describe('Markdown renderer', () => {
  it('renders Markdown headings and emphasis', () => {
    expect(renderMarkdown('# Notes\n\n*focused*')).toContain('<h1>Notes</h1>')
    expect(renderMarkdown('# Notes\n\n*focused*')).toContain('<em>focused</em>')
  })

  it('does not render raw HTML from a document', () => {
    const html = renderMarkdown('<script>alert(1)</script>')
    expect(html).toContain('&lt;script&gt;')
    expect(html).not.toContain('<script>')
  })
})
