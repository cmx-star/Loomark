<script setup lang="ts">
import { computed } from 'vue'
import { renderMarkdown } from '@/domain/markdown'

const props = defineProps<{ content: string }>()
const renderedHtml = computed(() => renderMarkdown(props.content))
</script>

<template>
  <section class="preview-scroll">
    <!-- markdown-it disables source HTML; only renderer-created HTML reaches this element. -->
    <article class="markdown-body" v-html="renderedHtml" />
  </section>
</template>

<style scoped>
.preview-scroll { height: 100%; overflow: auto; scrollbar-color: transparent transparent; scrollbar-width: thin; }
.preview-scroll::-webkit-scrollbar { height: 8px; width: 8px; }
.preview-scroll::-webkit-scrollbar-track { background: transparent; }
.preview-scroll::-webkit-scrollbar-thumb { background: transparent; border: 2px solid transparent; border-radius: 8px; }
.preview-scroll:hover, .preview-scroll:focus-within { scrollbar-color: var(--line-strong) transparent; }
.preview-scroll:hover::-webkit-scrollbar-thumb, .preview-scroll:focus-within::-webkit-scrollbar-thumb { background: var(--line-strong); }
.markdown-body { color: var(--ink); font-family: Georgia, "Songti SC", serif; font-size: 17px; line-height: 1.75; margin: 0 auto; max-width: 780px; padding: 44px clamp(24px, 7vw, 72px); }
.markdown-body :deep(h1), .markdown-body :deep(h2), .markdown-body :deep(h3) { font-family: Inter, ui-sans-serif, system-ui, sans-serif; line-height: 1.2; margin: 1.8em 0 .7em; }
.markdown-body :deep(h1) { font-size: 32px; }
.markdown-body :deep(h2) { border-bottom: 1px solid var(--line); font-size: 24px; padding-bottom: .35em; }
.markdown-body :deep(p), .markdown-body :deep(ul), .markdown-body :deep(ol) { margin: 0 0 1em; }
.markdown-body :deep(a) { color: var(--accent); }
.markdown-body :deep(code) { background: var(--code); border-radius: 3px; font-family: ui-monospace, SFMono-Regular, Menlo, monospace; font-size: .85em; padding: .15em .3em; }
.markdown-body :deep(pre) { background: var(--code); overflow: auto; padding: 14px; }
.markdown-body :deep(pre code) { padding: 0; }
.markdown-body :deep(blockquote) { border-left: 3px solid var(--accent); color: var(--muted); margin: 1em 0; padding-left: 16px; }
.markdown-body :deep(table) { border-collapse: collapse; width: 100%; }
.markdown-body :deep(th), .markdown-body :deep(td) { border: 1px solid var(--line); padding: 6px 9px; text-align: left; }
</style>
