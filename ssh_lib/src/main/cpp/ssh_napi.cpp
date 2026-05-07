/**
 * ssh_napi.cpp — N-API module core: CreateContext, GetHostKeyFingerprint,
 *                and module registration (RegisterSshNapi).
 *
 * All other functions are implemented in:
 *   ssh_napi_connect.cpp  — Connect, ConnectWithKey, Disconnect
 *   ssh_napi_exec.cpp     — ExecCommand
 *   ssh_napi_shell.cpp    — StartShell, WriteToShell, ResizePty
 *   ssh_napi_sftp.cpp     — SFTP text operations
 *   ssh_napi_sftp_binary.cpp — SFTP binary/streaming operations
 */

#include "napi_helpers.h"

// ---------------------------------------------------------------------------
// Forward declarations for functions in other translation units
// ---------------------------------------------------------------------------
// ssh_napi_connect.cpp
extern napi_value NapiConnect(napi_env env, napi_callback_info info);
extern napi_value NapiConnectWithKey(napi_env env, napi_callback_info info);
extern napi_value NapiDisconnect(napi_env env, napi_callback_info info);
// ssh_napi_exec.cpp
extern napi_value NapiExecCommand(napi_env env, napi_callback_info info);
// ssh_napi_shell.cpp
extern napi_value NapiStartShell(napi_env env, napi_callback_info info);
extern napi_value NapiWriteToShell(napi_env env, napi_callback_info info);
extern napi_value NapiResizePty(napi_env env, napi_callback_info info);
// ssh_napi_sftp.cpp
extern napi_value NapiSftpInit(napi_env env, napi_callback_info info);
extern napi_value NapiSftpShutdown(napi_env env, napi_callback_info info);
extern napi_value NapiSftpListDir(napi_env env, napi_callback_info info);
extern napi_value NapiSftpReadFile(napi_env env, napi_callback_info info);
extern napi_value NapiSftpWriteFile(napi_env env, napi_callback_info info);
extern napi_value NapiSftpDelete(napi_env env, napi_callback_info info);
extern napi_value NapiSftpMkdir(napi_env env, napi_callback_info info);
extern napi_value NapiSftpRmdir(napi_env env, napi_callback_info info);
extern napi_value NapiSftpRename(napi_env env, napi_callback_info info);
extern napi_value NapiSftpStat(napi_env env, napi_callback_info info);
extern napi_value NapiSftpChmod(napi_env env, napi_callback_info info);
// ssh_napi_sftp_binary.cpp
extern napi_value NapiSftpOpenFile(napi_env env, napi_callback_info info);
extern napi_value NapiSftpRead(napi_env env, napi_callback_info info);
extern napi_value NapiSftpWrite(napi_env env, napi_callback_info info);
extern napi_value NapiSftpCloseFile(napi_env env, napi_callback_info info);

// ===================================================================
// createContext()
// ===================================================================
static napi_value CreateContext(napi_env env, napi_callback_info info) {
    napi_value jsHandle;
    napi_create_object(env, &jsHandle);

    SshContext *ctx = new SshContext();

    napi_wrap(env, jsHandle, ctx,
        [](napi_env /*env*/, void *data, void * /*hint*/) {
            SshContext *ctx = static_cast<SshContext *>(data);
            SSH_LOGI("SshContext GC finalizer invoked");
            ctx->cleanup();
            delete ctx;
        },
        nullptr, nullptr);

    SSH_LOGI("createContext: SshContext created");
    return jsHandle;
}

// ===================================================================
// getHostKeyFingerprint(handle) -> string (sync)
// ===================================================================
static napi_value GetHostKeyFingerprint(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) return ThrowError(env, "getHostKeyFingerprint requires 1 argument: handle");

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (!ctx || !ctx->session) return ThrowError(env, "Invalid handle or not connected");

    const char *hash = libssh2_hostkey_hash(ctx->session, LIBSSH2_HOSTKEY_HASH_SHA256);
    if (!hash) return ThrowError(env, "Failed to get host key hash");

    // Base64 encode 32 bytes (SHA-256)
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *src = (const unsigned char *)hash;
    char out[45];
    int j = 0;
    for (int i = 0; i < 32; i += 3) {
        int remaining = 32 - i;
        unsigned int n = ((unsigned int)src[i]) << 16;
        if (remaining > 1) n |= ((unsigned int)src[i+1]) << 8;
        if (remaining > 2) n |= (unsigned int)src[i+2];
        out[j++] = b64[(n >> 18) & 0x3F];
        out[j++] = b64[(n >> 12) & 0x3F];
        out[j++] = (remaining > 1) ? b64[(n >> 6) & 0x3F] : '=';
        out[j++] = (remaining > 2) ? b64[n & 0x3F] : '=';
    }
    out[j] = '\0';

    napi_value result;
    napi_create_string_utf8(env, out, j, &result);
    return result;
}

// ===================================================================
// Module registration
// ===================================================================
napi_value RegisterSshNapi(napi_env env, napi_value exports) {
    static bool libssh2Inited = false;
    if (!libssh2Inited) {
        int rc = libssh2_init(0);
        if (rc != 0) {
            SSH_LOGE("libssh2_init failed: %{public}d", rc);
        } else {
            SSH_LOGI("libssh2_init OK, version: %{public}s", libssh2_version(0));
            libssh2Inited = true;
        }
    }

    napi_property_descriptor desc[] = {
        {"createContext",  nullptr, CreateContext,  nullptr, nullptr, nullptr, napi_default, nullptr},
        {"connect",        nullptr, NapiConnect,        nullptr, nullptr, nullptr, napi_default, nullptr},
        {"connectWithKey", nullptr, NapiConnectWithKey, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"execCommand",    nullptr, NapiExecCommand,    nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startShell",     nullptr, NapiStartShell,     nullptr, nullptr, nullptr, napi_default, nullptr},
        {"writeToShell",   nullptr, NapiWriteToShell,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"resizePty",      nullptr, NapiResizePty,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"disconnect",     nullptr, NapiDisconnect,     nullptr, nullptr, nullptr, napi_default, nullptr},
        // SFTP text ops
        {"sftpInit",       nullptr, NapiSftpInit,       nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpShutdown",   nullptr, NapiSftpShutdown,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpListDir",    nullptr, NapiSftpListDir,    nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpReadFile",   nullptr, NapiSftpReadFile,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpWriteFile",  nullptr, NapiSftpWriteFile,  nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpDelete",     nullptr, NapiSftpDelete,     nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpMkdir",      nullptr, NapiSftpMkdir,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpRmdir",      nullptr, NapiSftpRmdir,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpRename",     nullptr, NapiSftpRename,     nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpStat",       nullptr, NapiSftpStat,       nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpChmod",      nullptr, NapiSftpChmod,      nullptr, nullptr, nullptr, napi_default, nullptr},
        // SFTP binary/streaming
        {"sftpOpenFile",   nullptr, NapiSftpOpenFile,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpRead",       nullptr, NapiSftpRead,       nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpWrite",      nullptr, NapiSftpWrite,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpCloseFile",  nullptr, NapiSftpCloseFile,  nullptr, nullptr, nullptr, napi_default, nullptr},
        // Security
        {"getHostKeyFingerprint", nullptr, GetHostKeyFingerprint, nullptr, nullptr, nullptr, napi_default, nullptr},
    };

    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    SSH_LOGI("ssh_core N-API module registered");
    return exports;
}
