# the minimum version of CMake.
cmake_minimum_required(VERSION 3.5.0)
project(ssh_core)

set(CPP_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
set(THIRD_PARTY ${CPP_ROOT}/third_party)

if(DEFINED PACKAGE_FIND_FILE)
    include(${PACKAGE_FIND_FILE})
endif()

# ==========================================================
# 1. Crypto Backend Selection
# ==========================================================
set(LIBSSH2_DIR ${THIRD_PARTY}/libssh2)
option(USE_OPENSSL "Use OpenSSL backend instead of mbedTLS" ON)

if(USE_OPENSSL)
    message(STATUS "SSH Core: Using OpenSSL backend")
    set(OPENSSL_DIR ${THIRD_PARTY}/openssl)

    # Architecture mapping — hvigor passes -DCMAKE_OHOS_ARCH_ABI=arm64-v8a|x86_64
    if(CMAKE_OHOS_ARCH_ABI STREQUAL "arm64-v8a")
        set(OPENSSL_ARCH_DIR ${OPENSSL_DIR}/arm64-v8a)
    elseif(CMAKE_OHOS_ARCH_ABI STREQUAL "x86_64")
        set(OPENSSL_ARCH_DIR ${OPENSSL_DIR}/x86_64)
    else()
        message(FATAL_ERROR "Unsupported architecture for OpenSSL: ${CMAKE_OHOS_ARCH_ABI}")
    endif()

    add_library(crypto_backend STATIC IMPORTED)
    set_target_properties(crypto_backend PROPERTIES IMPORTED_LOCATION ${OPENSSL_ARCH_DIR}/libcrypto.a)

    add_library(ssl_backend STATIC IMPORTED)
    set_target_properties(ssl_backend PROPERTIES IMPORTED_LOCATION ${OPENSSL_ARCH_DIR}/libssl.a)

    set(CRYPTO_INCLUDE_DIRS ${OPENSSL_DIR}/include)
    set(LIBSSH2_CRYPTO_C ${LIBSSH2_DIR}/src/openssl.c)
    set(LIBSSH2_CRYPTO_MACRO LIBSSH2_OPENSSL)
    set(CRYPTO_LIBS crypto_backend ssl_backend)
else()
    message(STATUS "SSH Core: Using mbedTLS backend")
    set(MBEDTLS_DIR ${THIRD_PARTY}/mbedtls)
    file(GLOB MBEDTLS_CRYPTO_SOURCES ${MBEDTLS_DIR}/library/*.c)
    list(FILTER MBEDTLS_CRYPTO_SOURCES EXCLUDE REGEX "(ssl_|debug\\.c|mps_reader\\.c|mps_trace\\.c|net_sockets\\.c)")

    add_library(crypto_backend STATIC ${MBEDTLS_CRYPTO_SOURCES})
    target_include_directories(crypto_backend PUBLIC ${MBEDTLS_DIR}/include ${MBEDTLS_DIR}/library)
    target_compile_options(crypto_backend PRIVATE -w)

    set(CRYPTO_INCLUDE_DIRS ${MBEDTLS_DIR}/include)
    set(LIBSSH2_CRYPTO_C ${LIBSSH2_DIR}/src/mbedtls.c)
    set(LIBSSH2_CRYPTO_MACRO LIBSSH2_MBEDTLS)
    set(CRYPTO_LIBS crypto_backend)
endif()

# ==========================================================
# 2. libssh2 static library
# ==========================================================

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
    ${LIBSSH2_CRYPTO_C}
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

target_compile_definitions(ssh2 PRIVATE
    ${LIBSSH2_CRYPTO_MACRO}
    HAVE_CONFIG_H
)

target_include_directories(ssh2 PUBLIC
    ${LIBSSH2_DIR}/include
    ${LIBSSH2_DIR}/src
    ${CRYPTO_INCLUDE_DIRS}
)

target_link_libraries(ssh2 PRIVATE ${CRYPTO_LIBS})
target_compile_options(ssh2 PRIVATE -w)

# ==========================================================
# 3. Final N-API shared library: libssh_core.so
# ==========================================================
add_library(ssh_core SHARED
    napi_init.cpp
    ssh_napi.cpp
)

target_include_directories(ssh_core PRIVATE
    ${CPP_ROOT}
    ${LIBSSH2_DIR}/include
    ${CRYPTO_INCLUDE_DIRS}
)

target_link_libraries(ssh_core PRIVATE
    ssh2
    ${CRYPTO_LIBS}
    libace_napi.z.so
    libhilog_ndk.z.so
)