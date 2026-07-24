# Loomark

[English](README.en.md)

Loomark 是一个开源、local-first 的 Markdown 桌面编辑器，基于 Vue 3、Electron、TypeScript 与 CodeMirror 6 构建。它以原始 Markdown 为唯一文档状态，作为本地桌面应用运行，而不是浏览器页面。

默认界面为简体中文，当前提供 English 作为首个备用语言。

## 当前能力

- 打开、编辑、显式保存并重新打开本地 Markdown 文件。
- 提供源码、阅读和分屏模式；分屏中的源码与预览可独立滚动。
- 多标签、未保存修改提示、本地会话恢复、纸张/夜间主题与可收起的工作区目录。
- 原生应用菜单：文件、编辑、视图、外观、语言和窗口。
- 当前目录按层读取，仅显示 Markdown 文件与直接子目录。
- 监视已打开文档的外部保存。保存后会比较磁盘内容；内容未变时静默忽略，内容不同则提示用户重新加载或保留当前编辑，不会自动替换编辑器内容。
- 展示 Markdown 预检指标：文件大小、行数、最长行、读取时间和编辑器初始化时间。

## 大文件策略

Loomark 当前目标是支持不超过 50 MiB 的 Markdown 文件：

| 文件大小 | 默认行为 |
| --- | --- |
| 不超过 10 MiB | 将完整源码加载到 CodeMirror。 |
| 超过 10 MiB 至 50 MiB | 先进行元数据预检，显示有界预览，再在后台加载源码。完整预览、diff 和 AI 上下文不会走默认路径。 |
| 超过 50 MiB | 超出当前支持范围。 |

未编辑的文档不会调用保存命令，因此在文件系统允许时会保持原始字节不变。

## 开发

前置条件：

- Node.js 20 LTS 或更高版本
- pnpm 9 或更高版本
- Electron 41 的平台前置依赖

```bash
pnpm install --frozen-lockfile
pnpm electron:dev
```

常用检查：

```bash
pnpm typecheck
pnpm test:run
pnpm build
pnpm electron:package
```

生成桌面安装包：

```bash
pnpm electron:make
```

## 路线图

后续工作包括 Git 状态/diff、另存为、HTML/PDF/DOCX 导出、带显式权限的声明式插件，以及可选的 AI 写作辅助。约束和阶段计划见[开发计划](docs/development-plan.md)。

## 隐私与安全

Loomark 以本地优先为原则，当前文档操作都在本机完成。未来 AI 功能会要求用户明确选择供应商和上下文范围，不属于当前发布范围。

请通过 [GitHub Security Advisories](https://github.com/cmx-star/noteMD/security/advisories/new) 报告安全问题，不要公开创建 Issue。详见 [SECURITY.md](SECURITY.md)。

## 身份迁移

开发原型曾从 Marko 更名为 Loomark。新的桌面安装包使用 Bundle Identifier `io.md.loomark`。浏览器本地会话和语言设置在可访问时会迁移一次；操作系统会将新的 Bundle Identifier 视为独立应用，因此请在 Loomark 中检查本地文档前保留旧原型安装。

开发、打包和发布链路均使用 Electron。

## 参与贡献

欢迎贡献。创建 Issue 或 Pull Request 前，请阅读 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 许可证

Copyright 2026 Loomark contributors.

本项目采用 [Apache License 2.0](LICENSE) 许可。
