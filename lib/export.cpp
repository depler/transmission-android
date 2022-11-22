#include "daemon/daemon.h"

#include <mutex>

#define EXPORT_METHOD extern "C" __attribute__((visibility("default")))

std::string tr_web_folder;
std::string tr_session_folder;

EXPORT_METHOD tr_daemon* InitDaemon(int argc, char** argv, char* web_folder, char* session_folder)
{
    tr_web_folder = web_folder;
    tr_session_folder = session_folder;

    int ret = 0;
    bool foreground = false;
    auto daemon = new tr_daemon();

    if (!daemon->init(argc, argv, &foreground, &ret))
    {
        delete daemon;
        daemon = nullptr;
    }

    return daemon;
}

EXPORT_METHOD bool StartDaemon(tr_daemon* daemon, bool foreground)
{
    int ret = 0;
    tr_error* error = nullptr;

    if (!daemon->spawn(foreground, &ret, &error))
    {
        tr_error_free(error);
        return false;
    }

    return true;
}