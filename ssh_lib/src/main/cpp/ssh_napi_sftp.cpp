/**
 * ssh_napi_sftp.cpp — SFTP text operations
 */
#include <libssh2_sftp.h>
#include "napi_helpers.h"

struct SftpSimpleData {
    SshContext *ctx;
    bool success = false;
    std::string errorMsg;
    std::string result;
    std::string path;
    std::string path2;
    std::string data;
    int opType = 0;
    napi_deferred deferred;
    napi_async_work work;
};

static void SftpSimpleComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    if (d->success) {
        if (d->result.empty()) NapiResolveUndefined(env, d->deferred);
        else NapiResolveString(env, d->deferred, d->result);
    } else {
        NapiRejectWithError(env, d->deferred, d->errorMsg);
    }
    napi_delete_async_work(env, d->work);
    delete d;
}

// --- sftpInit ---
static void SftpInitExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (ctx->state != SshState::CONNECTED) { d->errorMsg = "Not connected"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    ctx->sftp = libssh2_sftp_init(ctx->session);
    if (!ctx->sftp) { d->errorMsg = "SFTP init failed: " + GetSessionError(ctx->session); return; }
    d->success = true;
}
napi_value NapiSftpInit(napi_env env, napi_callback_info info) {
    size_t argc = 1; napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) return ThrowError(env, "sftpInit requires handle");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    return NapiCreatePromiseWork(env, d, "sftp_init", SftpInitExec, SftpSimpleComplete, &d->deferred, &d->work);
}

// --- sftpShutdown ---
static void SftpShutdownExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    libssh2_sftp_shutdown(ctx->sftp);
    ctx->sftp = nullptr;
    d->success = true;
}
napi_value NapiSftpShutdown(napi_env env, napi_callback_info info) {
    size_t argc = 1; napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) return ThrowError(env, "sftpShutdown requires handle");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    return NapiCreatePromiseWork(env, d, "sftp_shutdown", SftpShutdownExec, SftpSimpleComplete, &d->deferred, &d->work);
}

// --- sftpListDir ---
static void SftpListDirExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    LIBSSH2_SFTP_HANDLE *dh = libssh2_sftp_opendir(ctx->sftp, d->path.c_str());
    if (!dh) { d->errorMsg = "Failed to open directory: " + d->path; return; }
    std::string json = "[";
    char nb[512]; LIBSSH2_SFTP_ATTRIBUTES a; bool first = true;
    while (true) {
        int rc = libssh2_sftp_readdir(dh, nb, sizeof(nb), &a);
        if (rc <= 0) break;
        std::string name(nb, rc);
        if (name == "." || name == "..") continue;
        if (!first) json += ","; first = false;
        bool isDir = (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(a.permissions);
        unsigned long sz = (a.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (unsigned long)a.filesize : 0;
        unsigned long pm = (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? a.permissions : 0;
        unsigned long mt = (a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? a.mtime : 0;
        std::string esc; for (char c : name) { if (c=='"') esc+="\\\""; else if (c=='\\') esc+="\\\\"; else esc+=c; }
        json += "{\"name\":\"" + esc + "\",\"size\":" + std::to_string(sz)
             + ",\"permissions\":" + std::to_string(pm) + ",\"mtime\":" + std::to_string(mt)
             + ",\"isDir\":" + (isDir?"true":"false") + "}";
    }
    json += "]"; libssh2_sftp_closedir(dh);
    d->result = json; d->success = true;
}
napi_value NapiSftpListDir(napi_env env, napi_callback_info info) {
    size_t argc = 2; napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "sftpListDir requires handle, path");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->path = NapiGetString(env, args[1]);
    return NapiCreatePromiseWork(env, d, "sftp_listdir", SftpListDirExec, SftpSimpleComplete, &d->deferred, &d->work);
}

// --- sftpReadFile ---
static void SftpReadFileExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(ctx->sftp, d->path.c_str(), LIBSSH2_FXF_READ, 0);
    if (!fh) { d->errorMsg = "Failed to open: " + d->path; return; }
    char buf[8192]; std::string content;
    for (;;) { ssize_t n = libssh2_sftp_read(fh, buf, sizeof(buf));
        if (n > 0) content.append(buf, n); else if (n == 0) break;
        else if (n == LIBSSH2_ERROR_EAGAIN) continue;
        else { d->errorMsg = "Read error"; libssh2_sftp_close(fh); return; }
    }
    libssh2_sftp_close(fh); d->result = content; d->success = true;
}
napi_value NapiSftpReadFile(napi_env env, napi_callback_info info) {
    size_t argc = 2; napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "sftpReadFile requires handle, path");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->path = NapiGetString(env, args[1]);
    return NapiCreatePromiseWork(env, d, "sftp_readfile", SftpReadFileExec, SftpSimpleComplete, &d->deferred, &d->work);
}

// --- sftpWriteFile ---
static void SftpWriteFileExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(ctx->sftp, d->path.c_str(),
        LIBSSH2_FXF_WRITE|LIBSSH2_FXF_CREAT|LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR|LIBSSH2_SFTP_S_IWUSR|LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IROTH);
    if (!fh) { d->errorMsg = "Failed to open for write: " + d->path; return; }
    const char *p = d->data.c_str(); size_t rem = d->data.size();
    while (rem > 0) { ssize_t n = libssh2_sftp_write(fh, p, rem);
        if (n > 0) { p += n; rem -= n; } else if (n == LIBSSH2_ERROR_EAGAIN) continue;
        else { d->errorMsg = "Write error"; libssh2_sftp_close(fh); return; }
    }
    libssh2_sftp_close(fh); d->success = true;
}
napi_value NapiSftpWriteFile(napi_env env, napi_callback_info info) {
    size_t argc = 3; napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "sftpWriteFile requires handle, path, data");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->path = NapiGetString(env, args[1]); d->data = NapiGetString(env, args[2]);
    return NapiCreatePromiseWork(env, d, "sftp_writefile", SftpWriteFileExec, SftpSimpleComplete, &d->deferred, &d->work);
}

// --- sftpDelete / sftpMkdir / sftpRmdir ---
static void SftpPathOpExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    int rc = -1;
    switch (d->opType) {
        case 1: rc = libssh2_sftp_unlink(ctx->sftp, d->path.c_str()); break;
        case 2: rc = libssh2_sftp_mkdir(ctx->sftp, d->path.c_str(),
                    LIBSSH2_SFTP_S_IRWXU|LIBSSH2_SFTP_S_IRGRP|LIBSSH2_SFTP_S_IXGRP|
                    LIBSSH2_SFTP_S_IROTH|LIBSSH2_SFTP_S_IXOTH); break;
        case 3: rc = libssh2_sftp_rmdir(ctx->sftp, d->path.c_str()); break;
    }
    if (rc != 0) { const char *ops[]={"","delete","mkdir","rmdir"};
        d->errorMsg = std::string("SFTP ")+ops[d->opType]+" failed: "+GetSessionError(ctx->session); return; }
    d->success = true;
}
static napi_value SftpPathOp(napi_env env, napi_callback_info info, int op, const char* n) {
    size_t argc = 2; napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "requires handle, path");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->opType = op; d->path = NapiGetString(env, args[1]);
    return NapiCreatePromiseWork(env, d, n, SftpPathOpExec, SftpSimpleComplete, &d->deferred, &d->work);
}
napi_value NapiSftpDelete(napi_env e, napi_callback_info i) { return SftpPathOp(e, i, 1, "sftp_delete"); }
napi_value NapiSftpMkdir(napi_env e, napi_callback_info i)  { return SftpPathOp(e, i, 2, "sftp_mkdir"); }
napi_value NapiSftpRmdir(napi_env e, napi_callback_info i)  { return SftpPathOp(e, i, 3, "sftp_rmdir"); }

// --- sftpRename ---
static void SftpRenameExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    int rc = libssh2_sftp_rename(ctx->sftp, d->path.c_str(), d->path2.c_str());
    if (rc != 0) { d->errorMsg = "SFTP rename failed: " + GetSessionError(ctx->session); return; }
    d->success = true;
}
napi_value NapiSftpRename(napi_env env, napi_callback_info info) {
    size_t argc = 3; napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "sftpRename requires handle, src, dst");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->path = NapiGetString(env, args[1]); d->path2 = NapiGetString(env, args[2]);
    return NapiCreatePromiseWork(env, d, "sftp_rename", SftpRenameExec, SftpSimpleComplete, &d->deferred, &d->work);
}

// --- sftpStat ---
static void SftpStatExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    LIBSSH2_SFTP_ATTRIBUTES a;
    int rc = libssh2_sftp_stat(ctx->sftp, d->path.c_str(), &a);
    if (rc != 0) { d->errorMsg = "SFTP stat failed: " + GetSessionError(ctx->session); return; }
    bool isDir = (a.flags&LIBSSH2_SFTP_ATTR_PERMISSIONS) && LIBSSH2_SFTP_S_ISDIR(a.permissions);
    unsigned long sz = (a.flags&LIBSSH2_SFTP_ATTR_SIZE)?(unsigned long)a.filesize:0;
    unsigned long pm = (a.flags&LIBSSH2_SFTP_ATTR_PERMISSIONS)?a.permissions:0;
    unsigned long mt = (a.flags&LIBSSH2_SFTP_ATTR_ACMODTIME)?a.mtime:0;
    std::string esc; for (char c:d->path) { if(c=='"') esc+="\\\""; else if(c=='\\') esc+="\\\\"; else esc+=c; }
    d->result = "{\"name\":\""+esc+"\",\"size\":"+std::to_string(sz)
        +",\"permissions\":"+std::to_string(pm)+",\"mtime\":"+std::to_string(mt)
        +",\"isDir\":"+(isDir?"true":"false")+"}";
    d->success = true;
}
napi_value NapiSftpStat(napi_env env, napi_callback_info info) {
    size_t argc = 2; napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "sftpStat requires handle, path");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->path = NapiGetString(env, args[1]);
    return NapiCreatePromiseWork(env, d, "sftp_stat", SftpStatExec, SftpSimpleComplete, &d->deferred, &d->work);
}

// --- sftpChmod ---
static void SftpChmodExec(napi_env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    auto *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    LIBSSH2_SFTP_ATTRIBUTES a; memset(&a, 0, sizeof(a));
    a.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    a.permissions = (unsigned long)std::stoul(d->path2);
    int rc = libssh2_sftp_setstat(ctx->sftp, d->path.c_str(), &a);
    if (rc != 0) { d->errorMsg = "SFTP chmod failed: " + GetSessionError(ctx->session); return; }
    d->success = true;
}
napi_value NapiSftpChmod(napi_env env, napi_callback_info info) {
    size_t argc = 3; napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "sftpChmod requires handle, path, mode");
    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }
    d->path = NapiGetString(env, args[1]);
    int32_t mode = 0; napi_get_value_int32(env, args[2], &mode);
    d->path2 = std::to_string((unsigned long)mode);
    return NapiCreatePromiseWork(env, d, "sftp_chmod", SftpChmodExec, SftpSimpleComplete, &d->deferred, &d->work);
}
