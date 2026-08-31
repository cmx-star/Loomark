# M16 命令与状态系统

## Goal

批次4 M16：集中命令注册——命令根据当前会话/档位/装载状态正确启停（open/save/
save-as/reload/find/replace-all/workspace），替代分散的 setEnabled 调用；F03 收口。

## Requirements

- CommandRegistry：命令名 → QAction + 启用谓词；updateCommands() 统一求值
- 谓词输入：无会话 / 有会话 / 装载中 / 脏 / 大档位
- 谓词单测：空窗口、已打开、装载中、脏编辑四种状态下的启停矩阵
- F03「可长期使用的多文档工作区骨架」验收：多标签 + 工作区 + 最近文件 + 命令系统
  联合冒烟

## Acceptance Criteria

- [ ] 启停矩阵测试通过
- [ ] 现有测试无回归；git diff --check 通过
- [ ] F03 端到端冒烟记录（verification.md）
