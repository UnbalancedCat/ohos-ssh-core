/**
 * ssh_napi_connect.cpp — Connect, ConnectWithKey, Disconnect
 */

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include "napi_helpers.h"

// ===================================================================
// Connect / ConnectWithKey shared data & executor
// ===================================================================

struct ConnectData {
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

    bool success = false;
    std::string errorMsg;

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
        rc = libssh2_userauth_publickey_frommemory(
            ctx->session,
            d->username.c_str(), d->username.length(),
            nullptr, 0,
            d->privateKey.c_str(), d->privateKey.length(),
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
        NapiResolveUndefined(env, d->deferred);
    } else {
        NapiRejectWithError(env, d->deferred, d->errorMsg);
    }

    napi_delete_async_work(env, d->work);
    delete d;
}

// ===================================================================
// connect(handle, host, port, username, password, [timeout], [keepalive])
// ===================================================================
napi_value NapiConnect(napi_env env, napi_callback_info info) {
    size_t argc = 7;
    napi_value args[7];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 5) {
        return ThrowError(env, "connect requires 5 arguments: handle, host, port, username, password");
    }

    auto *d = new ConnectData();
    d->isKeyAuth = false;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    d->host = NapiGetString(env, args[1]);

    int32_t port = 22;
    napi_get_value_int32(env, args[2], &port);
    d->port = port;

    d->username = NapiGetString(env, args[3]);
    d->password = NapiGetString(env, args[4]);

    if (argc >= 6) d->timeoutSec = NapiGetOptionalInt(env, args[5], 10);
    if (argc >= 7) d->keepaliveInterval = NapiGetOptionalInt(env, args[6], 30);

    return NapiCreatePromiseWork(env, d, "ssh_connect",
                                 ConnectExecute, ConnectComplete,
                                 &d->deferred, &d->work);
}

// ===================================================================
// connectWithKey(handle, host, port, username, privateKey, [passphrase],
//                [timeout], [keepalive])
// ===================================================================
napi_value NapiConnectWithKey(napi_env env, napi_callback_info info) {
    size_t argc = 8;
    napi_value args[8];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 5) {
        return ThrowError(env, "connectWithKey requires at least 5 arguments");
    }

    auto *d = new ConnectData();
    d->isKeyAuth = true;
    d->ctx = UnwrapContext(env, args[0]);
    if (!d->ctx) { delete d; return ThrowError(env, "Invalid SSH handle"); }

    d->host = NapiGetString(env, args[1]);

    int32_t port = 22;
    napi_get_value_int32(env, args[2], &port);
    d->port = port;

    d->username = NapiGetString(env, args[3]);
    d->privateKey = NapiGetString(env, args[4]);

    if (argc >= 6) {
        napi_valuetype type;
        napi_typeof(env, args[5], &type);
        if (type == napi_string) {
            d->passphrase = NapiGetString(env, args[5]);
        }
    }
    if (argc >= 7) d->timeoutSec = NapiGetOptionalInt(env, args[6], 10);
    if (argc >= 8) d->keepaliveInterval = NapiGetOptionalInt(env, args[7], 30);

    return NapiCreatePromiseWork(env, d, "ssh_connect_key",
                                 ConnectExecute, ConnectComplete,
                                 &d->deferred, &d->work);
}

// ===================================================================
// disconnect(handle)
// ===================================================================
napi_value NapiDisconnect(napi_env env, napi_callback_info info) {
    size_t argc = 1;
    napi_value args[1];
    napi_get_cb_info(env, info, &argc, args, nullptr, nullptr);

    if (argc < 1) return ThrowError(env, "disconnect requires 1 argument: handle");

    SshContext *ctx = UnwrapContext(env, args[0]);
    if (ctx) {
        SSH_LOGI("disconnect: cleaning up");
        ctx->cleanup();
    }

    napi_value undefined;
    napi_get_undefined(env, &undefined);
    return undefined;
}
