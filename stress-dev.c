/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"dev N",	"start N device entry thrashing stressors" },
	{ NULL,	"dev-ops N",	"stop after N device thrashing bogo ops" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_POLL_H) &&		\
    defined(HAVE_LIB_PTHREAD) && 	\
    !defined(__sun__) && 		\
    !defined(__HAIKU__)

#define MAX_DEV_THREADS		(4)

static sigset_t set;
static shim_pthread_spinlock_t lock;
static char *dev_path;
static uint32_t mixup;

typedef struct stress_dev_func {
	const char *devpath;
	const size_t devpath_len;
	void (*func)(const char *name, const int fd, const char *devpath);
} stress_dev_func_t;

static stress_hash_table_t *dev_hash_table, *scsi_hash_table;

/*
 *  mixup_sort()
 *	sort helper based on hash to mix up ordering
 */
static int mixup_sort(const struct dirent **d1, const struct dirent **d2)
{
	uint32_t s1, s2;

	s1 = stress_hash_pjw((*d1)->d_name);
	s2 = stress_hash_pjw((*d2)->d_name);

	if (s1 == s2)
		return 0;
	return (s1 < s2) ? -1 : 1;
}

#if defined(__linux__) && 		\
    defined(HAVE_LINUX_MEDIA_H) && 	\
    defined(MEDIA_IOC_DEVICE_INFO)
static void stress_dev_media_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(MEDIA_IOC_DEVICE_INFO)
	{
		struct media_device_info mdi;
		int ret;

		ret = ioctl(fd, MEDIA_IOC_DEVICE_INFO, &mdi);
		if (ret < 0)
			return;

		if (!mdi.driver[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null driver name\n",
				name, devpath);
		if (!mdi.model[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null model name\n",
				name, devpath);
		if (!mdi.bus_info[0])
			pr_inf("%s: ioctl MEDIA_IOC_DEVICE_INFO %s: null bus_info field\n",
				name, devpath);
	}
#endif
}
#endif

#if defined(HAVE_LINUX_VT_H)
static void stress_dev_vcs_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(VT_GETMODE) &&	\
    defined(HAVE_VT_MODE)
	{
		struct vt_mode mode;
		int ret;

		ret = ioctl(fd, VT_GETMODE, &mode);
		(void)ret;
	}
#endif
#if defined(VT_GETSTATE) &&	\
    defined(HAVE_VT_STAT)
	{
		struct vt_stat vt_stat;
		int ret;

		ret = ioctl(fd, VT_GETSTATE, &vt_stat);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_LINUX_DM_IOCTL_H)
static void stress_dev_dm_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(DM_VERSION) &&	\
    defined(HAVE_DM_IOCTL)
	{
		struct dm_ioctl dm;
		int ret;

		ret = ioctl(fd, DM_VERSION, &dm);
		(void)ret;
	}
#endif
#if defined(DM_STATUS) &&	\
    defined(HAVE_DM_IOCTL)
	{
		struct dm_ioctl dm;
		int ret;

		ret = ioctl(fd, DM_STATUS, &dm);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_LINUX_VIDEODEV2_H)
static void stress_dev_video_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(VIDIOC_QUERYCAP)
	{
		struct v4l2_capability c;
		int ret;

		(void)memset(&c, 0, sizeof(c));
		ret = ioctl(fd, VIDIOC_QUERYCAP, &c);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_FBUF)
	{
		struct v4l2_framebuffer f;
		int ret;

		(void)memset(&f, 0, sizeof(f));
		ret = ioctl(fd, VIDIOC_G_FBUF, &f);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_STD)
	{
		v4l2_std_id id;
		int ret;

		(void)memset(&id, 0, sizeof(id));
		ret = ioctl(fd, VIDIOC_G_STD, &id);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_AUDIO)
	{
		struct v4l2_audio a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_AUDIO, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_INPUT)
	{
		int in = 0, ret;

		ret = ioctl(fd, VIDIOC_G_INPUT, &in);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_OUTPUT)
	{
		int in = 0, ret;

		ret = ioctl(fd, VIDIOC_G_OUTPUT, &in);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_AUDOUT)
	{
		struct v4l2_audioout a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_AUDOUT, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_JPEGCOMP)
	{
		struct v4l2_jpegcompression a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_JPEGCOMP, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_QUERYSTD)
	{
		v4l2_std_id a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_QUERYSTD, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_PRIORITY)
	{
		uint32_t a;
		int ret;

		ret = ioctl(fd, VIDIOC_G_PRIORITY, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_ENC_INDEX)
	{
		struct v4l2_enc_idx a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_ENC_INDEX, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_G_ENC_INDEX)
	{
		struct v4l2_event a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_G_ENC_INDEX, &a);
		(void)ret;
	}
#endif
#if defined(VIDIOC_QUERY_DV_TIMINGS)
	{
		struct v4l2_dv_timings a;
		int ret;

		(void)memset(&a, 0, sizeof(a));
		ret = ioctl(fd, VIDIOC_QUERY_DV_TIMINGS, &a);
		(void)ret;
	}
#endif
}
#endif

#if defined(HAVE_TERMIOS_H) &&	\
    defined(TCGETS)
static void stress_dev_tty(
	const char *name,
	const int fd,
	const char *devpath)
{
	int ret;
	struct termios t;

	(void)name;
	(void)devpath;

	if (!isatty(fd))
		return;

	ret = tcgetattr(fd, &t);
	(void)ret;
#if defined(TCGETS)
	{
		ret = ioctl(fd, TCGETS, &t);
#if defined(TCSETS)
		if (ret == 0) {
			ret = ioctl(fd, TCSETS, &t);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGPTLCK)
	{
		int lck;

		ret = ioctl(fd, TIOCGPTLCK, &lck);
#if defined(TIOCSPTLCK)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSPTLCK, &lck);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGPKT)
	{
		int pktmode;

		ret = ioctl(fd, TIOCGPKT, &pktmode);
#if defined(TIOCPKT)
		if (ret == 0) {
			ret = ioctl(fd, TIOCPKT, &pktmode);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGPTN)
	{
		int ptnum;

		ret = ioctl(fd, TIOCGPTN, &ptnum);
		(void)ret;
	}
#endif
#if defined(TIOCSIG) &&	\
    defined(SIGCONT)
	{
		int sig = SIGCONT;

		/* generally causes EINVAL */
		ret = ioctl(fd, TIOCSIG, &sig);
		(void)ret;
	}
#endif
#if defined(TIOCGWINSZ)
	{
		struct winsize ws;

		ret = ioctl(fd, TIOCGWINSZ, &ws);
#if defined(TIOCSWINSZ)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSWINSZ, &ws);
		}
#endif
		(void)ret;
	}
#endif
#if defined(FIONREAD)
	{
		int n;

		ret = ioctl(fd, FIONREAD, &n);
		(void)ret;
	}
#endif
#if defined(TIOCINQ)
	{
		int n;

		ret = ioctl(fd, TIOCINQ, &n);
		(void)ret;
	}
#endif
#if defined(TIOCOUTQ)
	{
		int n;

		ret = ioctl(fd, TIOCOUTQ, &n);
		(void)ret;
	}
#endif
#if defined(TIOCGPGRP)
	{
		pid_t pgrp;

		ret = ioctl(fd, TIOCGPGRP, &pgrp);
#if defined(TIOCSPGRP)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSPGRP, &pgrp);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGSID)
	{
		pid_t gsid;

		ret = ioctl(fd, TIOCGSID, &gsid);
		(void)ret;
	}
#endif
#if defined(TIOCGEXCL)
	{
		int excl;

		ret = ioctl(fd, TIOCGEXCL, &excl);
		if (ret == 0) {
#if defined(TIOCNXCL) &&	\
    defined(TIOCEXCL)
			if (excl) {
				ret = ioctl(fd, TIOCNXCL, NULL);
				(void)ret;
				ret = ioctl(fd, TIOCEXCL, NULL);
			} else {
				ret = ioctl(fd, TIOCEXCL, NULL);
				(void)ret;
				ret = ioctl(fd, TIOCNXCL, NULL);
			}
#endif
		}
		(void)ret;
	}
#endif
/*
 *  On some older 3.13 kernels this can lock up, need to add
 *  a method to detect and skip this somehow. For the moment
 *  disable this stress test.
 */
#if defined(TIOCGETD) && 0
	{
		int ldis;

		ret = ioctl(fd, TIOCGETD, &ldis);
#if defined(TIOCSETD)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSETD, &ldis);
		}
#endif
		(void)ret;
	}
#endif

#if defined(TCOOFF) && 	\
    defined(TCOON)
	{
		ret = ioctl(fd, TCOOFF, 0);
		if (ret == 0)
			ret = ioctl(fd, TCOON, 0);
		(void)ret;
	}
#endif

#if defined(TCIOFF) &&	\
    defined(TCION)
	{
		ret = ioctl(fd, TCIOFF, 0);
		if (ret == 0)
			ret = ioctl(fd, TCION, 0);
		(void)ret;
	}
#endif

	/* Modem */
#if defined(TIOCGSOFTCAR)
	{
		int flag;

		ret = ioctl(fd, TIOCGSOFTCAR, &flag);
#if defined(TIOCSSOFTCAR)
		if (ret == 0) {
			ret = ioctl(fd, TIOCSSOFTCAR, &flag);
		}
#endif
		(void)ret;
	}
#endif

#if defined(KDGETLED)
	{
		char state;

		ret = ioctl(fd, KDGETLED, &state);
		(void)ret;
	}
#endif

#if defined(KDGKBTYPE)
	{
		char type;

		ret = ioctl(fd, KDGKBTYPE, &type);
		(void)ret;
	}
#endif

#if defined(KDGETMODE)
	{
		int mode;

		ret = ioctl(fd, KDGETMODE, &mode);
		(void)ret;
	}
#endif

#if defined(KDGKBMODE)
	{
		long mode;

		ret = ioctl(fd, KDGKBMODE, &mode);
		(void)ret;
	}
#endif

#if defined(KDGKBMETA)
	{
		long mode;

		ret = ioctl(fd, KDGKBMETA, &mode);
		(void)ret;
	}
#endif
#if defined(TIOCMGET)
	{
		int status;

		ret = ioctl(fd, TIOCMGET, &status);
#if defined(TIOCMSET)
		if (ret == 0) {
#if defined(TIOCMBIC)
			ret = ioctl(fd, TIOCMBIC, &status);
			(void)ret;
#endif
#if defined(TIOCMBIS)
			ret = ioctl(fd, TIOCMBIS, &status);
			(void)ret;
#endif
			ret = ioctl(fd, TIOCMSET, &status);
		}
#endif
		(void)ret;
	}
#endif
#if defined(TIOCGICOUNT) &&		\
    defined(HAVE_LINUX_SERIAL_H) &&	\
    defined(HAVE_SERIAL_ICOUNTER)
	{
		struct serial_icounter_struct counter;

		ret = ioctl(fd, TIOCGICOUNT, &counter);
		(void)ret;
	}
#endif
#if defined(TIOCGSERIAL) &&		\
    defined(HAVE_LINUX_SERIAL_H) &&	\
    defined(HAVE_SERIAL_STRUCT)
	{
		struct serial_struct serial;

		ret = ioctl(fd, TIOCGSERIAL, &serial);
		(void)ret;
	}
#endif
}
#endif

/*
 *  stress_dev_blk()
 *	block device specific ioctls
 */
static void stress_dev_blk(
	const char *name,
	const int fd,
	const char *devpath)
{
	off_t offset;

	(void)name;
	(void)fd;
	(void)devpath;

#if defined(BLKFLSBUF)
	{
		int ret;
		ret = ioctl(fd, BLKFLSBUF, 0);
		(void)ret;
	}
#endif
#if defined(BLKRAGET)
	/* readahead */
	{
		unsigned long ra;
		int ret;

		ret = ioctl(fd, BLKRAGET, &ra);
		(void)ret;
	}
#endif
#if defined(BLKROGET)
	/* readonly state */
	{
		int ret, ro;

		ret = ioctl(fd, BLKROGET, &ro);
		(void)ret;
	}
#endif
#if defined(BLKBSZGET)
	/* get block device soft block size */
	{
		int ret, sz;

		ret = ioctl(fd, BLKBSZGET, &sz);
		(void)ret;
	}
#endif
#if defined(BLKPBSZGET)
	/* get block device physical block size */
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKPBSZGET, &sz);
		(void)ret;
	}
#endif
#if defined(BLKIOMIN)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKIOMIN, &sz);
		(void)ret;
	}
#endif
#if defined(BLKIOOPT)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKIOOPT, &sz);
		(void)ret;
	}
#endif
#if defined(BLKALIGNOFF)
	{
		unsigned int sz;
		int ret;

		ret = ioctl(fd, BLKALIGNOFF, &sz);
		(void)ret;
	}
#endif
#if defined(BLKROTATIONAL)
	{
		unsigned short rotational;
		int ret;

		ret = ioctl(fd, BLKROTATIONAL, &rotational);
		(void)ret;
	}
#endif
#if defined(BLKSECTGET)
	{
		unsigned short max_sectors;
		int ret;

		ret = ioctl(fd, BLKSECTGET, &max_sectors);
		(void)ret;
	}
#endif
#if defined(BLKGETSIZE)
	{
		unsigned long sz;
		int ret;

		ret = ioctl(fd, BLKGETSIZE, &sz);
		(void)ret;
	}
#endif
#if defined(BLKGETSIZE64)
	{
		uint64_t sz;
		int ret;

		ret = ioctl(fd, BLKGETSIZE64, &sz);
		(void)ret;
	}
#endif
#if defined(BLKGETZONESZ)
	{
		uint32_t sz;
		int ret;

		ret = ioctl(fd, BLKGETZONESZ, &sz);
		(void)ret;
	}
#endif
#if defined(BLKGETNRZONES)
	{
		uint32_t sz;
		int ret;

		ret = ioctl(fd, BLKGETNRZONES, &sz);
		(void)ret;
	}
#endif
	offset = lseek(fd, 0, SEEK_END);
	stress_uint64_put((uint64_t)offset);

	offset = lseek(fd, 0, SEEK_SET);
	stress_uint64_put((uint64_t)offset);

	offset = lseek(fd, 0, SEEK_CUR);
	stress_uint64_put((uint64_t)offset);
}

#if defined(__linux__)
static inline const char *dev_basename(const char *devpath)
{
	const char *ptr = devpath;
	const char *base = devpath;

	while (*ptr) {
		if ((*ptr == '/') && (*(ptr + 1)))
			base = ptr + 1;
		ptr++;
	}

	return base;
}

static inline void add_scsi_dev(const char *devpath)
{
	int ret;

	ret = shim_pthread_spin_lock(&lock);
	if (ret)
		return;

	stress_hash_add(scsi_hash_table, devpath);
	(void)shim_pthread_spin_unlock(&lock);
}

static inline bool is_scsi_dev_cached(const char *devpath)
{
	int ret;
	bool is_scsi = false;

	ret = shim_pthread_spin_lock(&lock);
	if (ret)
		return false;

	is_scsi = (stress_hash_get(scsi_hash_table, devpath) != NULL);
	(void)shim_pthread_spin_unlock(&lock);

	return is_scsi;
}

static inline bool is_scsi_dev(const char *devpath)
{
	int i, n;
	static const char scsi_device_path[] = "/sys/class/scsi_device/";
	struct dirent **scsi_device_list;
	bool is_scsi = false;
	const char *devname = dev_basename(devpath);

	if (!*devname)
		return false;

	if (is_scsi_dev_cached(devpath))
		return true;

	scsi_device_list = NULL;
	n = scandir(scsi_device_path, &scsi_device_list, NULL, alphasort);
	if (n <= 0)
		return is_scsi;

	for (i = 0; !is_scsi && (i < n); i++) {
		int j, m;
		char scsi_block_path[PATH_MAX];
		struct dirent **scsi_block_list;

		if (scsi_device_list[i]->d_name[0] == '.')
			continue;

		(void)snprintf(scsi_block_path, sizeof(scsi_block_path),
			"%s/%s/device/block", scsi_device_path,
			scsi_device_list[i]->d_name);
		scsi_block_list = NULL;
		m = scandir(scsi_block_path, &scsi_block_list, NULL, alphasort);
		if (m <= 0)
			continue;

		for (j = 0; j < m; j++) {
			if (!strcmp(devname, scsi_block_list[j]->d_name)) {
				is_scsi = true;
				break;
			}
		}

		stress_dirent_list_free(scsi_block_list, m);
	}

	stress_dirent_list_free(scsi_device_list, n);

	if (is_scsi)
		add_scsi_dev(devpath);

	return is_scsi;
}

#else
static inline bool is_scsi_dev(const char *devpath)
{
	(void)devpath;

	/* Assume not */
	return false;
}
#endif

/*
 *  stress_dev_scsi_blk()
 *	SCSI block device specific ioctls
 */
static void stress_dev_scsi_blk(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;

	if (!is_scsi_dev(devpath))
		return;

#if defined(SG_GET_VERSION_NUM)
	{
		int ret, ver;

		ret = ioctl(fd, SG_GET_VERSION_NUM, &ver);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_IDLUN)
	{
		int ret;
		struct sng_scsi_idlun {
			int four_in_one;
			int host_unique_id;
		} lun;

		(void)memset(&lun, 0, sizeof(lun));
		ret = ioctl(fd, SCSI_IOCTL_GET_IDLUN, &lun);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_BUS_NUMBER)
	{
		int ret, bus;

		ret = ioctl(fd, SCSI_IOCTL_GET_BUS_NUMBER, &bus);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_TIMEOUT)
	{
		int ret;

		ret = ioctl(fd, SCSI_IOCTL_GET_TIMEOUT, 0);
		(void)ret;
	}
#endif
#if defined(SCSI_IOCTL_GET_RESERVED_SIZE)
	{
		int ret, sz;

		ret = ioctl(fd, SCSI_IOCTL_GET_RESERVED_SIZE, &sz);
		(void)ret;
	}
#endif
}

#if defined(HAVE_LINUX_RANDOM_H)
/*
 *  stress_dev_random_linux()
 *	Linux /dev/random ioctls
 */
static void stress_dev_random_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(RNDGETENTCNT)
	{
		long entropy;
		int ret;

		ret = ioctl(fd, RNDGETENTCNT, &entropy);
		(void)ret;
	}
#endif
}
#endif

#if defined(__linux__)
/*
 *  stress_dev_mem_mmap_linux()
 *	Linux mmap'ing on a device
 */
static void stress_dev_mem_mmap_linux(const int fd, const bool read_page)
{
	void *ptr;
	const size_t page_size = stress_get_pagesize();

	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED) {
		(void)munmap(ptr, page_size);
	}
	if (read_page) {
		off_t off;

		/* Try seeking */
		off = lseek(fd, (off_t)0, SEEK_SET);
#if defined(STRESS_ARCH_X86)
		if (off == 0) {
			char buffer[page_size];
			ssize_t ret;

			/* And try reading */
			ret = read(fd, buffer, page_size);
			(void)ret;
		}
#else
		(void)off;
#endif
	}

	ptr = mmap(NULL, page_size, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED) {
		(void)munmap(ptr, page_size);
	}

}

static void stress_dev_mem_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, true);
}
#endif

#if defined(__linux__)
static void stress_dev_kmem_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, false);
}
#endif

#if defined(__linux__) &&		\
    defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_MSF) &&		\
    defined(HAVE_CDROM_TOCENTRY)
/*
 * cdrom_get_address_msf()
 *      Given a track and fd, the function returns
 *      the address of the track in MSF format
 */
static void cdrom_get_address_msf(
	const int fd,
	int track,
	uint8_t *min,
	uint8_t *seconds,
	uint8_t *frames)
{
	struct cdrom_tocentry entry;

	(void)memset(&entry, 0, sizeof(entry));
	entry.cdte_track = track;
	entry.cdte_format = CDROM_MSF;
	if (ioctl(fd, CDROMREADTOCENTRY, &entry) == 0) {
		*min = entry.cdte_addr.msf.minute;
		*seconds = entry.cdte_addr.msf.second;
		*frames = entry.cdte_addr.msf.frame;
	}
}
#endif

#if defined(__linux__)
/*
 * stress_cdrom_ioctl_msf()
 *      tests all CDROM ioctl syscalls that
 *      requires address argument in MSF Format
 */
static void stress_cdrom_ioctl_msf(const int fd)
{
	int starttrk = 0, endtrk = 0;

	(void)fd;

#if defined(CDROMREADTOCHDR) &&	\
    defined(HAVE_CDROM_MSF) &&	\
    defined(HAVE_CDROM_TOCHDR)
	{
		struct cdrom_tochdr header;
		/* Reading the number of tracks on disc */

		(void)memset(&header, 0, sizeof(header));
		if (ioctl(fd, CDROMREADTOCHDR, &header) == 0) {
			starttrk = header.cdth_trk0;
			endtrk = header.cdth_trk1;
		}
	}
#endif

	/* Return if endtrack is not set or starttrk is invalid */
	if ((endtrk == 0) && (starttrk != 0)) {
		return;
	}

#if defined(CDROMPLAYTRKIND) &&	\
    defined(HAVE_CDROM_TI) &&	\
    defined(CDROMPAUSE)
	{
		struct cdrom_ti ti;

		(void)memset(&ti, 0, sizeof(ti));
		ti.cdti_trk1 = endtrk;
		if (ioctl(fd, CDROMPLAYTRKIND, &ti) == 0) {
			(void)ioctl(fd, CDROMPAUSE, 0);
		}
	}
#endif

#if defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_MSF) &&		\
    defined(HAVE_CDROM_TOCENTRY)
	{
		struct cdrom_msf msf;

		/* Fetch address of start and end track in MSF format */
		(void)memset(&msf, 0, sizeof(msf));
		cdrom_get_address_msf(fd, starttrk, &msf.cdmsf_min0,
			&msf.cdmsf_sec0, &msf.cdmsf_frame0);
		cdrom_get_address_msf(fd, endtrk, &msf.cdmsf_min1,
			&msf.cdmsf_sec1, &msf.cdmsf_frame1);

#if defined(CDROMPLAYMSF) && 	\
    defined(CDROMPAUSE)
		if (ioctl(fd, CDROMPLAYMSF, &msf) == 0) {
			(void)ioctl(fd, CDROMPAUSE, 0);
		}
#endif

#if defined(CDROMREADRAW) &&	\
    defined(CD_FRAMESIZE_RAW)
		{
			union {
				struct cdrom_msf msf;		/* input */
				char buffer[CD_FRAMESIZE_RAW];	/* return */
			} arg;

			arg.msf = msf;
			(void)ioctl(fd, CDROMREADRAW, &arg);
		}
#endif

#if defined(CDROMREADMODE1) &&	\
    defined(CD_FRAMESIZE)
		{
			union {
				struct cdrom_msf msf;		/* input */
				char buffer[CD_FRAMESIZE];	/* return */
			} arg;

			arg.msf = msf;
			(void)ioctl(fd, CDROMREADMODE1, &arg);
		}
#endif

#if defined(CDROMREADMODE2) &&	\
    defined(CD_FRAMESIZE_RAW0)
		{
			union {
				struct cdrom_msf msf;		/* input */
				char buffer[CD_FRAMESIZE_RAW0];	/* return */
			} arg;

			arg.msf = msf;
			(void)ioctl(fd, CDROMREADMODE2, &arg);
		}
#endif
	}
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_cdrom_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

	stress_cdrom_ioctl_msf(fd);

#if defined(CDROM_GET_MCN) &&	\
    defined(HAVE_CDROM_MCN)
	{
		struct cdrom_mcn mcn;
		int ret;

		(void)memset(&mcn, 0, sizeof(mcn));
		ret = ioctl(fd, CDROM_GET_MCN, &mcn);
		(void)ret;
	}
#endif
#if defined(CDROMREADTOCHDR) &&		\
    defined(HAVE_CDROM_TOCHDR)
	{
		struct cdrom_tochdr header;
		int ret;

		(void)memset(&header, 0, sizeof(header));
		ret = ioctl(fd, CDROMREADTOCHDR, &header);
		(void)ret;
	}
#endif
#if defined(CDROMREADTOCENTRY) &&	\
    defined(HAVE_CDROM_TOCENTRY)
	{
		struct cdrom_tocentry entry;
		int ret;

		(void)memset(&entry, 0, sizeof(entry));
		ret = ioctl(fd, CDROMREADTOCENTRY, &entry);
		(void)ret;
	}
#endif
#if defined(CDROMVOLREAD) &&	\
    defined(HAVE_CDROM_VOLCTRL)
	{
		struct cdrom_volctrl volume;
		int ret;

		(void)memset(&volume, 0, sizeof(volume));
		ret = ioctl(fd, CDROMVOLREAD, &volume);
#if defined(CDROMVOLCTRL)
		if (ret == 0) {
			(void)ioctl(fd, CDROMVOLCTRL, &volume);
		}
#endif
	}
#endif
#if defined(CDROMSUBCHNL) &&	\
    defined(HAVE_CDROM_SUBCHNL)
	{
		struct cdrom_subchnl q;
		int ret;

		(void)memset(&q, 0, sizeof(q));
		ret = ioctl(fd, CDROMSUBCHNL, &q);
		(void)ret;
	}
#endif
#if defined(CDROMREADAUDIO) &&	\
    defined(HAVE_CDROM_READ_AUDIO)
	{
		struct cdrom_read_audio ra;
		int ret;

		(void)memset(&ra, 0, sizeof(ra));
		ret = ioctl(fd, CDROMREADAUDIO, &ra);
		(void)ret;
	}
#endif
#if defined(CDROMREADCOOKED) &&	\
    defined(CD_FRAMESIZE)
	{
		uint8_t buffer[CD_FRAMESIZE];
		int ret;

		(void)memset(&buffer, 0, sizeof(buffer));
		ret = ioctl(fd, CDROMREADCOOKED, buffer);
		(void)ret;
	}
#endif
#if defined(CDROMREADALL) &&	\
    defined(CD_FRAMESIZE)
	{
		uint8_t buffer[CD_FRAMESIZE];
		int ret;

		(void)memset(&buffer, 0, sizeof(buffer));
		ret = ioctl(fd, CDROMREADALL, buffer);
		(void)ret;
	}
#endif
#if defined(CDROMSEEK) &&	\
    defined(HAVE_CDROM_MSF)
	{
		struct cdrom_msf msf;
		int ret;

		(void)memset(&msf, 0, sizeof(msf));
		ret = ioctl(fd, CDROMSEEK, &msf);
		(void)ret;
	}
#endif
#if defined(CDROMGETSPINDOWN)
	{
		char spindown;
		int ret;

		ret = ioctl(fd, CDROMGETSPINDOWN, &spindown);
		(void)ret;
	}
#endif
#if defined(CDROM_DISC_STATUS)
	{
		int ret;

		ret = ioctl(fd, CDROM_DISC_STATUS, 0);
		(void)ret;
	}
#endif
#if defined(CDROM_GET_CAPABILITY)
	{
		int ret;

		ret = ioctl(fd, CDROM_GET_CAPABILITY, 0);
		(void)ret;
	}
#endif
#if defined(CDROM_CHANGER_NSLOTS)
	{
		int ret;

		ret = ioctl(fd, CDROM_CHANGER_NSLOTS, 0);
		(void)ret;
	}
#endif
#if defined(CDROM_NEXT_WRITABLE)
	{
		int ret;
		long next;

		ret = ioctl(fd, CDROM_NEXT_WRITABLE, &next);
		(void)ret;
	}
#endif
#if defined(CDROM_LAST_WRITTEN)
	{
		int ret;
		long last;

		ret = ioctl(fd, CDROM_LAST_WRITTEN, &last);
		(void)ret;
	}
#endif
#if defined(CDROM_MEDIA_CHANGED)
	{
		int ret, slot = 0;

		ret = ioctl(fd, CDROM_MEDIA_CHANGED, slot);
		(void)ret;

#if defined(CDSL_NONE)
		slot = CDSL_NONE;
		ret = ioctl(fd, CDROM_MEDIA_CHANGED, slot);
		(void)ret;
#endif
#if defined(CDSL_CURRENT)
		slot = CDSL_CURRENT;
		ret = ioctl(fd, CDROM_MEDIA_CHANGED, slot);
		(void)ret;
#endif
	}
#endif
#if defined(CDROMPAUSE)
	{
		int ret;

		ret = ioctl(fd, CDROMPAUSE, 0);
		(void)ret;
	}
#endif
#if defined(CDROMRESUME)
	{
		int ret;

		ret = ioctl(fd, CDROMRESUME, 0);
		(void)ret;
	}
#endif
#if defined(CDROM_DRIVE_STATUS)
	{
		int ret, slot = 0;

		ret = ioctl(fd, CDROM_DRIVE_STATUS, slot);
		(void)ret;

#if defined(CDSL_NONE)
		slot = CDSL_NONE;
		ret = ioctl(fd, CDROM_DRIVE_STATUS, slot);
		(void)ret;
#endif
#if defined(CDSL_CURRENT)
		slot = CDSL_CURRENT;
		ret = ioctl(fd, CDROM_DRIVE_STATUS, slot);
		(void)ret;
#endif
	}
#endif
#if defined(DVD_READ_STRUCT) &&	\
    defined(HAVE_DVD_STRUCT)
	{
		dvd_struct s;

		/*
		 *  Invalid DVD_READ_STRUCT ioctl syscall with
		 *  invalid layer number resulting in EINVAL
		 */
		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_PHYSICAL;
		s.physical.layer_num = UINT8_MAX;
		(void)ioctl(fd, DVD_READ_STRUCT, &s);

		/*
		 *  Exercise each DVD structure type to cover all the
		 *  respective functions to increase kernel coverage
		 */
		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_PHYSICAL;
		(void)ioctl(fd, DVD_READ_STRUCT, &s);

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_COPYRIGHT;
		(void)ioctl(fd, DVD_READ_STRUCT, &s);

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_DISCKEY;
		(void)ioctl(fd, DVD_READ_STRUCT, &s);

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_BCA;
		(void)ioctl(fd, DVD_READ_STRUCT, &s);

		(void)memset(&s, 0, sizeof(s));
		s.type = DVD_STRUCT_MANUFACT;
		(void)ioctl(fd, DVD_READ_STRUCT, &s);

		/* Invalid DVD_READ_STRUCT call with invalid type argument */
		(void)memset(&s, 0, sizeof(s));
		s.type = UINT8_MAX;
		(void)ioctl(fd, DVD_READ_STRUCT, &s);
	}
#endif
#if defined(CDROMAUDIOBUFSIZ)
	{
		int val = INT_MIN, ret;

		/* Invalid CDROMAUDIOBUFSIZ call with negative buffer size */
		ret = ioctl(fd, CDROMAUDIOBUFSIZ, val);
		(void)ret;
	}
#endif
#if defined(DVD_AUTH) &&	\
    defined(HAVE_DVD_AUTHINFO)
	{
		int ret;
		dvd_authinfo ai;

		/* Invalid DVD_AUTH call with no credentials */
		(void)memset(&ai, 0, sizeof(ai));
		ret = ioctl(fd, DVD_AUTH, &ai);
		(void)ret;

		/*
		 *  Exercise each DVD AUTH type to cover all the
		 *  respective code to increase kernel coverage
		 */
		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_AGID;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_KEY1;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_CHALLENGE;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_TITLE_KEY;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_ASF;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_CHALLENGE;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_KEY2;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_INVALIDATE_AGID;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_LU_SEND_RPC_STATE;
		(void)ioctl(fd, DVD_AUTH, &ai);

		(void)memset(&ai, 0, sizeof(ai));
		ai.type = DVD_HOST_SEND_RPC_STATE;
		(void)ioctl(fd, DVD_AUTH, &ai);

		/* Invalid DVD_READ_STRUCT call with invalid type argument */
		(void)memset(&ai, 0, sizeof(ai));
		ai.type = ~0;
		(void)ioctl(fd, DVD_AUTH, &ai);
	}
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_console_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(KDGETLED)
	{
		char argp;
		int ret;

		ret = ioctl(fd, KDGETLED, &argp);

#if defined(KDSETLED)
		if (ret == 0) {
			const char bad_val = ~0;

			(void)ioctl(fd, KDSETLED, &argp);

			/* Exercise Invalid KDSETLED ioctl call with invalid flags */
			if (ioctl(fd, KDSETLED, &bad_val) == 0) {
				/* Unexpected success, so set it back */
				(void)ioctl(fd, KDSETLED, &argp);
			}
		}
#endif

	}
#endif

#if defined(KDGKBLED)
	{
		char argp;
		int ret;

		ret = ioctl(fd, KDGKBLED, &argp);

#if defined(KDSKBLED)
		if (ret == 0) {
			unsigned long bad_val = ~0, val;

			val = (unsigned long)argp;
			(void)ioctl(fd, KDSKBLED, val);

			/* Exercise Invalid KDSKBLED ioctl call with invalid flags */
			if (ioctl(fd, KDSKBLED, bad_val) == 0) {
				/* Unexpected success, so set it back */
				(void)ioctl(fd, KDSKBLED, val);
			}
		}
#endif

	}
#endif

#if defined(KDGETMODE)
	{
		int ret;
		unsigned long argp = 0;

		ret = ioctl(fd, KDGETMODE, &argp);

#if defined(KDSETMODE)
		if (ret == 0) {
			unsigned long bad_val = ~0;

			(void)ioctl(fd, KDSETMODE, argp);

			/* Exercise Invalid KDSETMODE ioctl call with invalid flags */
			if (ioctl(fd, KDSETMODE, bad_val) == 0) {
				/* Unexpected success, so set it back */
				(void)ioctl(fd, KDSETMODE, argp);
			}
		}
#endif

#if defined(KDGKBTYPE)
		{
			int val = 0;

			(void)ioctl(fd, KDGKBTYPE, &val);
		}
#endif

	}
#endif
}
#endif

#if defined(__linux__)
static void stress_dev_kmsg_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, true);
}
#endif

#if defined(__linux__)
static void stress_dev_nvram_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)devpath;

	stress_dev_mem_mmap_linux(fd, true);
}
#endif

#if defined(HAVE_LINUX_HPET_H)
static void stress_dev_hpet_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;

#if defined(HPET_INFO)
	{
		struct hpet_info info;
		int ret;

		ret = ioctl(fd, HPET_INFO, &info);
		(void)ret;
	}
#endif
#if defined(HPET_IRQFREQ)
	{
		unsigned long freq;
		int ret;

		ret = ioctl(fd, HPET_IRQFREQ, &freq);
		(void)ret;
	}
#endif
#if defined(CDROMMULTISESSION)
	{
		int ret;
		struct cdrom_multisession ms_info;

		/*
		 *  Invalid CDROMMULTISESSION ioctl syscall with
		 *  invalid format number resulting in EINVAL
		 */
		(void)memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = UINT8_MAX;
		ret = ioctl(fd, CDROMMULTISESSION, &ms_info);
		(void)ret;

		/* Valid CDROMMULTISESSION with address formats */
		(void)memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = CDROM_MSF;
		ret = ioctl(fd, CDROMMULTISESSION, &ms_info);
		(void)ret;

		(void)memset(&ms_info, 0, sizeof(ms_info));
		ms_info.addr_format = CDROM_LBA;
		ret = ioctl(fd, CDROMMULTISESSION, &ms_info);
		(void)ret;
	}
#endif
}
#endif

#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
static void stress_dev_port_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	off_t off;
	uint8_t *ptr;
	const size_t page_size = stress_get_pagesize();

	(void)name;
	(void)devpath;

	/* seek and read port 0x80 */
	off = lseek(fd, (off_t)0x80, SEEK_SET);
	if (off == 0) {
		char data[1];
		ssize_t ret;

		ret = read(fd, data, sizeof(data));
		(void)ret;
	}

	/* Should fail */
	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, page_size);
}
#endif

#if defined(HAVE_LINUX_HDREG_H)
static void stress_dev_hd_linux_ioctl_long(int fd, int cmd)
{
	long val;
	int ret;

	ret = ioctl(fd, cmd, &val);
	(void)ret;
}

/*
 *  stress_dev_hd_linux()
 *	Linux HDIO ioctls
 */
static void stress_dev_hd_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)devpath;

#if defined(HDIO_GETGEO)
	{
		struct hd_geometry geom;
		int ret;

		ret = ioctl(fd, HDIO_GETGEO, &geom);
		(void)ret;
	}
#endif

#if defined(HDIO_GET_UNMASKINTR)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_UNMASKINTR);
#endif

#if defined(HDIO_GET_MULTCOUNT)
	{
		int val, ret;

		ret = ioctl(fd, HDIO_GET_MULTCOUNT, &val);
		(void)ret;
	}
#endif

#if defined(HDIO_GET_IDENTITY)
	{
		unsigned char identity[512];
		int ret;

		ret = ioctl(fd, HDIO_GET_IDENTITY, identity);
		(void)ret;
	}
#endif

#if defined(HDIO_GET_KEEPSETTINGS)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_KEEPSETTINGS);
#endif

#if defined(HDIO_GET_32BIT)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_32BIT);
#endif

#if defined(HDIO_GET_NOWERR)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_NOWERR);
#endif

#if defined(HDIO_GET_DMA)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_DMA);
#endif

#if defined(HDIO_GET_NICE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_NICE);
#endif

#if defined(HDIO_GET_WCACHE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_WCACHE);
#endif

#if defined(HDIO_GET_ACOUSTIC)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_ACOUSTIC);
#endif

#if defined(HDIO_GET_ADDRESS)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_ADDRESS);
#endif

#if defined(HDIO_GET_BUSSTATE)
	stress_dev_hd_linux_ioctl_long(fd, HDIO_GET_BUSSTATE);
#endif
}
#endif

static void stress_dev_null_nop(
	const char *name,
	const int fd,
	const char *devpath)
{
	(void)name;
	(void)fd;
	(void)devpath;
}

/*
 *  stress_dev_ptp_linux()
 *	minor exercising of the PTP device
 */
static void stress_dev_ptp_linux(
	const char *name,
	const int fd,
	const char *devpath)
{
#if defined(HAVE_LINUX_PTP_CLOCK_H) &&	\
    defined(PTP_CLOCK_GETCAPS) &&	\
    defined(PTP_PIN_GETFUNC)
	int ret;
	struct ptp_clock_caps caps;

	(void)name;
	(void)devpath;

	errno = 0;
	ret = ioctl(fd, PTP_CLOCK_GETCAPS, &caps);
	if (ret == 0) {
		int i, pins = caps.n_pins;

		for (i = 0; i < pins; i++) {
			struct ptp_pin_desc desc;

			(void)memset(&desc, 0, sizeof(desc));
			desc.index = i;
			ret = ioctl(fd, PTP_PIN_GETFUNC, &desc);
			(void)ret;
		}
	}
#else
	(void)name;
	(void)fd;
	(void)devpath;
#endif
}

#define DEV_FUNC(dev, func) \
	{ dev, sizeof(dev) - 1, func }

static const stress_dev_func_t dev_funcs[] = {
#if defined(__linux__) &&		\
    defined(HAVE_LINUX_MEDIA_H) &&	\
    defined(MEDIA_IOC_DEVICE_INFO)
	DEV_FUNC("/dev/media",	stress_dev_media_linux),
#endif
#if defined(HAVE_LINUX_VT_H)
	DEV_FUNC("/dev/vcs",	stress_dev_vcs_linux),
#endif
#if defined(HAVE_LINUX_DM_IOCTL_H)
	DEV_FUNC("/dev/dm",	stress_dev_dm_linux),
#endif
#if defined(HAVE_LINUX_VIDEODEV2_H)
	DEV_FUNC("/dev/video",	stress_dev_video_linux),
#endif
#if defined(HAVE_LINUX_RANDOM_H)
	DEV_FUNC("/dev/random",	stress_dev_random_linux),
#endif
#if defined(__linux__)
	DEV_FUNC("/dev/mem",	stress_dev_mem_linux),
	DEV_FUNC("/dev/kmem",	stress_dev_kmem_linux),
	DEV_FUNC("/dev/kmsg",	stress_dev_kmsg_linux),
	DEV_FUNC("/dev/nvram",	stress_dev_nvram_linux),
	DEV_FUNC("/dev/cdrom",  stress_dev_cdrom_linux),
	DEV_FUNC("/dev/sr0",  stress_dev_cdrom_linux),
	DEV_FUNC("/dev/console",  stress_dev_console_linux),
#endif
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
	DEV_FUNC("/dev/port",	stress_dev_port_linux),
#endif
#if defined(HAVE_LINUX_HPET_H)
	DEV_FUNC("/dev/hpet",	stress_dev_hpet_linux),
#endif
	DEV_FUNC("/dev/null",	stress_dev_null_nop),
	DEV_FUNC("/dev/ptp",	stress_dev_ptp_linux)
};

/*
 *  stress_dev_rw()
 *	exercise a dev entry
 */
static inline void stress_dev_rw(
	const stress_args_t *args,
	int32_t loops)
{
	int fd, ret;
	off_t offset;
	struct stat buf;
	struct pollfd fds[1];
	fd_set rfds;
	void *ptr;
	size_t i;
	char path[PATH_MAX];
	const double threshold = 0.25;

	while (loops == -1 || loops > 0) {
		double t_start;
		bool timeout = false;
#if defined(HAVE_TERMIOS_H) &&	\
    defined(TCGETS)
		struct termios tios;
#endif
		ret = shim_pthread_spin_lock(&lock);
		if (ret)
			return;
		(void)shim_strlcpy(path, dev_path, sizeof(path));
		(void)shim_pthread_spin_unlock(&lock);

		if (!*path || !keep_stressing_flag())
			break;

		t_start = stress_time_now();


		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0) {
			if (errno == EINTR)
				goto next;
			goto rdwr;
		}

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		if (fstat(fd, &buf) < 0) {
			pr_fail("%s: stat failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
			if ((S_ISBLK(buf.st_mode) | (S_ISCHR(buf.st_mode))) == 0) {
				(void)close(fd);
				goto next;
			}
		}

		if (S_ISBLK(buf.st_mode)) {
			stress_dev_blk(args->name, fd, path);
			stress_dev_scsi_blk(args->name, fd, path);
#if defined(HAVE_LINUX_HDREG_H)
			stress_dev_hd_linux(args->name, fd, path);
#endif
		}
#if defined(HAVE_TERMIOS_H) &&	\
    defined(TCGETS)
		if (S_ISCHR(buf.st_mode) &&
		    strncmp("/dev/vsock", path, 9) &&
		    strncmp("/dev/dri", path, 7) &&
		    (ioctl(fd, TCGETS, &tios) == 0))
			stress_dev_tty(args->name, fd, path);
#endif

		offset = lseek(fd, 0, SEEK_SET);
		stress_uint64_put((uint64_t)offset);
		offset = lseek(fd, 0, SEEK_CUR);
		stress_uint64_put((uint64_t)offset);
		offset = lseek(fd, 0, SEEK_END);
		stress_uint64_put((uint64_t)offset);

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

		FD_ZERO(&rfds);
		fds[0].fd = fd;
		fds[0].events = POLLIN;
		ret = poll(fds, 1, 0);
		(void)ret;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}

#if !defined(__NetBSD__)
		{
			struct timeval tv;
			fd_set wfds;

			FD_ZERO(&rfds);
			FD_SET(fd, &rfds);
			FD_ZERO(&wfds);
			FD_SET(fd, &wfds);
			tv.tv_sec = 0;
			tv.tv_usec = 10000;
			ret = select(fd + 1, &rfds, &wfds, NULL, &tv);
			(void)ret;

			if (stress_time_now() - t_start > threshold) {
				timeout = true;
				(void)close(fd);
				goto next;
			}
		}
#endif

#if defined(F_GETFD)
		ret = fcntl(fd, F_GETFD, NULL);
		(void)ret;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
#endif
#if defined(F_GETFL)
		ret = fcntl(fd, F_GETFL, NULL);
		(void)ret;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
#endif
#if defined(F_GETSIG)
		ret = fcntl(fd, F_GETSIG, NULL);
		(void)ret;

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			(void)close(fd);
			goto next;
		}
#endif
		ptr = mmap(NULL, args->page_size, PROT_READ, MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);
		ptr = mmap(NULL, args->page_size, PROT_READ, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);
		(void)close(fd);

		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}

		if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0) {
			if (errno == EINTR)
				goto next;
			goto rdwr;
		}
		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_PRIVATE, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);
		ptr = mmap(NULL, args->page_size, PROT_WRITE, MAP_SHARED, fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, args->page_size);

		ret = shim_fsync(fd);
		(void)ret;

		for (i = 0; i < SIZEOF_ARRAY(dev_funcs); i++) {
			if (!strncmp(path, dev_funcs[i].devpath, dev_funcs[i].devpath_len))
				dev_funcs[i].func(args->name, fd, path);
		}
		(void)close(fd);
		if (stress_time_now() - t_start > threshold) {
			timeout = true;
			goto next;
		}
rdwr:
		/*
		 *   O_RDONLY | O_WRONLY allows one to
		 *   use the fd for ioctl() only operations
		 */
		fd = open(path, O_RDONLY | O_WRONLY | O_NONBLOCK);
		if (fd >= 0)
			(void)close(fd);

next:
		if (loops > 0) {
			if (timeout)
				break;
			loops--;
		}
	}
}

/*
 *  stress_dev_thread
 *	keep exercising a /dev entry until
 *	controlling thread triggers an exit
 */
static void *stress_dev_thread(void *arg)
{
	static void *nowt = NULL;
	const stress_pthread_args_t *pa = (stress_pthread_args_t *)arg;
	const stress_args_t *args = pa->args;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (keep_stressing_flag())
		stress_dev_rw(args, -1);

	return &nowt;
}

/*
 *  stress_dev_dir()
 *	read directory
 */
static void stress_dev_dir(
	const stress_args_t *args,
	const char *path,
	const bool recurse,
	const int depth,
	const uid_t euid)
{
	struct dirent **dlist;
	const mode_t flags = S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
	int32_t loops = args->instance < 8 ? args->instance + 1 : 8;
	int i, n;

	if (!keep_stressing_flag())
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	dlist = NULL;
	n = scandir(path, &dlist, NULL, mixup_sort);
	if (n <= 0)
		goto done;

	for (i = 0; i < n; i++) {
		int ret;
		struct stat buf;
		char filename[PATH_MAX];
		char tmp[PATH_MAX];
		struct dirent *d = dlist[i];
		size_t len;

		if (!keep_stressing())
			break;
		if (stress_is_dot_filename(d->d_name))
			continue;

		/*
		 * Xen clients hang on hpet when running as root
		 * see: LP#1741409, so avoid opening /dev/hpet
		 */
		if (!euid && !strcmp(d->d_name, "hpet"))
			continue;
		len = strlen(d->d_name);

		/*
		 *  Exercise no more than 3 of the same device
		 *  driver, e.g. ttyS0..ttyS2
		 */
		if (len > 1) {
			int dev_n;
			char *ptr = d->d_name + len - 1;

			while (ptr > d->d_name && isdigit((int)*ptr))
				ptr--;
			ptr++;
			dev_n = atoi(ptr);
			if (dev_n > 2)
				continue;
		}

		(void)snprintf(tmp, sizeof(tmp), "%s/%s", path, d->d_name);
		switch (d->d_type) {
		case DT_DIR:
			if (!recurse)
				continue;
			if (stress_hash_get(dev_hash_table, tmp))
				continue;
			ret = stat(tmp, &buf);
			if (ret < 0) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			if ((buf.st_mode & flags) == 0) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			inc_counter(args);
			stress_dev_dir(args, tmp, recurse, depth + 1, euid);
			break;
		case DT_BLK:
		case DT_CHR:
			if (stress_hash_get(dev_hash_table, tmp))
				continue;
			if (strstr(tmp, "watchdog")) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			if (stress_try_open(args, tmp, O_RDONLY | O_NONBLOCK, 1500000000)) {
				stress_hash_add(dev_hash_table, tmp);
				continue;
			}
			ret = shim_pthread_spin_lock(&lock);
			if (!ret) {
				(void)shim_strlcpy(filename, tmp, sizeof(filename));
				dev_path = filename;
				(void)shim_pthread_spin_unlock(&lock);
				stress_dev_rw(args, loops);
				inc_counter(args);
			}
			break;
		default:
			break;
		}
	}
done:
	stress_dirent_list_free(dlist, n);
}

/*
 *  stress_dev
 *	stress reading all of /dev
 */
static int stress_dev(const stress_args_t *args)
{
	pthread_t pthreads[MAX_DEV_THREADS];
	int ret[MAX_DEV_THREADS], rc = EXIT_SUCCESS;
	uid_t euid = geteuid();
	stress_pthread_args_t pa;

	dev_path = "/dev/null";
	pa.args = args;
	pa.data = NULL;

	(void)memset(ret, 0, sizeof(ret));

	do {
		pid_t pid;

again:
		if (!keep_stressing())
			break;
		pid = fork();
		if (pid < 0) {
			if ((errno == EAGAIN) || (errno == ENOMEM))
				goto again;
		} else if (pid > 0) {
			int status, wret;

			(void)setpgid(pid, g_pgrp);
			/* Parent, wait for child */
			wret = shim_waitpid(pid, &status, 0);
			if (wret < 0) {
				if (errno != EINTR)
					pr_dbg("%s: waitpid(): errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
				(void)shim_waitpid(pid, &status, 0);
			} else {
				if (WIFEXITED(status) &&
				    WEXITSTATUS(status) != 0) {
					rc = EXIT_FAILURE;
					break;
				}
			}
		} else if (pid == 0) {
			size_t i;
			int r;

			dev_hash_table = stress_hash_create(251);
			if (!dev_hash_table) {
				pr_err("%s: cannot create device hash table: %d (%s))\n",
					args->name, errno, strerror(errno));
				_exit(EXIT_NO_RESOURCE);
			}
			scsi_hash_table = stress_hash_create(251);
			if (!scsi_hash_table) {
				pr_err("%s: cannot create scsi device hash table: %d (%s))\n",
					args->name, errno, strerror(errno));
				stress_hash_delete(dev_hash_table);
				_exit(EXIT_NO_RESOURCE);
			}

			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
			rc = shim_pthread_spin_init(&lock, SHIM_PTHREAD_PROCESS_SHARED);
			if (rc) {
				pr_inf("%s: pthread_spin_init failed, errno=%d (%s)\n",
					args->name, rc, strerror(rc));
				_exit(EXIT_NO_RESOURCE);
			}

			/* Make sure this is killable by OOM killer */
			stress_set_oom_adjustment(args->name, true);
			mixup = stress_mwc32();

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				ret[i] = pthread_create(&pthreads[i], NULL,
						stress_dev_thread, (void *)&pa);
			}

			do {
				stress_dev_dir(args, "/dev", true, 0, euid);
			} while (keep_stressing());

			r = shim_pthread_spin_lock(&lock);
			if (r) {
				pr_dbg("%s: failed to lock spin lock for dev_path\n", args->name);
			} else {
				dev_path = "";
				r = shim_pthread_spin_unlock(&lock);
				(void)r;
			}

			for (i = 0; i < MAX_DEV_THREADS; i++) {
				if (ret[i] == 0)
					(void)pthread_join(pthreads[i], NULL);
			}
			stress_hash_delete(dev_hash_table);
			_exit(EXIT_SUCCESS);
		}
	} while (keep_stressing());

	(void)shim_pthread_spin_destroy(&lock);

	return rc;
}
stressor_info_t stress_dev_info = {
	.stressor = stress_dev,
	.class = CLASS_DEV | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_dev_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_DEV | CLASS_OS,
	.help = help
};
#endif
