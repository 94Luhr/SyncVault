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
- [x] Fixed-size chunking and SHA-256 content hashing
- [x] Atomic content-addressed chunk storage and deduplication
- [x] Atomic snapshot manifest creation and listing
- [x] Atomic file and directory restore with chunk verification
- [x] Repository, manifest, and chunk integrity verification
- [x] Incremental repository-to-repository synchronization
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

Split a file into 4 MiB chunks and print their SHA-256 digests:

```powershell
build/debug/syncvault.exe chunks D:/data/large-file.bin
```

Store a file's chunks in the repository (running it again reuses unchanged
chunks):

```powershell
build/debug/syncvault.exe store D:/syncvault-repository D:/data/large-file.bin
```

Create and list directory snapshots:

```powershell
build/debug/syncvault.exe snapshot create D:/syncvault-repository D:/data
build/debug/syncvault.exe snapshot list D:/syncvault-repository
```

Restore a snapshot into an absent or empty directory:

```powershell
build/debug/syncvault.exe snapshot restore D:/syncvault-repository SNAPSHOT_ID D:/restored
```

Verify all manifests and stored chunks:

```powershell
build/debug/syncvault.exe verify D:/syncvault-repository
```

Synchronize only missing chunks and snapshots into another repository:

```powershell
build/debug/syncvault.exe sync D:/syncvault-repository D:/syncvault-copy
```

## MVP acceptance criteria

1. Repeated snapshots store unchanged file content only once.
2. A selected snapshot can be restored into an empty directory byte-for-byte.
3. Interrupted writes never publish a partial snapshot.
4. Repository verification detects missing or corrupted chunks.
5. Automated tests cover normal operation and expected failure paths.

## License

SyncVault is released under the [MIT License](LICENSE).
