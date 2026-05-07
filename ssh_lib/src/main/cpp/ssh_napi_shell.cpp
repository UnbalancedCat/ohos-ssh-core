/**
 * ssh_napi_shell.cpp — StartShell, WriteToShell, ResizePty
 */

#include <poll.h>
#include <cstdlib>
#include <thread>

#include "napi_helpers.h"

// ===================================================================
// Shell data callback (called on main thread via tsfn)
// ===================================================================
static void ShellDataCallJS(napi_env env, napi_value jsCb, void * /*context*/, void *data) {
    if (!env || !data) return;

    char *str = static_cast<char *>(data);
    napi_value argv;
    napi_create_string_utf8(env, str, NAPI_AUTO_LENGTH, &argv);

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    napi_call_function(env, undefined, jsCb, 1, &argv, nullptr);

    free(str);
}

// ===================================================================
// Background read loop for interactive shell
// ===================================================================
static void ShellReadLoop(SshContext *ctx) {
    SSH_LOGI("Shell read thread started");
    char buf[4096];

    while (!ctx->stopReading) {
        ssize_t n = libssh2_channel_read(ctx->channel, buf, sizeof(buf));
        if (n > 0) {
            char *copy = static_cast<char *>(malloc(n + 1));
            if (copy) {
                memcpy(copy, buf, n);
                copy[n] = '\0';
                napi_call_threadsafe_function(ctx->tsfn, copy, napi_tsfn_blocking);
            }
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            struct pollfd pfd;
            pfd.fd = ctx->sockfd;
            pfd.events = POLLIN;
            poll(&pfd, 1, 50);
        } else if (n == 0 || libssh2_channel_eof(ctx->channel)) {
            SSH_LOGI("Shell channel EOF");
            break;
        } else {
            SSH_LOGE("Shell read error: %{public}zd", n);
            break;
        }
    }

    SSH_LOGI("Shell read thread exiting");

    if (ctx->tsfn) {
        napi_release_threadsafe_function(ctx->tsfn, napi_tsfn_release);
        ctx->tsfn = nullptr;
    }
}

// ===================================================================
// startShell(handle, onData, cols, rows) -> Promise<void>
// ===================================================================

struct ShellData {
    SshContext *ctx;
    int cols;
    int rows;
    bool success = false;
    std::string errorMsg;
    napi_deferred deferred;
    napi_async_work work;
    napi_threadsafe_function tsfn;
};

static void ShellExecute(napi_env env, void *data) {
    auto *d = static_cast<ShellData *>(data);
    SshContext *ctx = d->ctx;

    if (ctx->state != SshState::CONNECTED) {
        d->errorMsg = "Not connected";
        return;
    }

    libssh2_session_set_blocking(ctx->session, 1);

    ctx->channel = libssh2_channel_open_session(ctx->session);
    if (!ctx->channel) {
        d->errorMsg = "Failed to open channel: " + GetSessionError(ctx->session);
        return;
    }

    int rc = libssh2_channel_request_pty_ex(ctx->channel,
                                            "xterm", 5,
                                            nullptr, 0,
                                            d->cols, d->rows, 0, 0);
    if (rc != 0) {
        d->errorMsg = "Failed to request PTY: " + GetSessionError(ctx->session);
        libssh2_channel_free(ctx->channel);
        ctx->channel = nullptr;
        return;
    }

    rc = libssh2_channel_shell(ctx->channel);
    if (rc != 0) {
        d->errorMsg = "Failed to start shell: " + GetSessionError(ctx->session);
        libssh2_channel_free(ctx->channel);
        ctx->channel = nullptr;
        return;
    }

    libssh2_session_set_blocking(ctx->session, 0);
    d->success = true;
}

static void ShellComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<ShellData *>(data);

    if (d->success) {
        d->ctx->tsfn = d->tsfn;
        d->ctx->stopReading = false;
        d->ctx->readThread = std::thread(ShellReadLoop, d->ctx);
        NapiResolveUndefined(env, d->deferred);
    } else {
        napi_release_threadsafe_function(d->tsfn, napi_tsfn_abort);
        NapiRejectWithError(env, d->deferred, d->errorMsg);
    }

    napi_delete_async_work(env, d->work);
    delete d;
}

napi_value NapiStartShell(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 4) return ThrowError(env, "startShell requires 4 arguments: handle, onData, cols, rows");

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (!ctx) return ThrowError(env, "Invalid SSH handle");

    // Create threadsafe function from onData callback
    napi_value tsfnName;
    napi_create_string_utf8(env, "shellOnData", NAPI_AUTO_LENGTH, &tsfnName);

    napi_threadsafe_function tsfn;
    napi_status nstatus = napi_create_threadsafe_function(
        env, args[1], nullptr, tsfnName,
        0, 1, nullptr, nullptr, nullptr,
        ShellDataCallJS, &tsfn
    );
    if (nstatus != napi_ok) return ThrowError(env, "Failed to create threadsafe function");

    auto *d = new ShellData();
    d->ctx = ctx;
    d->tsfn = tsfn;

    int32_t cols = 80, rows = 24;
    napi_get_value_int32(env, args[2], &cols);
    napi_get_value_int32(env, args[3], &rows);
    d->cols = cols;
    d->rows = rows;

    return NapiCreatePromiseWork(env, d, "ssh_shell",
                                 ShellExecute, ShellComplete,
                                 &d->deferred, &d->work);
}

// ===================================================================
// writeToShell(handle, data)
// ===================================================================
napi_value NapiWriteToShell(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) return ThrowError(env, "writeToShell requires 2 arguments: handle, data");

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (!ctx || !ctx->channel) return ThrowError(env, "Invalid handle or shell not started");

    std::string data = NapiGetString(env, args[1]);
    if (data.empty()) {
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    const char *ptr = data.c_str();
    size_t remaining = data.size();

    while (remaining > 0) {
        ssize_t n = libssh2_channel_write(ctx->channel, ptr, remaining);
        if (n > 0) {
            ptr += n;
            remaining -= n;
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            struct pollfd pfd;
            pfd.fd = ctx->sockfd;
            pfd.events = POLLOUT;
            poll(&pfd, 1, 10);
        } else {
            SSH_LOGE("writeToShell error: %{public}zd", n);
            return ThrowError(env, "Write to shell failed");
        }
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

// ===================================================================
// resizePty(handle, cols, rows)
// ===================================================================
napi_value NapiResizePty(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 3) return ThrowError(env, "resizePty requires 3 arguments: handle, cols, rows");

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (!ctx || !ctx->channel) return ThrowError(env, "Invalid handle or shell not started");

    int32_t cols = 80, rows = 24;
    napi_get_value_int32(env, args[1], &cols);
    napi_get_value_int32(env, args[2], &rows);

    int rc = libssh2_channel_request_pty_size(ctx->channel, cols, rows);
    if (rc != 0 && rc != LIBSSH2_ERROR_EAGAIN) {
        SSH_LOGE("resizePty failed: %{public}d", rc);
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}
