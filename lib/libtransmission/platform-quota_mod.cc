// This file Copyright © 2013-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <cerrno>
#include <string>
#include <string_view>

#include <event2/util.h> /* evutil_ascii_strcasecmp() */

#ifdef __ANDROID__
#define HAVE_GETMNTENT
#define HAVE_STATVFS

#define btodb(num) ((num) >> 10)

#include <unistd.h>
#include <sys/syscall.h>
#include <sys/quota.h>
#include <sys/statvfs.h>

int quotactl(int cmd, const char* special, int id, caddr_t addr)
{
    return syscall(SYS_quotactl, cmd, special, id, addr);
}
#endif

#ifndef _WIN32
#include <unistd.h> /* getuid() */
#include <sys/types.h> /* types needed by quota.h */
#if defined(__FreeBSD__) || defined(__OpenBSD__)
#include <ufs/ufs/quota.h> /* quotactl() */
#elif defined(__DragonFly__)
#include <vfs/ufs/quota.h> /* quotactl */
#elif defined(__NetBSD__)
#include <sys/param.h>
#ifndef statfs
#define statfs statvfs
#endif
#elif defined(__sun)
#include <sys/fs/ufs_quota.h> /* quotactl */
#else
#include <sys/quota.h> /* quotactl() */
#endif
#ifdef HAVE_GETMNTENT
#ifdef __sun
#include <fcntl.h>
#include <stdio.h>
#include <sys/mntent.h>
#include <sys/mnttab.h>
#define _PATH_MOUNTED MNTTAB
#else
#include <mntent.h>
#include <paths.h> /* _PATH_MOUNTED */
#endif
#else /* BSD derived systems */
#include <sys/param.h>
#include <sys/ucred.h>
#include <sys/mount.h>
#endif
#endif

#ifdef __APPLE__
#ifndef HAVE_SYS_STATVFS_H
#define HAVE_SYS_STATVFS_H
#endif
#ifndef HAVE_STATVFS
#define HAVE_STATVFS
#endif
#endif

#ifdef HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif

#ifdef HAVE_XFS_XFS_H
#define HAVE_XQM
#include <xfs/xqm.h>
#endif

#include "transmission.h"
#include "tr-macros.h"
#include "utils.h"
#include "platform-quota.h"

/***
****
***/

#ifndef _WIN32

static char const* getdev(std::string_view path)
{
#ifdef HAVE_GETMNTENT

#ifdef __sun

    FILE* const fp = fopen(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mnttab mnt;
    while (getmntent(fp, &mnt) != -1)
    {
        if (mnt.mnt_mountp != nullptr && path == mnt.mnt_mountp)
        {
            break;
        }
    }

    fclose(fp);
    return mnt.mnt_special;

#else

    FILE* const fp = setmntent(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mntent const* mnt = nullptr;
    while ((mnt = getmntent(fp)) != nullptr)
    {
        if (mnt->mnt_dir != nullptr && path == mnt->mnt_dir)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != nullptr ? mnt->mnt_fsname : nullptr;

#endif

#else /* BSD derived systems */

    struct statfs* mnt = nullptr;
    int const n = getmntinfo(&mnt, MNT_WAIT);
    if (n == 0)
    {
        return nullptr;
    }

    for (int i = 0; i < n; i++)
    {
        if (path == mnt[i].f_mntonname)
        {
            return mnt[i].f_mntfromname;
        }
    }

    return nullptr;

#endif
}

static char const* getfstype(std::string_view device)
{
#ifdef HAVE_GETMNTENT

#ifdef __sun

    FILE* const fp = fopen(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mnttab mnt;
    while (getmntent(fp, &mnt) != -1)
    {
        if (device == mnt.mnt_mountp)
        {
            break;
        }
    }

    fclose(fp);
    return mnt.mnt_fstype;

#else

    FILE* const fp = setmntent(_PATH_MOUNTED, "r");
    if (fp == nullptr)
    {
        return nullptr;
    }

    struct mntent const* mnt = nullptr;
    while ((mnt = getmntent(fp)) != nullptr)
    {
        if (device == mnt->mnt_fsname)
        {
            break;
        }
    }

    endmntent(fp);
    return mnt != nullptr ? mnt->mnt_type : nullptr;

#endif

#else /* BSD derived systems */

    struct statfs* mnt = nullptr;
    int const n = getmntinfo(&mnt, MNT_WAIT);
    if (n == 0)
    {
        return nullptr;
    }

    for (int i = 0; i < n; i++)
    {
        if (device == mnt[i].f_mntfromname)
        {
            return mnt[i].f_fstypename;
        }
    }

    return nullptr;

#endif
}

static std::string getblkdev(std::string_view path)
{
    for (;;)
    {
        if (auto const* const device = getdev(path); device != nullptr)
        {
            return device;
        }

        auto const pos = path.rfind('/');
        if (pos == std::string_view::npos)
        {
            return {};
        }

        path = path.substr(0, pos);
    }
}

#if defined(__NetBSD__) && __NetBSD_Version__ >= 600000000

extern "C"
{
#include <quota.h>
}

struct tr_disk_space getquota(char const* device)
{
    struct quotahandle* qh;
    struct quotakey qk;
    struct quotaval qv;
    struct tr_disk_space disk_space = { -1, -1 };

    qh = quota_open(device);

    if (qh == nullptr)
    {
        return disk_space;
    }

    qk.qk_idtype = QUOTA_IDTYPE_USER;
    qk.qk_id = getuid();
    qk.qk_objtype = QUOTA_OBJTYPE_BLOCKS;

    if (quota_get(qh, &qk, &qv) == -1)
    {
        quota_close(qh);
        return disk_space;
    }

    int64_t limit;
    if (qv.qv_softlimit > 0)
    {
        limit = qv.qv_softlimit;
    }
    else if (qv.qv_hardlimit > 0)
    {
        limit = qv.qv_hardlimit;
    }
    else
    {
        quota_close(qh);
        return disk_space;
    }

    int64_t const spaceused = qv.qv_usage;
    quota_close(qh);

    int64_t const freespace = limit - spaceused;
    disk_space.free = freespace < 0 ? 0 : freespace;
    disk_space.total = limit;
    return disk_space;
}

#else

static struct tr_disk_space getquota(char const* device)
{
#if defined(__DragonFly__)
    struct ufs_dqblk dq = {};
#else
    struct dqblk dq = {};
#endif
    struct tr_disk_space disk_space = { -1, -1 };

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__) || defined(__APPLE__)
    if (quotactl(device, QCMD(Q_GETQUOTA, USRQUOTA), getuid(), (caddr_t)&dq) != 0)
    {
        return disk_space;
    }
#elif defined(__sun)
    struct quotctl op;
    int fd = open(device, O_RDONLY);

    if (fd < 0)
    {
        return disk_space;
    }

    op.op = Q_GETQUOTA;
    op.uid = getuid();
    op.addr = (caddr_t)&dq;

    if (ioctl(fd, Q_QUOTACTL, &op) != 0)
    {
        close(fd);
        return disk_space;
    }

    close(fd);
#else
    if (quotactl(QCMD(Q_GETQUOTA, USRQUOTA), device, getuid(), (caddr_t)&dq) != 0)
    {
        return disk_space;
    }
#endif

    int64_t limit = 0;
    if (dq.dqb_bsoftlimit > 0)
    {
        /* Use soft limit first */
        limit = dq.dqb_bsoftlimit;
    }
    else if (dq.dqb_bhardlimit > 0)
    {
        limit = dq.dqb_bhardlimit;
    }
    else
    {
        /* No quota enabled for this user */
        return disk_space;
    }

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
    int64_t const spaceused = (int64_t)dq.dqb_curblocks >> 1;
#elif defined(__APPLE__)
    int64_t const spaceused = (int64_t)dq.dqb_curbytes;
#elif defined(__UCLIBC__) && !TR_UCLIBC_CHECK_VERSION(1, 0, 18)
    int64_t const spaceused = (int64_t)btodb(dq.dqb_curblocks);
#elif defined(__sun) || (defined(_LINUX_QUOTA_VERSION) && _LINUX_QUOTA_VERSION < 2)
    int64_t const spaceused = (int64_t)dq.dqb_curblocks >> 1;
#else
    int64_t const spaceused = btodb(dq.dqb_curspace);
#endif

    int64_t const freespace = limit - spaceused;

#ifdef __APPLE__
    disk_space.free = freespace < 0 ? 0 : freespace;
    disk_space.total = limit < 0 ? 0 : limit;
    return disk_space;
#else
    disk_space.free = freespace < 0 ? 0 : (freespace * 1024);
    disk_space.total = limit < 0 ? 0 : (limit * 1024);
    return disk_space;
#endif
}

#endif

#ifdef HAVE_XQM

static struct tr_disk_space getxfsquota(char const* device)
{
    struct tr_disk_space disk_space = { -1, -1 };
    struct fs_disk_quota dq;

    if (quotactl(QCMD(Q_XGETQUOTA, USRQUOTA), device, getuid(), (caddr_t)&dq) == 0)
    {
        int64_t limit = 0;
        if (dq.d_blk_softlimit > 0)
        {
            /* Use soft limit first */
            limit = dq.d_blk_softlimit >> 1;
        }
        else if (dq.d_blk_hardlimit > 0)
        {
            limit = dq.d_blk_hardlimit >> 1;
        }
        else
        {
            /* No quota enabled for this user */
            return disk_space;
        }

        int64_t freespace = limit - (dq.d_bcount >> 1);
        freespace = freespace < 0 ? 0 : (freespace * 1024);
        limit = limit * 1024;
        disk_space.free = freespace;
        disk_space.total = limit;
        return disk_space;
    }

    /* something went wrong */
    return disk_space;
}

#endif /* HAVE_XQM */

#endif /* _WIN32 */

static tr_disk_space getQuotaSpace([[maybe_unused]] tr_device_info const& info)
{
    struct tr_disk_space ret = { -1, -1 };

#ifndef _WIN32

    if (evutil_ascii_strcasecmp(info.fstype.c_str(), "xfs") == 0)
    {
#ifdef HAVE_XQM
        ret = getxfsquota(info.device.c_str());
#endif
    }
    else
    {
        ret = getquota(info.device.c_str());
    }

#endif /* _WIN32 */

    return ret;
}

static struct tr_disk_space getDiskSpace(char const* path)
{
#ifdef _WIN32

    struct tr_disk_space ret = { -1, -1 };

    if (auto const wide_path = tr_win32_utf8_to_native(path); !std::empty(wide_path))
    {
        ULARGE_INTEGER freeBytesAvailable;
        ULARGE_INTEGER totalBytesAvailable;

        if (GetDiskFreeSpaceExW(wide_path.c_str(), &freeBytesAvailable, &totalBytesAvailable, nullptr))
        {
            ret.free = freeBytesAvailable.QuadPart;
            ret.total = totalBytesAvailable.QuadPart;
        }
    }

    return ret;

#elif defined(HAVE_STATVFS)

    struct statvfs buf = {};
    return statvfs(path, &buf) != 0 ?
        (struct tr_disk_space){ -1, -1 } :
        (struct tr_disk_space){ (int64_t)buf.f_bavail * (int64_t)buf.f_frsize, (int64_t)buf.f_blocks * (int64_t)buf.f_frsize };

#else

#warning FIXME: not implemented

    return { -1, -1 };

#endif
}

tr_device_info tr_device_info_create(std::string_view path)
{
    auto out = tr_device_info{};
    out.path = path;
#ifndef _WIN32
    out.device = getblkdev(out.path);
    auto const* const fstype = getfstype(out.path);
    out.fstype = fstype != nullptr ? fstype : "";
#endif
    return out;
}

tr_disk_space tr_device_info_get_disk_space(struct tr_device_info const& info)
{
    if (std::empty(info.path))
    {
        errno = EINVAL;
        return { -1, -1 };
    }

    auto space = getQuotaSpace(info);

    if (space.free < 0 || space.total < 0)
    {
        space = getDiskSpace(info.path.c_str());
    }

    return space;
}
