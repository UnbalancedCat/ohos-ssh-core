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
    int timeoutSec = 10;
    int keepaliveInterval = 30;

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

    // Set socket timeout
    if (d->timeoutSec > 0) {
        struct timeval tv;
        tv.tv_sec = d->timeoutSec;
        tv.tv_usec = 0;
        setsockopt(ctx->sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(ctx->sockfd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
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
            nullptr,  // OpenSSL derives public key from private key
            0,
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

    // Configure keepalive
    if (d->keepaliveInterval > 0) {
        libssh2_keepalive_config(ctx->session, 1, d->keepaliveInterval);
        SSH_LOGI("Keepalive configured: interval=%{public}d sec", d->keepaliveInterval);
    }

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
    size_t argc = 7;
    napi_value args[7];
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

    // Extract optional timeoutSec (arg 5)
    if (argc >= 6) {
        napi_valuetype type;
        napi_typeof(env, args[5], &type);
        if (type == napi_number) {
            int32_t val = 10;
            napi_get_value_int32(env, args[5], &val);
            d->timeoutSec = val;
        }
    }
    // Extract optional keepaliveInterval (arg 6)
    if (argc >= 7) {
        napi_valuetype type;
        napi_typeof(env, args[6], &type);
        if (type == napi_number) {
            int32_t val = 30;
            napi_get_value_int32(env, args[6], &val);
            d->keepaliveInterval = val;
        }
    }

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
    size_t argc = 8;
    napi_value args[8];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 5) {
        return ThrowError(env, "connectWithKey requires at least 5 arguments: handle, host, port, username, privateKey, [passphrase]");
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

    // Extract privateKey
    napi_get_value_string_utf8(env, args[4], nullptr, 0, &strLen);
    d->privateKey.resize(strLen);
    napi_get_value_string_utf8(env, args[4], &d->privateKey[0], strLen + 1, &strLen);

    // Extract passphrase (optional)
    if (argc >= 6) {
        napi_valuetype type;
        napi_typeof(env, args[5], &type);
        if (type == napi_string) {
            napi_get_value_string_utf8(env, args[5], nullptr, 0, &strLen);
            d->passphrase.resize(strLen);
            napi_get_value_string_utf8(env, args[5], &d->passphrase[0], strLen + 1, &strLen);
        }
    }

    // Extract optional timeoutSec
    if (argc >= 7) {
        napi_valuetype type;
        napi_typeof(env, args[6], &type);
        if (type == napi_number) {
            int32_t val = 10;
            napi_get_value_int32(env, args[6], &val);
            d->timeoutSec = val;
        }
    }
    // Extract optional keepaliveInterval
    if (argc >= 8) {
        napi_valuetype type;
        napi_typeof(env, args[7], &type);
        if (type == napi_number) {
            int32_t val = 30;
            napi_get_value_int32(env, args[7], &val);
            d->keepaliveInterval = val;
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
// SFTP Operations
// ===================================================================

// --- sftpInit(handle) -> Promise<void> ---
struct SftpSimpleData {
    SshContext *ctx;
    bool success = false;
    std::string errorMsg;
    std::string result; // For operations that return data
    std::string path;
    std::string path2; // For rename (dest)
    std::string data;  // For write
    int opType = 0;    // Operation type discriminator
    napi_deferred deferred;
    napi_async_work work;
};

static void SftpInitExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (ctx->state != SshState::CONNECTED) { d->errorMsg = "Not connected"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    ctx->sftp = libssh2_sftp_init(ctx->session);
    if (!ctx->sftp) {
        d->errorMsg = "SFTP init failed: " + GetSessionError(ctx->session);
        return;
    }
    d->success = true;
}

static void SftpSimpleComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    if (d->success) {
        if (d->result.empty()) {
            napi_value undefined;
            napi_get_undefined(env, &undefined);
            napi_resolve_deferred(env, d->deferred, undefined);
        } else {
            napi_value result;
            napi_create_string_utf8(env, d->result.c_str(), d->result.size(), &result);
            napi_resolve_deferred(env, d->deferred, result);
        }
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

static napi_value SftpInit(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) return ThrowError(env, "sftpInit requires 1 argument: handle");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_init", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpInitExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- sftpShutdown(handle) -> Promise<void> ---
static void SftpShutdownExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);
    libssh2_sftp_shutdown(ctx->sftp);
    ctx->sftp = nullptr;
    d->success = true;
}

static napi_value SftpShutdown(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 1) return ThrowError(env, "sftpShutdown requires 1 argument: handle");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_shutdown", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpShutdownExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- sftpListDir(handle, path) -> Promise<string> (JSON array) ---
static void SftpListDirExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    LIBSSH2_SFTP_HANDLE *dirHandle = libssh2_sftp_opendir(ctx->sftp, d->path.c_str());
    if (!dirHandle) {
        d->errorMsg = "Failed to open directory: " + d->path;
        return;
    }

    std::string json = "[";
    char nameBuf[512];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    bool first = true;

    while (true) {
        int rc = libssh2_sftp_readdir(dirHandle, nameBuf, sizeof(nameBuf), &attrs);
        if (rc <= 0) break;

        std::string name(nameBuf, rc);
        if (name == "." || name == "..") continue;

        if (!first) json += ",";
        first = false;

        bool isDir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                     LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
        unsigned long size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (unsigned long)attrs.filesize : 0;
        unsigned long perms = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? attrs.permissions : 0;
        unsigned long mtime = (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? attrs.mtime : 0;

        // Escape quotes in filename
        std::string escaped;
        for (char c : name) {
            if (c == '"') escaped += "\\\"";
            else if (c == '\\') escaped += "\\\\";
            else escaped += c;
        }

        json += "{\"name\":\"" + escaped + "\""
             + ",\"size\":" + std::to_string(size)
             + ",\"permissions\":" + std::to_string(perms)
             + ",\"mtime\":" + std::to_string(mtime)
             + ",\"isDir\":" + (isDir ? "true" : "false")
             + "}";
    }

    json += "]";
    libssh2_sftp_closedir(dirHandle);
    d->result = json;
    d->success = true;
}

static napi_value SftpListDir(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "sftpListDir requires 2 arguments: handle, path");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_listdir", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpListDirExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- sftpReadFile(handle, remotePath) -> Promise<string> ---
static void SftpReadFileExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(ctx->sftp, d->path.c_str(),
                                                  LIBSSH2_FXF_READ, 0);
    if (!fh) {
        d->errorMsg = "Failed to open file: " + d->path;
        return;
    }

    char buf[8192];
    std::string content;
    for (;;) {
        ssize_t n = libssh2_sftp_read(fh, buf, sizeof(buf));
        if (n > 0) content.append(buf, n);
        else if (n == 0) break;
        else if (n == LIBSSH2_ERROR_EAGAIN) continue;
        else { d->errorMsg = "Read error"; libssh2_sftp_close(fh); return; }
    }

    libssh2_sftp_close(fh);
    d->result = content;
    d->success = true;
}

static napi_value SftpReadFile(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "sftpReadFile requires 2 arguments: handle, remotePath");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_readfile", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpReadFileExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- sftpWriteFile(handle, remotePath, data) -> Promise<void> ---
static void SftpWriteFileExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(ctx->sftp, d->path.c_str(),
        LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
        LIBSSH2_SFTP_S_IRUSR | LIBSSH2_SFTP_S_IWUSR |
        LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IROTH);
    if (!fh) {
        d->errorMsg = "Failed to open file for writing: " + d->path;
        return;
    }

    const char *ptr = d->data.c_str();
    size_t remaining = d->data.size();
    while (remaining > 0) {
        ssize_t n = libssh2_sftp_write(fh, ptr, remaining);
        if (n > 0) { ptr += n; remaining -= n; }
        else if (n == LIBSSH2_ERROR_EAGAIN) continue;
        else { d->errorMsg = "Write error"; libssh2_sftp_close(fh); return; }
    }

    libssh2_sftp_close(fh);
    d->success = true;
}

static napi_value SftpWriteFile(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "sftpWriteFile requires 3 arguments: handle, remotePath, data");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    napi_get_value_string_utf8(env, args[2], nullptr, 0, &strLen);
    d->data.resize(strLen);
    napi_get_value_string_utf8(env, args[2], &d->data[0], strLen + 1, &strLen);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_writefile", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpWriteFileExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- sftpDelete / sftpMkdir / sftpRmdir --- (share one executor with opType)
static void SftpPathOpExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    int rc = -1;
    switch (d->opType) {
        case 1: rc = libssh2_sftp_unlink(ctx->sftp, d->path.c_str()); break;
        case 2: rc = libssh2_sftp_mkdir(ctx->sftp, d->path.c_str(),
                    LIBSSH2_SFTP_S_IRWXU | LIBSSH2_SFTP_S_IRGRP | LIBSSH2_SFTP_S_IXGRP |
                    LIBSSH2_SFTP_S_IROTH | LIBSSH2_SFTP_S_IXOTH); break;
        case 3: rc = libssh2_sftp_rmdir(ctx->sftp, d->path.c_str()); break;
    }

    if (rc != 0) {
        const char *ops[] = {"", "delete", "mkdir", "rmdir"};
        d->errorMsg = std::string("SFTP ") + ops[d->opType] + " failed: " + GetSessionError(ctx->session);
        return;
    }
    d->success = true;
}

static napi_value SftpPathOp(napi_env env, napi_callback_info info, int opType, const char* opName) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "requires 2 arguments: handle, path");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }
    d->opType = opType;

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, opName, NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpPathOpExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

static napi_value SftpDelete(napi_env env, napi_callback_info info) { return SftpPathOp(env, info, 1, "sftp_delete"); }
static napi_value SftpMkdir(napi_env env, napi_callback_info info) { return SftpPathOp(env, info, 2, "sftp_mkdir"); }
static napi_value SftpRmdir(napi_env env, napi_callback_info info) { return SftpPathOp(env, info, 3, "sftp_rmdir"); }

// --- sftpRename(handle, src, dst) -> Promise<void> ---
static void SftpRenameExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    int rc = libssh2_sftp_rename(ctx->sftp, d->path.c_str(), d->path2.c_str());
    if (rc != 0) {
        d->errorMsg = "SFTP rename failed: " + GetSessionError(ctx->session);
        return;
    }
    d->success = true;
}

static napi_value SftpRename(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "sftpRename requires 3 arguments: handle, src, dst");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    napi_get_value_string_utf8(env, args[2], nullptr, 0, &strLen);
    d->path2.resize(strLen);
    napi_get_value_string_utf8(env, args[2], &d->path2[0], strLen + 1, &strLen);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_rename", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpRenameExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- sftpStat(handle, path) -> Promise<string> (JSON) ---
static void SftpStatExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = libssh2_sftp_stat(ctx->sftp, d->path.c_str(), &attrs);
    if (rc != 0) {
        d->errorMsg = "SFTP stat failed: " + GetSessionError(ctx->session);
        return;
    }

    bool isDir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) &&
                 LIBSSH2_SFTP_S_ISDIR(attrs.permissions);
    unsigned long size = (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (unsigned long)attrs.filesize : 0;
    unsigned long perms = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? attrs.permissions : 0;
    unsigned long mtime = (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? attrs.mtime : 0;

    // Escape path for JSON
    std::string escaped;
    for (char c : d->path) {
        if (c == '"') escaped += "\\\"";
        else if (c == '\\') escaped += "\\\\";
        else escaped += c;
    }

    d->result = "{\"name\":\"" + escaped + "\""
             + ",\"size\":" + std::to_string(size)
             + ",\"permissions\":" + std::to_string(perms)
             + ",\"mtime\":" + std::to_string(mtime)
             + ",\"isDir\":" + (isDir ? "true" : "false")
             + "}";
    d->success = true;
}

static napi_value SftpStat(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "sftpStat requires 2 arguments: handle, path");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_stat", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpStatExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- sftpChmod(handle, path, mode) -> Promise<void> ---
static void SftpChmodExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpSimpleData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    memset(&attrs, 0, sizeof(attrs));
    attrs.flags = LIBSSH2_SFTP_ATTR_PERMISSIONS;
    // Mode stored in path2 as string
    attrs.permissions = (unsigned long)std::stoul(d->path2);

    int rc = libssh2_sftp_setstat(ctx->sftp, d->path.c_str(), &attrs);
    if (rc != 0) {
        d->errorMsg = "SFTP chmod failed: " + GetSessionError(ctx->session);
        return;
    }
    d->success = true;
}

static napi_value SftpChmod(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "sftpChmod requires 3 arguments: handle, path, mode");

    auto *d = new SftpSimpleData();
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    // Read mode as int, store as string in path2
    int32_t mode = 0;
    napi_get_value_int32(env, args[2], &mode);
    d->path2 = std::to_string((unsigned long)mode);

    napi_value promise;
    napi_create_promise(env, &d->deferred, &promise);
    napi_value workName;
    napi_create_string_utf8(env, "sftp_chmod", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpChmodExecute, SftpSimpleComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// --- Binary & Streaming SFTP Operations ---
struct SftpBinaryData {
    SshContext *ctx;
    bool success = false;
    std::string errorMsg;
    int opType = 0; // 1: open, 2: read, 3: write, 4: close
    std::string path;
    int fd = -1;
    long flags = 0;
    long mode = 0;
    size_t size = 0;
    std::vector<uint8_t> buffer;
    napi_deferred deferred;
    napi_async_work work;
};

static void SftpBinaryExecute(napi_env env, void *data) {
    auto *d = static_cast<SftpBinaryData *>(data);
    SshContext *ctx = d->ctx;
    if (!ctx->sftp && d->opType != 4) { d->errorMsg = "SFTP not initialized"; return; }
    libssh2_session_set_blocking(ctx->session, 1);

    if (d->opType == 1) { // open
        LIBSSH2_SFTP_HANDLE *fh = libssh2_sftp_open(ctx->sftp, d->path.c_str(), d->flags, d->mode);
        if (!fh) {
            d->errorMsg = "Failed to open file";
            return;
        }
        d->fd = ctx->nextFd++;
        ctx->openFiles[d->fd] = fh;
        d->success = true;
    } else if (d->opType == 2) { // read
        auto it = ctx->openFiles.find(d->fd);
        if (it == ctx->openFiles.end()) { d->errorMsg = "Invalid fd"; return; }
        LIBSSH2_SFTP_HANDLE *fh = it->second;
        
        d->buffer.resize(d->size);
        ssize_t n = libssh2_sftp_read(fh, reinterpret_cast<char*>(d->buffer.data()), d->size);
        if (n < 0 && n != LIBSSH2_ERROR_EAGAIN) {
            d->errorMsg = "Read error";
            return;
        }
        if (n < 0) n = 0; // EAGAIN fallback though blocking
        d->buffer.resize(n);
        d->success = true;
    } else if (d->opType == 3) { // write
        auto it = ctx->openFiles.find(d->fd);
        if (it == ctx->openFiles.end()) { d->errorMsg = "Invalid fd"; return; }
        LIBSSH2_SFTP_HANDLE *fh = it->second;
        
        const char *ptr = reinterpret_cast<const char*>(d->buffer.data());
        size_t remaining = d->buffer.size();
        while (remaining > 0) {
            ssize_t n = libssh2_sftp_write(fh, ptr, remaining);
            if (n > 0) { ptr += n; remaining -= n; }
            else if (n == LIBSSH2_ERROR_EAGAIN) continue;
            else { d->errorMsg = "Write error"; return; }
        }
        d->success = true;
    } else if (d->opType == 4) { // close
        auto it = ctx->openFiles.find(d->fd);
        if (it == ctx->openFiles.end()) { d->errorMsg = "Invalid fd"; return; }
        LIBSSH2_SFTP_HANDLE *fh = it->second;
        libssh2_sftp_close(fh);
        ctx->openFiles.erase(it);
        d->success = true;
    }
}

static void SftpBinaryComplete(napi_env env, napi_status status, void *data) {
    auto *d = static_cast<SftpBinaryData *>(data);
    if (d->success) {
        napi_value result;
        if (d->opType == 1) { // open -> returns fd
            napi_create_int32(env, d->fd, &result);
            napi_resolve_deferred(env, d->deferred, result);
        } else if (d->opType == 2) { // read -> returns ArrayBuffer
            void* arrayData = nullptr;
            napi_create_arraybuffer(env, d->buffer.size(), &arrayData, &result);
            if (d->buffer.size() > 0) {
                memcpy(arrayData, d->buffer.data(), d->buffer.size());
            }
            napi_resolve_deferred(env, d->deferred, result);
        } else { // write, close -> returns void
            napi_get_undefined(env, &result);
            napi_resolve_deferred(env, d->deferred, result);
        }
    } else {
        napi_value errMsg, error;
        napi_create_string_utf8(env, d->errorMsg.c_str(), NAPI_AUTO_LENGTH, &errMsg);
        napi_create_error(env, nullptr, errMsg, &error);
        napi_reject_deferred(env, d->deferred, error);
    }
    napi_delete_async_work(env, d->work);
    delete d;
}

static napi_value SftpOpenFile(napi_env env, napi_callback_info info) {
    size_t argc = 4;
    napi_value args[4];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 4) return ThrowError(env, "Requires handle, path, flags, mode");

    auto *d = new SftpBinaryData();
    d->opType = 1;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }

    size_t strLen = 0;
    napi_get_value_string_utf8(env, args[1], nullptr, 0, &strLen);
    d->path.resize(strLen);
    napi_get_value_string_utf8(env, args[1], &d->path[0], strLen + 1, &strLen);

    napi_get_value_int32(env, args[2], (int32_t*)&d->flags);
    napi_get_value_int32(env, args[3], (int32_t*)&d->mode);

    napi_value promise, workName;
    napi_create_promise(env, &d->deferred, &promise);
    napi_create_string_utf8(env, "sftp_open", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpBinaryExecute, SftpBinaryComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

static napi_value SftpRead(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "Requires handle, fd, size");

    auto *d = new SftpBinaryData();
    d->opType = 2;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }

    napi_get_value_int32(env, args[1], &d->fd);
    int32_t size = 0;
    napi_get_value_int32(env, args[2], &size);
    d->size = size > 0 ? size : 0;

    napi_value promise, workName;
    napi_create_promise(env, &d->deferred, &promise);
    napi_create_string_utf8(env, "sftp_read", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpBinaryExecute, SftpBinaryComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

static napi_value SftpWrite(napi_env env, napi_callback_info info) {
    size_t argc = 3;
    napi_value args[3];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 3) return ThrowError(env, "Requires handle, fd, arraybuffer");

    auto *d = new SftpBinaryData();
    d->opType = 3;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }

    napi_get_value_int32(env, args[1], &d->fd);

    bool isArrayBuffer = false;
    napi_is_arraybuffer(env, args[2], &isArrayBuffer);
    if (isArrayBuffer) {
        void* dataPtr = nullptr;
        size_t byteLength = 0;
        napi_get_arraybuffer_info(env, args[2], &dataPtr, &byteLength);
        if (byteLength > 0 && dataPtr != nullptr) {
            d->buffer.assign((uint8_t*)dataPtr, (uint8_t*)dataPtr + byteLength);
        }
    }

    napi_value promise, workName;
    napi_create_promise(env, &d->deferred, &promise);
    napi_create_string_utf8(env, "sftp_write", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpBinaryExecute, SftpBinaryComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

static napi_value SftpCloseFile(napi_env env, napi_callback_info info) {
    size_t argc = 2;
    napi_value args[2];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);
    if (argc < 2) return ThrowError(env, "Requires handle, fd");

    auto *d = new SftpBinaryData();
    d->opType = 4;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid handle"); }

    napi_get_value_int32(env, args[1], &d->fd);

    napi_value promise, workName;
    napi_create_promise(env, &d->deferred, &promise);
    napi_create_string_utf8(env, "sftp_close_file", NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, SftpBinaryExecute, SftpBinaryComplete, d, &d->work);
    napi_queue_async_work(env, d->work);
    return promise;
}

// ===================================================================
// getHostKeyFingerprint(handle) -> string (sync)
// Returns Base64-encoded SHA-256 fingerprint (matches ssh-keygen format)
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

    // Base64 encode 32 bytes (matches ssh-keygen -lf output)
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const unsigned char *src = (const unsigned char *)hash;
    char out[45]; // ceil(32/3)*4 + 1 = 44 + 1
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
        // SFTP
        {"sftpInit",       nullptr, SftpInit,       nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpShutdown",   nullptr, SftpShutdown,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpListDir",    nullptr, SftpListDir,    nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpReadFile",   nullptr, SftpReadFile,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpWriteFile",  nullptr, SftpWriteFile,  nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpDelete",     nullptr, SftpDelete,     nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpMkdir",      nullptr, SftpMkdir,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpRmdir",      nullptr, SftpRmdir,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpRename",     nullptr, SftpRename,     nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpStat",       nullptr, SftpStat,       nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpChmod",      nullptr, SftpChmod,      nullptr, nullptr, nullptr, napi_default, nullptr},
        // SFTP Phase 2: Binary/Streaming
        {"sftpOpenFile",   nullptr, SftpOpenFile,   nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpRead",       nullptr, SftpRead,       nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpWrite",      nullptr, SftpWrite,      nullptr, nullptr, nullptr, napi_default, nullptr},
        {"sftpCloseFile",  nullptr, SftpCloseFile,  nullptr, nullptr, nullptr, napi_default, nullptr},
        // Stage 5: Security & Config
        {"getHostKeyFingerprint", nullptr, GetHostKeyFingerprint, nullptr, nullptr, nullptr, napi_default, nullptr},
    };

    napi_define_properties(env, exports, sizeof(desc) / sizeof(desc[0]), desc);

    SSH_LOGI("ssh_core N-API module registered");
    return exports;
}
