# SyncVault

[English](README.md) | [简体中文](README.zh-CN.md)

SyncVault 是一个使用 C++20 开发的增量备份与文件同步项目。第一个里程碑专注于安全的本地快照、内容寻址的数据块存储、数据恢复和仓库完整性校验。在本地存储模型达到可靠状态后，项目将继续实现网络增量同步。

## 当前进度

- [x] 基于 CMake 的 C++20 项目
- [x] 备份仓库初始化
- [x] 原子创建仓库配置
- [x] 仓库目录结构自动化测试
- [x] 目录扫描与文件元数据
- [x] 固定大小分块与 SHA-256 内容哈希
- [ ] 创建和列出快照
- [ ] 文件及目录恢复
- [ ] 数据完整性校验
- [ ] 网络增量同步

## 仓库目录结构

```text
repository/
|-- config
|-- chunks/
|-- snapshots/
`-- tmp/
```

## 在 Windows 上构建

项目提供了一键构建脚本，可自动定位已安装的 Visual Studio C++ 工具链、CMake 和 Ninja：

```powershell
.\scripts\build.ps1
```

如需构建优化版本，请运行 `.\scripts\build.ps1 Release`。

初始化一个备份仓库：

```powershell
build/debug/syncvault.exe init D:/syncvault-repository
```

扫描源目录：

```powershell
build/debug/syncvault.exe scan D:/data
```

将文件分割为 4 MiB 数据块并输出 SHA-256 哈希：

```powershell
build/debug/syncvault.exe chunks D:/data/large-file.bin
```

## MVP 验收标准

1. 重复创建快照时，未变化的文件内容只保存一次。
2. 可以将指定快照逐字节正确恢复到空目录。
3. 写入被中断时，不会发布不完整的快照。
4. 仓库校验能够发现丢失或损坏的数据块。
5. 自动化测试覆盖正常流程和预期的失败场景。

## 许可证

SyncVault 使用 [MIT License](LICENSE) 开源。