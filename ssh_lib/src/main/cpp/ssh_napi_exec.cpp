/**
 * ssh_napi_exec.cpp — execCommand (single-shot command execution)
 */

#include "napi_helpers.h"

// ===================================================================
// execCommand(handle, command) -> Promise<string>
// ===================================================================

struct ExecData {
    SshContext *ctx;
    std::string command;
    bool success = false;
    std::string output;
    std::string errorMsg;
    napi_deferred deferred;
    napi_async_work work;
};

static void ExecExecute(napi_env env, void *data) {
    auto *d = static_cast<ExecData *>(data);
    SshContext *ctx = d->ctx;

    if (ctx->state != SshState::CONNECTED) {
        d->errorMsg = "Not connected";
        return;
    }

    libssh2_session_set_blocking(ctx->session, 1);

    LIBSSH2_CHANNEL *ch = libssh2_channel_open_session(ctx->session);
    if (!ch) {
        d->errorMsg = "Failed to open channel: " + GetSessionError(ctx->session);
        return;
    }

    int rc = libssh2_channel_exec(ch, d->command.c_str());
    if (rc != 0) {
        d->errorMsg = "Failed to exec command: " + GetSessionError(ctx->session);
        libssh2_channel_free(ch);
        return;
    }

    // Read stdout
    char buf[4096];
    for (;;) {
        ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
        if (n > 0) d->output.append(buf, n);
        else if (n == 0) break;
        else if (n == LIBSSH2_ERROR_EAGAIN) continue;
        else break;
    }

    // Read stderr
    for (;;) {
        ssize_t n = libssh2_channel_read_stderr(ch, buf, sizeof(buf));
        if (n > 0) d->output.append(buf, n);
        else if (n == 0) break;
        else if (n == LIBSSH2_ERROR_EAGAIN) continue;
        else break;
    }

    libssh2_channel_close(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);
    d->success = true;
}

static void ExecComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<ExecData *>(data);

    if (d->success) {
        NapiResolveString(env, d->deferred, d->output);
    } else {
        NapiRejectWithError(env, d->deferred, d->errorMsg);
    }

    napi_delete_async_work(env, d->work);
    delete d;
}

napi_value NapiExecCommand(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) return ThrowError(env, "execCommand requires 2 arguments: handle, command");

    auto *d = new ExecData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    d->command = NapiGetString(env, args[1]);

    return NapiCreatePromiseWork(env, d, "ssh_exec",
                                 ExecExecute, ExecComplete,
                                 &d->deferred, &d->work);
}
