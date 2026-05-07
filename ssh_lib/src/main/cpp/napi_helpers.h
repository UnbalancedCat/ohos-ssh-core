#pragma once

/**
 * napi_helpers.h — Common NAPI utility functions and macros for ssh_core.
 *
 * Provides:
 * - HiLog macros
 * - String/int extraction from napi_value
 * - Promise resolve/reject helpers
 * - Shared forward declarations
 */

#include <string>
#include <cstring>
#include <cerrno>

#include <napi/native_api.h>
#include <hilog/log.h>
#include <libssh2.h>

#include "SshContext.h"

// ---------------------------------------------------------------------------
// HiLog
// ---------------------------------------------------------------------------
#undef LOG_TAG
#define LOG_TAG "SshCore"
#undef LOG_DOMAIN
#define LOG_DOMAIN 0x0201

#define SSH_LOGI(...) OH_LOG_INFO(LOG_APP, __VA_ARGS__)
#define SSH_LOGE(...) OH_LOG_ERROR(LOG_APP, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Unwrap SshContext* from JS handle
// ---------------------------------------------------------------------------
inline SshContext *UnwrapContext(napi_env env, napi_value jsHandle) {
    void *data = nullptr;
    napi_unwrap(env, jsHandle, &data);
    return static_cast<SshContext *>(data);
}

// ---------------------------------------------------------------------------
// Throw JS Error and return nullptr
// ---------------------------------------------------------------------------
inline napi_value ThrowError(napi_env env, const char *msg) {
    napi_throw_error(env, nullptr, msg);
    return nullptr;
}

// ---------------------------------------------------------------------------
// Get libssh2 session error message
// ---------------------------------------------------------------------------
inline std::string GetSessionError(LIBSSH2_SESSION *session) {
    char *errmsg = nullptr;
    int errlen = 0;
    libssh2_session_last_error(session, &errmsg, &errlen, 0);
    if (errmsg && errlen > 0) {
        return std::string(errmsg, errlen);
    }
    return "Unknown libssh2 error";
}

// ---------------------------------------------------------------------------
// Extract std::string from napi_value
// ---------------------------------------------------------------------------
inline std::string NapiGetString(napi_env env, napi_value val) {
    size_t len = 0;
    napi_get_value_string_utf8(env, val, nullptr, 0, &len);
    std::string str(len, '\0');
    napi_get_value_string_utf8(env, val, &str[0], len + 1, &len);
    return str;
}

// ---------------------------------------------------------------------------
// Extract optional int32 from napi_value (returns defaultVal if not a number)
// ---------------------------------------------------------------------------
inline int32_t NapiGetOptionalInt(napi_env env, napi_value val, int32_t defaultVal) {
    napi_valuetype type;
    napi_typeof(env, val, &type);
    if (type == napi_number) {
        int32_t v = defaultVal;
        napi_get_value_int32(env, val, &v);
        return v;
    }
    return defaultVal;
}

// ---------------------------------------------------------------------------
// Promise helpers
// ---------------------------------------------------------------------------
inline void NapiRejectWithError(napi_env env, napi_deferred deferred, const std::string &msg) {
    napi_value errMsg, error;
    napi_create_string_utf8(env, msg.c_str(), NAPI_AUTO_LENGTH, &errMsg);
    napi_create_error(env, nullptr, errMsg, &error);
    napi_reject_deferred(env, deferred, error);
}

inline void NapiResolveUndefined(napi_env env, napi_deferred deferred) {
    napi_value undefined;
    napi_get_undefined(env, &undefined);
    napi_resolve_deferred(env, deferred, undefined);
}

inline void NapiResolveString(napi_env env, napi_deferred deferred, const std::string &str) {
    napi_value result;
    napi_create_string_utf8(env, str.c_str(), str.size(), &result);
    napi_resolve_deferred(env, deferred, result);
}

// ---------------------------------------------------------------------------
// Create async work + promise boilerplate
// ---------------------------------------------------------------------------
inline napi_value NapiCreatePromiseWork(napi_env env, void *data,
                                         const char *name,
                                         napi_async_execute_callback execCb,
                                         napi_async_complete_callback completeCb,
                                         napi_deferred *outDeferred,
                                         napi_async_work *outWork) {
    napi_value promise;
    napi_create_promise(env, outDeferred, &promise);

    napi_value workName;
    napi_create_string_utf8(env, name, NAPI_AUTO_LENGTH, &workName);
    napi_create_async_work(env, nullptr, workName, execCb, completeCb, data, outWork);
    napi_queue_async_work(env, *outWork);

    return promise;
}
