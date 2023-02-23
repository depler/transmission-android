// This file Copyright © 2014-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#ifdef _WIN32

#include <windows.h>

/* MinGW :( */
#ifndef ERROR_DIRECTORY_NOT_SUPPORTED
#define ERROR_DIRECTORY_NOT_SUPPORTED 336
#endif

#define TR_ERROR_IS_ENOENT(code) ((code) == ERROR_FILE_NOT_FOUND || (code) == ERROR_PATH_NOT_FOUND)
#define TR_ERROR_IS_ENOSPC(code) ((code) == ERROR_DISK_FULL)

#define TR_ERROR_EINVAL ERROR_INVALID_PARAMETER
#define TR_ERROR_EISDIR ERROR_DIRECTORY_NOT_SUPPORTED

#else /* _WIN32 */

#include <errno.h>

#define TR_ERROR_IS_ENOENT(code) ((code) == ENOENT)
#define TR_ERROR_IS_ENOSPC(code) ((code) == ENOSPC)

#define TR_ERROR_EINVAL EINVAL
#define TR_ERROR_EISDIR EISDIR

#endif /* _WIN32 */
