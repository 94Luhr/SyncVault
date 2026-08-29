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
- [x] Read-only synchronization planning and transfer estimation
- [x] Versioned binary network frames with SHA-256 payload verification
- [x] TCP client/server connection and version handshake
- [x] TCP incremental content-addressed chunk transfer
- [x] TCP snapshot manifest transfer with verified restore
- [x] HMAC-SHA256 challenge-response authentication
- [x] Authenticated LAN access with configurable IPv4 binding

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

Preview the required transfer without changing the destination:

```powershell
build/debug/syncvault.exe sync --dry-run D:/syncvault-repository D:/syncvault-copy
```

Start a one-connection loopback server, then test the protocol handshake
from another terminal:

```powershell
build/debug/syncvault.exe serve --once D:/syncvault-repository 39761
build/debug/syncvault.exe ping 127.0.0.1 39761
```

Transfer only missing content-addressed chunks:

```powershell
build/debug/syncvault.exe serve --once-sync D:/syncvault-copy 39762
build/debug/syncvault.exe sync-network D:/syncvault-repository 127.0.0.1 39762
```

For authenticated loopback synchronization, set the same secret in both
terminals and use the authenticated commands:

```powershell
$env:SYNCVAULT_TOKEN = "replace-with-a-long-random-secret"
build/debug/syncvault.exe serve --once-sync-auth D:/syncvault-copy 39763 0.0.0.0
build/debug/syncvault.exe sync-network-auth D:/syncvault-repository SERVER_LAN_IP 39763
```

The optional final server argument is the numeric IPv4 bind address.
`0.0.0.0` listens on every IPv4 interface; a specific LAN address is safer.
Non-loopback binding is rejected unless authentication is enabled. The secret
is used in an HMAC-SHA256 challenge-response and is never sent over the
connection. Windows Firewall may require an inbound rule for the selected port.

## MVP acceptance criteria

1. Repeated snapshots store unchanged file content only once.
2. A selected snapshot can be restored into an empty directory byte-for-byte.
3. Interrupted writes never publish a partial snapshot.
4. Repository verification detects missing or corrupted chunks.
5. Automated tests cover normal operation and expected failure paths.

## License

SyncVault is released under the [MIT License](LICENSE).
