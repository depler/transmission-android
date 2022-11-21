#include "daemon/daemon.h"

#include <mutex>

#define EXPORT_METHOD extern "C" __attribute__((visibility("default")))

#define TR_STATUS_EMPTY -1000
#define TR_STATUS_ERROR_SPAWN -2
#define TR_STATUS_ERROR_INIT -1
#define TR_STATUS_STARTED 0
#define TR_STATUS_RUNNING 1

int tr_status = TR_STATUS_EMPTY;
std::mutex tr_mutex;
std::string tr_web_folder;
std::string tr_session_folder;

EXPORT_METHOD void AbortProcess()
{
    abort();
}

EXPORT_METHOD int GetTransmissionStatus()
{
    std::lock_guard lock(tr_mutex);
    return tr_status;
}

EXPORT_METHOD int StartTransmission(int argc, char** argv, char* web_folder, char* session_folder)
{

    std::lock_guard lock(tr_mutex);

    if (tr_status != TR_STATUS_EMPTY)
        return tr_status;

    tr_web_folder = web_folder;
    tr_session_folder = session_folder;

    int ret = 0;
    bool foreground = false;
    tr_error* error = nullptr;
    tr_daemon daemon;

    if (!daemon.init(argc, argv, &foreground, &ret))
    {
        tr_status = TR_STATUS_ERROR_INIT;
        return tr_status;
    }
 
    if (!daemon.spawn(foreground, &ret, &error))
    {
        tr_status = TR_STATUS_ERROR_SPAWN;
        return tr_status;
    }

    tr_status = TR_STATUS_RUNNING;
    return TR_STATUS_STARTED;
}

