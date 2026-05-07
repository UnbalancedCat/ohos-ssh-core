#include "napi/native_api.h"

// Implemented in ssh_napi.cpp
napi_value RegisterSshNapi(napi_env env, napi_value exports);

EXTERN_C_START
static napi_value Init(napi_env env, napi_value exports) {
    return RegisterSshNapi(env, exports);
}
EXTERN_C_END

static napi_module sshModule = {
    .nm_version = 1,
    .nm_flags = 0,
    .nm_filename = nullptr,
    .nm_register_func = Init,
    .nm_modname = "ssh_core",
    .nm_priv = nullptr,
    .reserved = { 0 },
};

extern "C" __attribute__((constructor)) void RegisterSshCoreModule(void) {
    napi_module_register(&sshModule);
}
