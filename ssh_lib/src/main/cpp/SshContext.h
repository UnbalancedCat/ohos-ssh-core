#pragma once

#include <libssh2.h>
#include <libssh2_sftp.h>
#include <string>
#include <thread>
#include <atomic>
#include <napi/native_api.h>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_map>

// SSH connection state machine
enum class SshState {
    IDLE,
    CONNECTING,
    CONNECTED,
    ERROR,
    CLOSED
};

// SSH context: bound to an ArkTS object via napi_wrap
struct SshContext {
    // Socket
    int sockfd = -1;

    // libssh2 handles
    LIBSSH2_SESSION *session = nullptr;
    LIBSSH2_CHANNEL *channel = nullptr;
    LIBSSH2_SFTP *sftp = nullptr;

    // SFTP open file handles map
    std::unordered_map<int, LIBSSH2_SFTP_HANDLE*> openFiles;
    int nextFd = 1;

    // State
    std::atomic<SshState> state{SshState::IDLE};
    std::string lastError;

    // Background read thread for interactive shell
    std::thread readThread;
    std::atomic<bool> stopReading{false};

    // N-API threadsafe function for cross-thread data delivery
    napi_threadsafe_function tsfn = nullptr;

    // Cleanup all resources in a safe order
    void cleanup() {
        // 1. Signal the read thread to stop
        stopReading = true;

        // 2. If the session is in non-blocking mode, switch back to blocking
        //    so that cleanup calls don't return EAGAIN
        if (session) {
            libssh2_session_set_blocking(session, 1);
        }

        // 3. Wait for read thread to finish
        if (readThread.joinable()) {
            readThread.join();
        }

        // 4. Close SFTP subsystem and any open files
        for (auto& pair : openFiles) {
            if (pair.second) libssh2_sftp_close(pair.second);
        }
        openFiles.clear();

        if (sftp) {
            libssh2_sftp_shutdown(sftp);
            sftp = nullptr;
        }

        // 5. Close channel
        if (channel) {
            libssh2_channel_close(channel);
            libssh2_channel_free(channel);
            channel = nullptr;
        }

        // 6. Disconnect and free session
        if (session) {
            libssh2_session_disconnect(session, "Normal shutdown");
            libssh2_session_free(session);
            session = nullptr;
        }

        // 7. Close socket
        if (sockfd >= 0) {
            close(sockfd);
            sockfd = -1;
        }

        // 8. Release threadsafe function
        if (tsfn) {
            napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
            tsfn = nullptr;
        }

        state = SshState::CLOSED;
    }

    ~SshContext() {
        cleanup();
    }
};

