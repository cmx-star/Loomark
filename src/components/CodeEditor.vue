<script setup lang="ts">
import { EditorState } from '@codemirror/state'
import { defaultKeymap, history, historyKeymap } from '@codemirror/commands'
import { markdown } from '@codemirror/lang-markdown'
import { EditorView, keymap, lineNumbers } from '@codemirror/view'
import { onBeforeUnmount, onMounted, shallowRef, useTemplateRef, watch } from 'vue'
import { useI18n } from 'vue-i18n'

const props = defineProps<{ content: string }>()
const emit = defineEmits<{
  initialized: [milliseconds: number]
  'update:content': [content: string]
}>()
const host = useTemplateRef<HTMLDivElement>('editorHost')
const editorView = shallowRef<EditorView | null>(null)
const { t } = useI18n()

function createEditor(content: string) {
  if (!host.value) return
  editorView.value?.destroy()
  const startedAt = performance.now()
  editorView.value = new EditorView({
    state: EditorState.create({
      doc: content,
      extensions: [
        lineNumbers(),
        history(),
        keymap.of([...defaultKeymap, ...historyKeymap]),
        markdown(),
        EditorView.updateListener.of((update) => {
          if (update.docChanged) emit('update:content', update.state.doc.toString())
        }),
      ],
    }),
    parent: host.value,
  })
  emit('initialized', performance.now() - startedAt)
}

onMounted(() => createEditor(props.content))
watch(() => props.content, (content) => {
  const view = editorView.value
  if (!view || view.state.doc.toString() === content) return
  view.dispatch({ changes: { from: 0, to: view.state.doc.length, insert: content } })
})
onBeforeUnmount(() => editorView.value?.destroy())
</script>

<template>
  <div ref="editorHost" class="editor-host" :aria-label="t('editor.sourceLabel')" />
</template>

<style scoped>
.editor-host { height: 100%; min-height: 0; overflow: hidden; }
.editor-host :deep(.cm-editor) { background: var(--surface); color: var(--ink); height: 100%; }
.editor-host :deep(.cm-content) { caret-color: var(--accent); color: var(--ink); }
.editor-host :deep(.cm-line) { color: var(--ink); }
.editor-host :deep(.cm-cursor), .editor-host :deep(.cm-dropCursor) { border-left-color: var(--accent) !important; }
.editor-host :deep(.cm-selectionBackground), .editor-host :deep(.cm-focused .cm-selectionBackground) { background: color-mix(in srgb, var(--accent) 42%, var(--surface)) !important; }
.editor-host :deep(.cm-scroller) { font-family: ui-monospace, SFMono-Regular, Menlo, Monaco, Consolas, monospace; font-size: 13px; line-height: 1.6; scrollbar-color: var(--line-strong) var(--surface); scrollbar-width: thin; }
.editor-host :deep(.cm-scroller::-webkit-scrollbar) { height: 10px; width: 10px; }
.editor-host :deep(.cm-scroller::-webkit-scrollbar-track) { background: var(--surface); }
.editor-host :deep(.cm-scroller::-webkit-scrollbar-thumb) { background: var(--line-strong); border: 3px solid var(--surface); border-radius: 8px; }
.editor-host:hover :deep(.cm-scroller), .editor-host:focus-within :deep(.cm-scroller) { scrollbar-color: var(--muted) var(--surface); }
.editor-host:hover :deep(.cm-scroller::-webkit-scrollbar-thumb), .editor-host:focus-within :deep(.cm-scroller::-webkit-scrollbar-thumb) { background: var(--muted); }
.editor-host :deep(.cm-gutters) { background: var(--panel); border-right: 1px solid var(--line); color: var(--muted); }
.editor-host :deep(.cm-activeLineGutter) { background: var(--accent-soft); }
</style>
