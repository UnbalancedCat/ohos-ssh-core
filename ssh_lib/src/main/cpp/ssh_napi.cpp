/**
 * ssh_napi.cpp — N-API bridge for libssh2 SSH operations on HarmonyOS.
 *
 * Exports: createContext, connect, execCommand, startShell,
 *          writeToShell, resizePty, disconnect
 */

#include <cstring>
#include <cstdlib>
#include <string>
#include <cerrno>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>

#include <napi/native_api.h>
#include <hilog/log.h>
#include <libssh2.h>

#include "SshContext.h"

// ---------------------------------------------------------------------------
// HiLog helper
// ---------------------------------------------------------------------------
#undef LOG_TAG
#define LOG_TAG "SshCore"
#undef LOG_DOMAIN
#define LOG_DOMAIN 0x0201

#define SSH_LOGI(...) OH_LOG_INFO(LOG_APP, __VA_ARGS__)
#define SSH_LOGE(...) OH_LOG_ERROR(LOG_APP, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Utility: unwrap SshContext* from JS handle
// ---------------------------------------------------------------------------
static SshContext *UnwrapContext(napi_env env, napi_value jsHandle) {
    void *data = nullptr;
    napi_unwrap(env, jsHandle, &data);
    return static_cast<SshContext *>(data);
}

// ---------------------------------------------------------------------------
// Utility: throw JS Error
// ---------------------------------------------------------------------------
static napi_value ThrowError(napi_env env, const char *msg) {
    napi_throw_error(env, nullptr, msg);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Utility: get libssh2 session error message
// ---------------------------------------------------------------------------
static std::string GetSessionError(LIBSSH2_SESSION *session) {
    char *errmsg = nullptr;
    int errlen = 0;
    libssh2_session_last_error(session, &errmsg, &errlen, 0);
    if (errmsg && errlen > 0) {
        return std::string(errmsg, errlen);
    }
    return "Unknown libssh2 error";
}

// ===================================================================
// createContext()
// ===================================================================
static napi_value CreateContext(napi_env env, napi_callback_info info) {
    napi_value jsHandle;
    napi_create_object(env, &jsHandle);

    SshContext *ctx = new SshContext();

    // Wrap C++ pointer onto JS object; GC finalizer will clean up
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
// connect(handle, host, port, username, password) -> Promise<void>
// ===================================================================

struct ConnectData {
    // Input
    SshContext *ctx;
    std::string host;
    int port;
    std::string username;
    std::string password;
    std::string publicKey;
    std::string privateKey;
    std::string passphrase;
    bool isKeyAuth = false;

    // Output
    bool success = false;
    std::string errorMsg;

    // N-API
    napi_deferred deferred;
    napi_async_work work;
};

static void ConnectExecute(napi_env env, void *data) {
    auto *d = static_cast<ConnectData *>(data);
    SshContext *ctx = d->ctx;

    ctx->state = SshState::CONNECTING;

    // 1. DNS resolve + TCP connect
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string portStr = std::to_string(d->port);
    int rc = getaddrinfo(d->host.c_str(), portStr.c_str(), &hints, &res);
    if (rc != 0 || !res) {
        d->errorMsg = std::string("DNS resolve failed: ") + gai_strerror(rc);
        ctx->state = SshState::ERROR;
        return;
    }

    ctx->sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (ctx->sockfd < 0) {
        d->errorMsg = std::string("socket() failed: ") + strerror(errno);
        freeaddrinfo(res);
        ctx->state = SshState::ERROR;
        return;
    }

    rc = ::connect(ctx->sockfd, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);
    if (rc != 0) {
        d->errorMsg = std::string("TCP connect failed: ") + strerror(errno);
        close(ctx->sockfd);
        ctx->sockfd = -1;
        ctx->state = SshState::ERROR;
        return;
    }

    SSH_LOGI("TCP connected to %{public}s:%{public}d", d->host.c_str(), d->port);

    // 2. libssh2 session init + handshake
    ctx->session = libssh2_session_init();
    if (!ctx->session) {
        d->errorMsg = "libssh2_session_init() failed";
        close(ctx->sockfd);
        ctx->sockfd = -1;
        ctx->state = SshState::ERROR;
        return;
    }

    // Blocking mode for handshake
    libssh2_session_set_blocking(ctx->session, 1);

    rc = libssh2_session_handshake(ctx->session, ctx->sockfd);
    if (rc != 0) {
        d->errorMsg = "SSH handshake failed: " + GetSessionError(ctx->session);
        libssh2_session_free(ctx->session);
        ctx->session = nullptr;
        close(ctx->sockfd);
        ctx->sockfd = -1;
        ctx->state = SshState::ERROR;
        return;
    }

    SSH_LOGI("SSH handshake complete");

    // 3. Authentication
    if (d->isKeyAuth) {
        // Authenticate with public key from memory
        rc = libssh2_userauth_publickey_frommemory(
            ctx->session,
            d->username.c_str(),
            d->username.length(),
            d->publicKey.empty() ? nullptr : d->publicKey.c_str(),
            d->publicKey.length(),
            d->privateKey.c_str(),
            d->privateKey.length(),
            d->passphrase.empty() ? nullptr : d->passphrase.c_str()
        );
        if (rc != 0) {
            d->errorMsg = "Key authentication failed: " + GetSessionError(ctx->session);
            libssh2_session_disconnect(ctx->session, "Auth failed");
            libssh2_session_free(ctx->session);
            ctx->session = nullptr;
            close(ctx->sockfd);
            ctx->sockfd = -1;
            ctx->state = SshState::ERROR;
            return;
        }
    } else {
        // Password authentication
        rc = libssh2_userauth_password(ctx->session,
                                       d->username.c_str(),
                                       d->password.c_str());
        if (rc != 0) {
            d->errorMsg = "Password authentication failed: " + GetSessionError(ctx->session);
            libssh2_session_disconnect(ctx->session, "Auth failed");
            libssh2_session_free(ctx->session);
            ctx->session = nullptr;
            close(ctx->sockfd);
            ctx->sockfd = -1;
            ctx->state = SshState::ERROR;
            return;
        }
    }

    SSH_LOGI("SSH authenticated as %{public}s", d->username.c_str());
    ctx->state = SshState::CONNECTED;
    d->success = true;
}

static void ConnectComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<ConnectData *>(data);

    if (d->success) {
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        napi_resolve_deferred(env, d->deferred, undefined);
    } else {
        napi_value errMsg;
        napi_create_string_utf8(env, d->errorMsg.c_str(), NAPI_AUTO_LENGTH, &errMsg);
        napi_value error;
        napi_create_error(env, nullptr, errMsg, &error);
        napi_reject_deferred(env, d->deferred, error);
    }

    napi_delete_async_work(env, d->work);
    delete d;
}

static napi_value Connect(napi_env env, napi_callback_info info) {
    size_t argc = 5;
    napi_value args[5];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 5) {
        return ThrowError(env, "connect requires 5 arguments: handle, host, port, username, password");
    }

    auto *d = new ConnectData();
    d->isKeyAuth = false;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) {
        delete d;
        return ThrowError(env, "Invalid SSH handle");
    }

    // Extract host
    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->host.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->host[0], strLen + 1, &strLen);

    // Extract port
    int32_t port = 22;
    napi_get_value_int32(env, args[2], &port);
    d->port = port;

    // Extract username
    napi_get_value_string_utf8(env, args[3], nullptr, 0, &strLen);
    d->username.resize(strLen);
    napi_get_value_string_utf8(env, args[3], &d->username[0], strLen + 1, &strLen);

    // Extract password
    napi_get_value_string_utf8(env, args[4], nullptr, 0, &strLen);
    d->password.resize(strLen);
    napi_get_value_string_utf8(env, args[4], &d->password[0], strLen + 1, &strLen);

    // Create Promise
    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);

    // Create async work
    napi_value workName;
    napi_create_string_utf8(env, "ssh_connect", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName,
                           ConnectExecute, ConnectComplete, d, &d->work);
    napi_queue_async_work(env, d->work);

    return promise;
}

static napi_value ConnectWithKey(napi_env env, napi_callback_info info) {
    size_t argc = 6;
    napi_value args[6];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 6) {
        return ThrowError(env, "connectWithKey requires at least 6 arguments: handle, host, port, username, publicKey, privateKey, [passphrase]");
    }

    auto *d = new ConnectData();
    d->isKeyAuth = true;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) {
        delete d;
        return ThrowError(env, "Invalid SSH handle");
    }

    size_t strLen = 0;
    // Extract host
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->host.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->host[0], strLen + 1, &strLen);

    // Extract port
    int32_t port = 22;
    napi_get_value_int32(env, args[2], &port);
    d->port = port;

    // Extract username
    napi_get_value_string_utf8(env, args[3], nullptr, 0, &strLen);
    d->username.resize(strLen);
    napi_get_value_string_utf8(env, args[3], &d->username[0], strLen + 1, &strLen);

    // Extract publicKey
    napi_get_value_string_utf8(env, args[4], nullptr, 0, &strLen);
    d->publicKey.resize(strLen);
    napi_get_value_string_utf8(env, args[4], &d->publicKey[0], strLen + 1, &strLen);

    // Extract privateKey
    napi_get_value_string_utf8(env, args[5], nullptr, 0, &strLen);
    d->privateKey.resize(strLen);
    napi_get_value_string_utf8(env, args[5], &d->privateKey[0], strLen + 1, &strLen);

    // Extract passphrase (optional)
    if (argc >= 7) {
        napi_valuetype type;
        napi_typeof(env, args[6], &type);
        if (type == napi_string) {
            napi_get_value_string_utf8(env, args[6], nullptr, 0, &strLen);
            d->passphrase.resize(strLen);
            napi_get_value_string_utf8(env, args[6], &d->passphrase[0], strLen + 1, &strLen);
        }
    }

    // Create Promise
    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);

    // Create async work
    napi_value workName;
    napi_create_string_utf8(env, "ssh_connect_key", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName,
                           ConnectExecute, ConnectComplete, d, &d->work);
    napi_queue_async_work(env, d->work);

    return promise;
}

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

    // Ensure blocking mode for exec
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

    // Read all output
    char buf[4096];
    for (;;) {
        ssize_t n = libssh2_channel_read(ch, buf, sizeof(buf));
        if (n > 0) {
            d->output.append(buf, n);
        } else if (n == 0) {
            break;
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            continue;
        } else {
            break;  // error or EOF
        }
    }

    // Also read stderr
    for (;;) {
        ssize_t n = libssh2_channel_read_stderr(ch, buf, sizeof(buf));
        if (n > 0) {
            d->output.append(buf, n);
        } else if (n == 0) {
            break;
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            continue;
        } else {
            break;
        }
    }

    libssh2_channel_close(ch);
    libssh2_channel_wait_closed(ch);
    libssh2_channel_free(ch);
    d->success = true;
}

static void ExecComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<ExecData *>(data);

    if (d->success) {
        napi_value result;
        napi_create_string_utf8(env, d->output.c_str(), d->output.size(), &result);
        napi_resolve_deferred(env, d->deferred, result);
    } else {
        napi_value errMsg;
        napi_create_string_utf8(env, d->errorMsg.c_str(), NAPI_AUTO_LENGTH, &errMsg);
        napi_value error;
        napi_create_error(env, nullptr, errMsg, &error);
        napi_reject_deferred(env, d->deferred, error);
    }

    napi_delete_async_work(env, d->work);
    delete d;
}

static napi_value ExecCommand(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) {
        return ThrowError(env, "execCommand requires 2 arguments: handle, command");
    }

    auto *d = new ExecData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) {
        delete d;
        return ThrowError(env, "Invalid SSH handle");
    }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->command.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->command[0], strLen + 1, &strLen);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);

    napi_value workName;
    napi_create_string_utf8(env, "ssh_exec", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName,
                           ExecExecute, ExecComplete, d, &d->work);
    napi_queue_async_work(env, d->work);

    return promise;
}

// ===================================================================
// startShell(handle, onData, cols, rows) -> Promise<void>
// ===================================================================

// Called on main thread when tsfn delivers data from read thread
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

// Background thread: continuously reads from channel
static void ShellReadLoop(SshContext *ctx) {
    SSH_LOGI("Shell read thread started");
    char buf[4096];

    while (!ctx->stopReading) {
        ssize_t n = libssh2_channel_read(ctx->channel, buf, sizeof(buf));
        if (n > 0) {
            // Allocate a copy for the tsfn (freed in CallJS)
            char *copy = static_cast<char *>(malloc(n + 1));
            if (copy) {
                memcpy(copy, buf, n);
                copy[n] = '\0';
                napi_call_threadsafe_function(ctx->tsfn, copy, napi_tsfn_blocking);
            }
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            // No data yet, wait on socket
            struct pollfd pfd;
            pfd.fd = ctx->sockfd;
            pfd.events = POLLIN;
            poll(&pfd, 1, 50);  // 50ms timeout
        } else if (n == 0 || libssh2_channel_eof(ctx->channel)) {
            SSH_LOGI("Shell channel EOF");
            break;
        } else {
            SSH_LOGE("Shell read error: %{public}zd", n);
            break;
        }
    }

    SSH_LOGI("Shell read thread exiting");

    // Release tsfn from this thread
    if (ctx->tsfn) {
        napi_release_threadsafe_function(ctx->tsfn, napi_tsfn_release);
        ctx->tsfn = nullptr;
    }
}

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

    // Blocking mode for channel setup
    libssh2_session_set_blocking(ctx->session, 1);

    // Open channel
    ctx->channel = libssh2_channel_open_session(ctx->session);
    if (!ctx->channel) {
        d->errorMsg = "Failed to open channel: " + GetSessionError(ctx->session);
        return;
    }

    // Request PTY
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

    // Start shell
    rc = libssh2_channel_shell(ctx->channel);
    if (rc != 0) {
        d->errorMsg = "Failed to start shell: " + GetSessionError(ctx->session);
        libssh2_channel_free(ctx->channel);
        ctx->channel = nullptr;
        return;
    }

    // Switch to non-blocking for the read loop
    libssh2_session_set_blocking(ctx->session, 0);

    d->success = true;
}

static void ShellComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<ShellData *>(data);

    if (d->success) {
        // Save tsfn and start read thread
        d->ctx->tsfn = d->tsfn;
        d->ctx->stopReading = false;
        d->ctx->readThread = std::thread(ShellReadLoop, d->ctx);

        napi_value undefined;
        napi_get_undefined(env, &undefined);
        napi_resolve_deferred(env, d->deferred, undefined);
    } else {
        // Release tsfn since we won't use it
        napi_release_threadsafe_function(d->tsfn, napi_tsfn_abort);

        napi_value errMsg;
        napi_create_string_utf8(env, d->errorMsg.c_str(), NAPI_AUTO_LENGTH, &errMsg);
        napi_value error;
        napi_create_error(env, nullptr, errMsg, &error);
        napi_reject_deferred(env, d->deferred, error);
    }

    napi_delete_async_work(env, d->work);
    delete d;
}

static napi_value StartShell(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 4) {
        return ThrowError(env, "startShell requires 4 arguments: handle, onData, cols, rows");
    }

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (!ctx) {
        return ThrowError(env, "Invalid SSH handle");
    }

    // Create threadsafe function from onData callback
    napi_value tsfnName;
    napi_create_string_utf8(env, "shellOnData", NAPI_AUTO_LENGTH, &tsfnName);

    napi_threadsafe_function tsfn;
    napi_status nstatus = napi_create_threadsafe_function(
        env,
        args[1],          // JS callback (onData)
        nullptr,          // async resource
        tsfnName,         // async resource name
        0,                // max queue size (0 = unlimited)
        1,                // initial thread count
        nullptr,          // thread finalize data
        nullptr,          // thread finalize callback
        nullptr,          // context
        ShellDataCallJS,  // call JS callback
        &tsfn
    );
    if (nstatus != napi_ok) {
        return ThrowError(env, "Failed to create threadsafe function");
    }

    auto *d = new ShellData();
    d->ctx = ctx;
    d->tsfn = tsfn;

    int32_t cols = 80, rows = 24;
    napi_get_value_int32(env, args[2], &cols);
    napi_get_value_int32(env, args[3], &rows);
    d->cols = cols;
    d->rows = rows;

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);

    napi_value workName;
    napi_create_string_utf8(env, "ssh_shell", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName,
                           ShellExecute, ShellComplete, d, &d->work);
    napi_queue_async_work(env, d->work);

    return promise;
}

// ===================================================================
// writeToShell(handle, data)
// ===================================================================
static napi_value WriteToShell(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 2) {
        return ThrowError(env, "writeToShell requires 2 arguments: handle, data");
    }

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (!ctx || !ctx->channel) {
        return ThrowError(env, "Invalid handle or shell not started");
    }

    // Get string data
    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    if (strLen == 0) {
        napi_value undefined;
        napi_get_undefined(env, &undefined);
        return undefined;
    }

    std::string data(strLen, '\0');
    napi_get_value_string_utf8(env, args[1], &data[0], strLen + 1, &strLen);

    // Write to channel, handling partial writes and EAGAIN
    const char *ptr = data.c_str();
    size_t remaining = strLen;

    while (remaining > 0) {
        ssize_t n = libssh2_channel_write(ctx->channel, ptr, remaining);
        if (n > 0) {
            ptr += n;
            remaining -= n;
        } else if (n == LIBSSH2_ERROR_EAGAIN) {
            // Wait briefly for socket to be writable
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
static napi_value ResizePty(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 3) {
        return ThrowError(env, "resizePty requires 3 arguments: handle, cols, rows");
    }

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (!ctx || !ctx->channel) {
        return ThrowError(env, "Invalid handle or shell not started");
    }

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

// ===================================================================
// disconnect(handle)
// ===================================================================
static napi_value Disconnect(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) {
        return ThrowError(env, "disconnect requires 1 argument: handle");
    }

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (ctx) {
        SSH_LOGI("disconnect: cleaning up");
        ctx->cleanup();
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}

// ===================================================================
// Module registration
// ===================================================================
napi_value RegisterSshNapi(napi_env env, napi_value exports) {
    // Initialize libssh2 globally (once)
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
        {"connect",        nullptr, Connect,        nullptr, nullptr, nullptr, napi_default, nullptr},
        {"connectWithKey", nullptr, ConnectWithKey, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"execCommand",    nullptr, ExecCommand,    nullptr, nullptr, nullptr, napi_default, nullptr},
        {"startShell",     nullptr, StartShell,     nullptr, nullptr, nullptr, napi_default, nullptr},
        {"writeToShell",   nullptr, WriteToShell,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"resizePty",      nullptr, ResizePty,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"disconnect",     nullptr, Disconnect,     nullptr, nullptr, nullptr, napi_default, nullptr},
    };

    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    SSH_LOGI("ssh_core N-API module registered");
    return exports;
}
