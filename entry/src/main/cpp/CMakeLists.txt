# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(ssh_core)

set(CPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(THIRD_PARTY ${CPP_ROOT}/third_party)

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

# ==========================================================
# 1. mbedTLS static library (crypto + x509 + pk only)
#    SSH does not need the TLS layer from mbedTLS.
# ==========================================================
set(MBEDTLS_DIR ${THIRD_PARTY}/mbedtls)

file(GLOB MBEDTLS_CRYPTO_SOURCES ${MBEDTLS_DIR}/library/*.c)

# Exclude SSL/TLS layer, debug, MPS, net_sockets — not needed for SSH
list(FILTER MBEDTLS_CRYPTO_SOURCES EXCLUDE REGEX
    "(ssl_|debug\\.c|mps_reader\\.c|mps_trace\\.c|net_sockets\\.c)")

add_library(mbedcrypto STATIC ${MBEDTLS_CRYPTO_SOURCES})
target_include_directories(mbedcrypto PUBLIC
    ${MBEDTLS_DIR}/include
    ${MBEDTLS_DIR}/library       # internal headers (common.h, etc.)
)
# Suppress all warnings for third-party code
target_compile_options(mbedcrypto PRIVATE -w)

# ==========================================================
# 2. libssh2 static library (mbedTLS backend)
# ==========================================================
set(LIBSSH2_DIR ${THIRD_PARTY}/libssh2)

set(LIBSSH2_SOURCES
    ${LIBSSH2_DIR}/src/agent.c
    ${LIBSSH2_DIR}/src/bcrypt_pbkdf.c
    ${LIBSSH2_DIR}/src/blowfish.c
    ${LIBSSH2_DIR}/src/channel.c
    ${LIBSSH2_DIR}/src/comp.c
    ${LIBSSH2_DIR}/src/chacha.c
    ${LIBSSH2_DIR}/src/cipher-chachapoly.c
    ${LIBSSH2_DIR}/src/crypt.c
    ${LIBSSH2_DIR}/src/crypto.c
    ${LIBSSH2_DIR}/src/global.c
    ${LIBSSH2_DIR}/src/hostkey.c
    ${LIBSSH2_DIR}/src/keepalive.c
    ${LIBSSH2_DIR}/src/kex.c
    ${LIBSSH2_DIR}/src/knownhost.c
    ${LIBSSH2_DIR}/src/mac.c
    ${LIBSSH2_DIR}/src/mbedtls.c
    ${LIBSSH2_DIR}/src/misc.c
    ${LIBSSH2_DIR}/src/packet.c
    ${LIBSSH2_DIR}/src/pem.c
    ${LIBSSH2_DIR}/src/poly1305.c
    ${LIBSSH2_DIR}/src/publickey.c
    ${LIBSSH2_DIR}/src/scp.c
    ${LIBSSH2_DIR}/src/session.c
    ${LIBSSH2_DIR}/src/sftp.c
    ${LIBSSH2_DIR}/src/transport.c
    ${LIBSSH2_DIR}/src/userauth.c
    ${LIBSSH2_DIR}/src/userauth_kbd_packet.c
    ${LIBSSH2_DIR}/src/version.c
)

add_library(ssh2 STATIC ${LIBSSH2_SOURCES})

# Critical: tell libssh2 to use mbedTLS as crypto backend
# HAVE_CONFIG_H: makes libssh2_setup.h include our hand-written libssh2_config.h
target_compile_definitions(ssh2 PRIVATE
    LIBSSH2_MBEDTLS
    HAVE_CONFIG_H
)

target_include_directories(ssh2 PUBLIC
    ${LIBSSH2_DIR}/include         # public: libssh2.h
    ${LIBSSH2_DIR}/src             # private: libssh2_priv.h, mbedtls.h, etc.
)

target_link_libraries(ssh2 PRIVATE mbedcrypto)

# Suppress all warnings for third-party code
target_compile_options(ssh2 PRIVATE -w)

# ==========================================================
# 3. Final N-API shared library: libssh_core.so
# ==========================================================
add_library(ssh_core SHARED
    napi_init.cpp
    ssh_napi.cpp
)

target_include_directories(ssh_core PRIVATE
    ${CPP_ROOT}                    # SshContext.h
    ${LIBSSH2_DIR}/include         # libssh2.h
    ${MBEDTLS_DIR}/include         # mbedtls headers (if needed)
)

target_link_libraries(ssh_core PRIVATE
    ssh2
    mbedcrypto
    libace_napi.z.so               # N-API runtime
    libhilog_ndk.z.so              # HiLog for native logging
)