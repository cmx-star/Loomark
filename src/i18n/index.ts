import { createI18n } from 'vue-i18n'

const LOCALE_KEY = 'loomark.locale.v1'
const LEGACY_LOCALE_KEY = 'marko.locale.v1'

export const messages = {
  'zh-CN': {
    app: {
      emptyLabel: '本地 Markdown 工作区',
      emptyText: '打开 Markdown 文件后，可以编辑源码、阅读排版结果，或在分屏中同时进行。',
      progressivePreview: '正在显示文件开头，完整源码在后台加载。',
      workspace: '文档工作区',
    },
    menu: {
      appearance: '外观',
      closeTab: '关闭标签页',
      copy: '复制',
      cut: '剪切',
      edit: '编辑',
      file: '文件',
      fullscreen: '全屏',
      language: '语言',
      open: '打开...',
      openLogDirectory: '打开日志目录',
      paste: '粘贴',
      print: '打印...',
      redo: '重做',
      save: '保存',
      selectAll: '全选',
      toggleNavigator: '显示/隐藏目录',
      undo: '撤销',
      view: '视图',
      window: '窗口',
    },
    navigator: {
      collapse: '收起目录',
      currentDirectory: '当前目录',
      directoryEmpty: '此目录中没有 Markdown 文件。',
      directoryTruncated: '仅显示前 {count} 个文件。',
      directoryUnavailable: '无法读取当前目录。',
      expand: '展开目录',
      files: '已打开文件',
      empty: '从“文件”菜单打开 Markdown 文件。',
      up: '上一级',
    },
    editor: {
      source: '源码',
      reading: '阅读',
      split: '分屏',
      sourceLabel: 'Markdown 源码编辑器',
    },
    externalChange: {
      cleanDescription: '文件已在应用外更新。重新加载将显示磁盘上的最新版本。',
      dirtyDescription: '文件已在应用外更新。重新加载会丢弃当前未保存的编辑。',
      keep: '保留当前编辑',
      reload: '重新加载',
      title: '检测到外部文件变更',
    },
    errors: {
      editorInitialization: 'Markdown 编辑器初始化失败。请查看 Loomark 日志以获取详细原因。',
      maxSize: 'M1 支持最大 50 MiB 的 Markdown 文件。',
    },
    language: {
      label: '语言',
      'zh-CN': '简体中文',
      'en-US': 'English',
    },
    status: {
      editor: '编辑器',
      empty: '选择 Markdown 文件以查看原生加载路径和性能指标。',
      lines: '行数',
      longestLine: '最长行',
      loading: '加载中',
      loadReport: '加载报告',
      phase: '阶段',
      preflight: '预检',
      full: '完整加载',
      progressive: '渐进加载',
      read: '读取',
      ready: '就绪',
      size: '大小',
      unsupported: '不支持',
      waiting: '等待',
    },
    tabs: {
      close: '关闭 {title}',
      openDocuments: '已打开文档',
      unsaved: '未保存修改',
    },
    theme: {
      label: '主题',
      night: '夜间',
      paper: '纸张',
    },
    toolbar: {
      actions: '文档操作',
      mode: '编辑模式',
      open: '打开',
      print: '打开打印对话框',
      save: '保存',
      subtitle: '本地 Markdown 工作区',
    },
  },
  'en-US': {
    app: {
      emptyLabel: 'Local Markdown workspace',
      emptyText: 'Open a Markdown file to edit source, read it, or work side by side.',
      progressivePreview: 'Showing the start of the file while the full source loads in the background.',
      workspace: 'Document workspace',
    },
    menu: {
      appearance: 'Appearance',
      closeTab: 'Close Tab',
      copy: 'Copy',
      cut: 'Cut',
      edit: 'Edit',
      file: 'File',
      fullscreen: 'Fullscreen',
      language: 'Language',
      open: 'Open...',
      openLogDirectory: 'Open Log Directory',
      paste: 'Paste',
      print: 'Print...',
      redo: 'Redo',
      save: 'Save',
      selectAll: 'Select All',
      toggleNavigator: 'Show or Hide Navigator',
      undo: 'Undo',
      view: 'View',
      window: 'Window',
    },
    navigator: {
      collapse: 'Collapse navigator',
      currentDirectory: 'Current folder',
      directoryEmpty: 'No Markdown files in this folder.',
      directoryTruncated: 'Showing the first {count} files.',
      directoryUnavailable: 'Cannot read the current folder.',
      expand: 'Expand navigator',
      files: 'Open files',
      empty: 'Open a Markdown file from the File menu.',
      up: 'Up',
    },
    editor: {
      source: 'Source',
      reading: 'Reading',
      split: 'Split',
      sourceLabel: 'Markdown source editor',
    },
    externalChange: {
      cleanDescription: 'This file changed outside Loomark. Reload to show the version on disk.',
      dirtyDescription: 'This file changed outside Loomark. Reload discards your unsaved edits.',
      keep: 'Keep current edits',
      reload: 'Reload',
      title: 'External file change detected',
    },
    errors: {
      editorInitialization: 'The Markdown editor could not initialize. Check the Loomark log for details.',
      maxSize: 'M1 supports Markdown files up to 50 MiB.',
    },
    language: {
      label: 'Language',
      'zh-CN': 'Simplified Chinese',
      'en-US': 'English',
    },
    status: {
      editor: 'editor',
      empty: 'Choose a Markdown file to inspect its native loading route and performance metrics.',
      lines: 'lines',
      longestLine: 'longest line',
      loading: 'loading',
      loadReport: 'Load report',
      phase: 'phase',
      preflight: 'preflight',
      full: 'full load',
      progressive: 'progressive load',
      read: 'read',
      ready: 'ready',
      size: 'size',
      unsupported: 'unsupported',
      waiting: 'waiting',
    },
    tabs: {
      close: 'Close {title}',
      openDocuments: 'Open documents',
      unsaved: 'Unsaved changes',
    },
    theme: {
      label: 'Theme',
      night: 'Night',
      paper: 'Paper',
    },
    toolbar: {
      actions: 'Document actions',
      mode: 'Editor mode',
      open: 'Open',
      print: 'Open print dialog',
      save: 'Save',
      subtitle: 'Local Markdown workspace',
    },
  },
} as const

export type AppLocale = keyof typeof messages

export function resolveLocale(value: string | null | undefined): AppLocale {
  return value === 'en-US' ? 'en-US' : 'zh-CN'
}

function initialLocale(): AppLocale {
  if (typeof window === 'undefined') return 'zh-CN'
  const storedLocale = window.localStorage.getItem(LOCALE_KEY)
  if (storedLocale) return resolveLocale(storedLocale)

  const legacyLocale = window.localStorage.getItem(LEGACY_LOCALE_KEY)
  if (!legacyLocale) return 'zh-CN'
  const locale = resolveLocale(legacyLocale)
  window.localStorage.setItem(LOCALE_KEY, locale)
  window.localStorage.removeItem(LEGACY_LOCALE_KEY)
  return locale
}

export const i18n = createI18n({
  legacy: false,
  locale: initialLocale(),
  fallbackLocale: 'en-US',
  messages,
})

export function persistLocale(locale: AppLocale) {
  window.localStorage.setItem(LOCALE_KEY, locale)
  window.localStorage.removeItem(LEGACY_LOCALE_KEY)
}
