/**
 * ssh_napi_sftp_binary.cpp — SFTP binary/streaming: OpenFile, Read, Write, CloseFile
 */
#include <vector>
#include <libssh2_sftp.h>
#include "napi_helpers.h"

struct SftpBinaryData {
    SshContext *ctx;
    bool success = false;
    std::string errorMsg;
    int opType = 0; // 1:open, 2:read, 3:write, 4:close
    std::string path;
    int fd = -1;
    long flags = 0;
    long mode = 0;
    size_t size = 0;
    std::vector<uint8_t> buffer;
    napi_deferred deferred;
    napi_async_work work;
};

static void SftpBinaryExec(napi_env, void *data) {
    auto *d = static_cast<SftpBinaryData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp && d->opType != 4) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    if (d->opType == 1) { // open
        LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(ctx->sftp, d->path.c_str(), d->flags, d->mode);
        if (!fh) { d->errorMsg = "Failed to open file"; return; }
        d->fd = ctx->nextFd++;
        ctx->openFiles[d->fd] = fh;
        d->success = true;
    } else if (d->opType == 2) { // read
        auto it = ctx->openFiles.find(d->fd);
        if (it == ctx->openFiles.end()) { d->errorMsg = "Invalid fd"; return; }
        d->buffer.resize(d->size);
        ssize_t n = libssh2_sftp_read(it->second, reinterpret_cast<char*>(d->buffer.data()), d->size);
        if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) { d->errorMsg = "Read error"; return; }
        if (n < 0) n = 0;
        d->buffer.resize(n);
        d->success = true;
    } else if (d->opType == 3) { // write
        auto it = ctx->openFiles.find(d->fd);
        if (it == ctx->openFiles.end()) { d->errorMsg = "Invalid fd"; return; }
        const char *p = reinterpret_cast<const char*>(d->buffer.data());
        size_t rem = d->buffer.size();
        while (rem > 0) {
            ssize_t n = libssh2_sftp_write(it->second, p, rem);
            if (n > 0) { p += n; rem -= n; }
            else if (n == LIBSSH2_ERROR_EAGAIN) continue;
            else { d->errorMsg = "Write error"; return; }
        }
        d->success = true;
    } else if (d->opType == 4) { // close
        auto it = ctx->openFiles.find(d->fd);
        if (it == ctx->openFiles.end()) { d->errorMsg = "Invalid fd"; return; }
        libssh2_sftp_close(it->second);
        ctx->openFiles.erase(it);
        d->success = true;
    }
}

static void SftpBinaryComplete(napi_env env, napi_status, void *data) {
    auto *d = static_cast<SftpBinaryData *>(data);
    if (d->success) {
        napi_value result;
        if (d->opType == 1) {
            napi_create_int32(env, d->fd, &result);
            napi_resolve_deferred(env, d->deferred, result);
        } else if (d->opType == 2) {
            void* arrayData = nullptr;
            napi_create_arraybuffer(env, d->buffer.size(), &arrayData, &result);
            if (d->buffer.size() > 0) memcpy(arrayData, d->buffer.data(), d->buffer.size());
            napi_resolve_deferred(env, d->deferred, result);
        } else {
            NapiResolveUndefined(env, d->deferred);
        }
    } else {
        NapiRejectWithError(env, d->deferred, d->errorMsg);
    }
    napi_delete_async_work(env, d->work);
    delete d;
}

napi_value NapiSftpOpenFile(napi_env env, napi_callback_info info) {
    size_t argc = 4; napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 4) return ThrowError(env, "Requires handle, path, flags, mode");
    auto *d = new SftpBinaryData(); d->opType = 1;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->path = NapiGetString(env, args[1]);
    napi_get_value_int32(env, args[2], (int32_t*)&d->flags);
    napi_get_value_int32(env, args[3], (int32_t*)&d->mode);
    return NapiCreatePromiseWork(env, d, "sftp_open", SftpBinaryExec, SftpBinaryComplete, &d->deferred, &d->work);
}

napi_value NapiSftpRead(napi_env env, napi_callback_info info) {
    size_t argc = 3; napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "Requires handle, fd, size");
    auto *d = new SftpBinaryData(); d->opType = 2;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    napi_get_value_int32(env, args[1], &d->fd);
    int32_t sz = 0; napi_get_value_int32(env, args[2], &sz);
    d->size = sz > 0 ? sz : 0;
    return NapiCreatePromiseWork(env, d, "sftp_read", SftpBinaryExec, SftpBinaryComplete, &d->deferred, &d->work);
}

napi_value NapiSftpWrite(napi_env env, napi_callback_info info) {
    size_t argc = 3; napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "Requires handle, fd, arraybuffer");
    auto *d = new SftpBinaryData(); d->opType = 3;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    napi_get_value_int32(env, args[1], &d->fd);
    bool isAB = false; napi_is_arraybuffer(env, args[2], &isAB);
    if (isAB) {
        void* ptr = nullptr; size_t len = 0;
        napi_get_arraybuffer_info(env, args[2], &ptr, &len);
        if (len > 0 && ptr) d->buffer.assign((uint8_t*)ptr, (uint8_t*)ptr + len);
    }
    return NapiCreatePromiseWork(env, d, "sftp_write", SftpBinaryExec, SftpBinaryComplete, &d->deferred, &d->work);
}

napi_value NapiSftpCloseFile(napi_env env, napi_callback_info info) {
    size_t argc = 2; napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "Requires handle, fd");
    auto *d = new SftpBinaryData(); d->opType = 4;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    napi_get_value_int32(env, args[1], &d->fd);
    return NapiCreatePromiseWork(env, d, "sftp_close_file", SftpBinaryExec, SftpBinaryComplete, &d->deferred, &d->work);
}
