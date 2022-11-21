// This file Copyright © 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm>
#include <string_view>

#include "transmission.h"
#include "error.h"
#include "file.h"
#include "tr-assert.h"

using namespace std::literals;

bool tr_sys_file_write_line(tr_sys_file_t handle, std::string_view buffer, tr_error** error)
{
    TR_ASSERT(handle != TR_BAD_SYS_FILE);

    bool ret = tr_sys_file_write(handle, std::data(buffer), std::size(buffer), nullptr, error);

    if (ret)
    {
        ret = tr_sys_file_write(handle, TR_NATIVE_EOL_STR, TR_NATIVE_EOL_STR_SIZE, nullptr, error);
    }

    return ret;
}
