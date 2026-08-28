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
- [x] 原子化内容寻址存储与数据块去重
- [x] 原子创建并列出快照清单
- [x] 带数据块校验的原子文件与目录恢复
- [x] 仓库、快照清单与数据块完整性校验
- [x] 仓库到仓库的增量同步
- [x] 只读同步规划与传输量预估
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

将文件的数据块存入仓库（再次执行时会复用未变化的数据块）：

```powershell
build/debug/syncvault.exe store D:/syncvault-repository D:/data/large-file.bin
```

创建并列出目录快照：

```powershell
build/debug/syncvault.exe snapshot create D:/syncvault-repository D:/data
build/debug/syncvault.exe snapshot list D:/syncvault-repository
```

将快照恢复到不存在或空的目录：

```powershell
build/debug/syncvault.exe snapshot restore D:/syncvault-repository SNAPSHOT_ID D:/restored
```

校验所有快照清单和已存储的数据块：

```powershell
build/debug/syncvault.exe verify D:/syncvault-repository
```

仅将目标仓库缺少的数据块和快照同步过去：

```powershell
build/debug/syncvault.exe sync D:/syncvault-repository D:/syncvault-copy
```

预览需要传输的内容，但不修改目标仓库：

```powershell
build/debug/syncvault.exe sync --dry-run D:/syncvault-repository D:/syncvault-copy
```

## MVP 验收标准

1. 重复创建快照时，未变化的文件内容只保存一次。
2. 可以将指定快照逐字节正确恢复到空目录。
3. 写入被中断时，不会发布不完整的快照。
4. 仓库校验能够发现丢失或损坏的数据块。
5. 自动化测试覆盖正常流程和预期的失败场景。

## 许可证

SyncVault 使用 [MIT License](LICENSE) 开源。