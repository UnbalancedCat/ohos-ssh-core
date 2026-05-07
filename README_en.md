# HarmonyOS SSH Core - ohos-ssh-core

[简体中文](./README.md) | English

This project is a native SSH and SFTP client library designed for HarmonyOS. Built upon industry-standard C/C++ open-source implementations, it provides an asynchronous ArkTS interface via N-API, empowering HarmonyOS applications with native capabilities for remote server management and file transfer.

The project is divided into two main modules: `ssh_lib` (the core HAR library) and `entry` (the demonstration app).

## Detailed API Documentation

For complete ArkTS API usage, parameter descriptions, and advanced streaming operation guides, please refer to the API document in the same directory:
👉 **[API Reference (API.md)](./API.md)**

## Underlying Dependencies & Simplification

To provide optimal compatibility and size control on HarmonyOS, this project integrates pre-compiled binaries or source code from the following three open-source repositories. Necessary platform adaptations have been made, and redundant components (such as test cases and CLI tools) have been stripped:

1. **[libssh2](https://github.com/libssh2/libssh2)**
   - **Purpose**: Provides underlying SSH2 protocol parsing, channel multiplexing, and SFTP subsystem implementation.
   - **Adaptation & Simplification**: Test codes and build scripts have been removed. Blocking POSIX socket operations are made asynchronous via N-API and wrapped as Promises to prevent blocking the main ArkTS thread.
2. **[OpenSSL](https://github.com/openssl/openssl)** (Primary Cryptography Backend)
   - **Purpose**: Provides high-performance cryptographic algorithm support (e.g., RSA/Ed25519 key parsing, stream encryption/decryption).
   - **Adaptation & Simplification**: Pre-compiled static libraries (`libcrypto.a`, `libssl.a`) for HarmonyOS architectures (e.g., arm64-v8a). To minimize the installation package size, all non-core executables, engine plugins, and legacy configuration scripts have been removed.
3. **[mbedTLS](https://github.com/Mbed-TLS/mbedtls)** (Optional Lightweight Backend)
   - **Purpose**: Serves as a lightweight alternative to OpenSSL.
   - **Adaptation**: Can be toggled via the `USE_OPENSSL` flag in `ssh_lib/src/main/cpp/CMakeLists.txt`, suitable for scenarios with extremely strict package size constraints.

## Core Features

1. **Comprehensive Authentication Support**
   - Native support for password login.
   - Powered by OpenSSL, it automatically parses standard OpenSSH private keys (e.g., `-----BEGIN OPENSSH PRIVATE KEY-----`) and supports passphrase-protected keys. Developers do not need to manually extract public keys.
2. **Interactive Shell Communication**
   - Maintains an independent C++ socket reading thread internally. When terminal data arrives, it non-blockingly and safely pushes data to ArkTS using N-API's `napi_threadsafe_function`.
   - Exposes a PTY resizing interface (`resizePty`), facilitating seamless frontend integration with terminal UI components like `xterm.js`.
3. **Hierarchical SFTP File Management**
   - **Standard File Operations**: Provides POSIX-semantic interfaces for directory reading, attribute querying (`stat`), creation, deletion, and renaming.
   - **Quick Read/Write**: Offers interfaces to directly read remote files into memory as `string` or `ArrayBuffer`, ideal for small files like configurations.
   - **Streaming Large File Transfer**: Exposes low-level `open`, `read`, `write`, `close` interfaces (returning file descriptors). The ArkTS side can control chunk sizes via loop transfers, effectively managing memory peaks and easily implementing upload/download progress bars.

## Runnable Demonstration (Demo)

The `entry` module includes a complete testing UI that objectively showcases all core functionalities of `ssh_lib`. It can be run directly on the HarmonyOS emulator or a physical device.

- **Entry File**: [`entry/src/main/ets/pages/Index.ets`](./entry/src/main/ets/pages/Index.ets)
- **Demo Features**:
  - Connect by inputting target IP, port, username, and credentials.
  - **Exec Mode**: Execute a single non-interactive command (e.g., `ls /`) and print the output.
  - **Shell Mode**: Start a PTY and allow users to send real-time terminal commands to the server via an input box.
  - **SFTP Tests**: One-click tests for listing directories (`sftpListDir`), reading system text files (`/etc/hostname`), and streaming binary reads of system commands (e.g., `/bin/ls`) with ELF magic byte verification.

## How to Integrate

In your DevEco Studio HarmonyOS project, import `ssh_lib` as a local shared package. Add it to the `oh-package.json5` of the module where it's needed:

```json
"dependencies": {
  "ssh_lib": "file:../path/to/ssh_core/ssh_lib"
}
```
Then execute `ohpm install` (or click Sync in the IDE) to start using it.

## License

This project is licensed under the [Apache License 2.0](./LICENSE).

The third-party components integrated into this project are distributed under their original open-source licenses:
- **OpenSSL**: Apache License 2.0
- **mbedTLS**: Apache License 2.0
- **libssh2**: BSD 3-Clause License

For detailed license texts and copyright notices, please refer to the [`LICENSE`](./LICENSE) file in the root directory.
