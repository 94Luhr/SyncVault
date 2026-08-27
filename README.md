# SyncVault

[English](README.md) | [简体中文](README.zh-CN.md)

SyncVault is a C++20 incremental backup and file synchronization project. The
first milestone focuses on safe local snapshots, content-addressed chunk
storage, restore, and repository verification. Network synchronization will be
added after the local storage model is reliable.

## Current status

- [x] CMake-based C++20 project
- [x] Repository initialization
- [x] Atomic repository configuration creation
- [x] Automated repository layout tests
- [x] Directory scanning and file metadata
- [ ] Fixed-size chunking and content hashing
- [ ] Snapshot creation and listing
- [ ] File and directory restore
- [ ] Integrity verification
- [ ] Incremental network synchronization

## Repository layout

```text
repository/
|-- config
|-- chunks/
|-- snapshots/
`-- tmp/
```

## Build on Windows

The repository includes a build script that locates the installed Visual
Studio C++ toolchain, CMake, and Ninja automatically:

```powershell
.\scripts\build.ps1
```

For an optimized build, run `.\scripts\build.ps1 Release`.

Initialize a repository:

```powershell
build/debug/syncvault.exe init D:/syncvault-repository
```

Scan a source directory:

```powershell
build/debug/syncvault.exe scan D:/data
```

## MVP acceptance criteria

1. Repeated snapshots store unchanged file content only once.
2. A selected snapshot can be restored into an empty directory byte-for-byte.
3. Interrupted writes never publish a partial snapshot.
4. Repository verification detects missing or corrupted chunks.
5. Automated tests cover normal operation and expected failure paths.

## License

SyncVault is released under the [MIT License](LICENSE).
