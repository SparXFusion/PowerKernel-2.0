#include <linux/suspend.h>
#include <linux/suspend_ioctls.h>
#include <linux/utsname.h>
#include <linux/freezer.h>

#if defined(CONFIG_CPU_FREQ) && defined(CONFIG_ARCH_EXYNOS4)
#define CONFIG_DVFS_LIMIT
#endif

struct swsusp_info {
	struct new_utsname	uts;
	u32			version_code;
	unsigned long		num_physpages;
	int			cpus;
	unsigned long		image_pages;
	unsigned long		pages;
	unsigned long		size;
} __attribute__((aligned(PAGE_SIZE)));

#ifdef CONFIG_HIBERNATION
/* kernel/power/snapshot.c */
extern void __init hibernate_reserved_size_init(void);
extern void __init hibernate_image_size_init(void);

#ifdef CONFIG_ARCH_HIBERNATION_HEADER
/* Maximum size of architecture specific data in a hibernation header */
#define MAX_ARCH_HEADER_SIZE	(sizeof(struct new_utsname) + 4)

extern int arch_hibernation_header_save(void *addr, unsigned int max_size);
extern int arch_hibernation_header_restore(void *addr);

static inline int init_header_complete(struct swsusp_info *info)
{
	return arch_hibernation_header_save(info, MAX_ARCH_HEADER_SIZE);
}

static inline char *check_image_kernel(struct swsusp_info *info)
{
	return arch_hibernation_header_restore(info) ?
			"architecture specific data" : NULL;
}
#endif /* CONFIG_ARCH_HIBERNATION_HEADER */

/*
 * Keep some memory free so that I/O operations can succeed without paging
 * [Might this be more than 4 MB?]
 */
#define PAGES_FOR_IO	((4096 * 1024) >> PAGE_SHIFT)

/*
 * Keep 1 MB of memory free so that device drivers can allocate some pages in
 * their .suspend() routines without breaking the suspend to disk.
 */
#define SPARE_PAGES	((1024 * 1024) >> PAGE_SHIFT)

/* kernel/power/hibernate.c */
extern int hibernation_snapshot(int platform_mode);
extern int hibernation_restore(int platform_mode);
extern int hibernation_platform_enter(void);

#else /* !CONFIG_HIBERNATION */

static inline void hibernate_reserved_size_init(void) {}
static inline void hibernate_image_size_init(void) {}
#endif /* !CONFIG_HIBERNATION */

extern int pfn_is_nosave(unsigned long);

#define power_attr(_name) \
static struct kobj_attribute _name##_attr = {	\
	.attr	= {				\
		.name = __stringify(_name),	\
		.mode = 0644,			\
	},					\
	.show	= _name##_show,			\
	.store	= _name##_store,		\
}

extern int noresume;
/* Preferred image size in bytes (default 500 MB) */
extern unsigned long image_size;
/* Size of memory reserved for drivers (default SPARE_PAGES x PAGE_SIZE) */
extern unsigned long reserved_size;
extern int in_suspend;
extern dev_t swsusp_resume_device;
extern sector_t swsusp_resume_block;

extern asmlinkage int swsusp_arch_suspend(void);
extern asmlinkage int swsusp_arch_resume(void);

extern int create_basic_memory_bitmaps(void);
extern void free_basic_memory_bitmaps(void);
extern int hibernate_preallocate_memory(void);

/**
 *	Auxiliary structure used for reading the snapshot image data and
 *	metadata from and writing them to the list of page backup entries
 *	(PBEs) which is the main data structure of swsusp.
 *
 *	Using struct snapshot_handle we can transfer the image, including its
 *	metadata, as a continuous sequence of bytes with the help of
 *	snapshot_read_next() and snapshot_write_next().
 *
 *	The code that writes the image to a storage or transfers it to
 *	the user land is required to use snapshot_read_next() for this
 *	purpose and it should not make any assumptions regarding the internal
 *	structure of the image.  Similarly, the code that reads the image from
 *	a storage or transfers it from the user land is required to use
 *	snapshot_write_next().
 *
 *	This may allow us to change the internal structure of the image
 *	in the future with considerably less effort.
 */

struct snapshot_handle {
	unsigned int	cur;	/* number of the block of PAGE_SIZE bytes the
				 * next operation will refer to (ie. current)
				 */
	void		*buffer;	/* address of the block to read from
					 * or write to
					 */
	int		sync_read;	/* Set to one to notify the caller of
					 * snapshot_write_next() that it may
					 * need to call wait_on_bio_chain()
					 */
};

/* This macro returns the address from/to which the caller of
 * snapshot_read_next()/snapshot_write_next() is allowed to
 * read/write data after the function returns
 */
#define data_of(handle)	((handle).buffer)

extern unsigned int snapshot_additional_pages(struct zone *zone);
extern unsigned long snapshot_get_image_size(void);
extern int snapshot_read_next(struct snapshot_handle *handle);
extern int snapshot_write_next(struct snapshot_handle *handle);
extern void snapshot_write_finalize(struct snapshot_handle *handle);
extern int snapshot_image_loaded(struct snapshot_handle *handle);

/* If unset, the snapshot device cannot be open. */
extern atomic_t snapshot_device_available;

extern sector_t alloc_swapdev_block(int swap);
extern void free_all_swap_pages(int swap);
extern int swsusp_swap_in_use(void);

/*
 * Flags that can be passed from the hibernatig hernel to the "boot" kernel in
 * the image header.
 */
#define SF_PLATFORM_MODE	1
#define SF_NOCOMPRESS_MODE	2

/* kernel/power/hibernate.c */
extern int swsusp_check(void);
extern void swsusp_free(void);
extern int swsusp_read(unsigned int *flags_p);
extern int swsusp_write(unsigned int flags);
extern void swsusp_close(fmode_t);

/* kernel/power/block_io.c */
extern struct block_device *hib_resume_bdev;

extern int hib_bio_read_page(pgoff_t page_off, void *addr,
		struct bio **bio_chain);
extern int hib_bio_write_page(pgoff_t page_off, void *addr,
		struct bio **bio_chain);
extern int hib_wait_on_bio_chain(struct bio **bio_chain);

struct timeval;
/* kernel/power/swsusp.c */
extern void swsusp_show_speed(struct timeval *, struct timeval *,
				unsigned int, char *);

#ifdef CONFIG_SUSPEND
/* kernel/power/suspend.c */
extern const char *const pm_states[];

extern bool valid_state(suspend_state_t state);
extern int suspend_devices_and_enter(suspend_state_t state);
extern int enter_state(suspend_state_t state);
#else /* !CONFIG_SUSPEND */
static inline int suspend_devices_and_enter(suspend_state_t state)
{
	return -ENOSYS;
}
static inline int enter_state(suspend_state_t state) { return -ENOSYS; }
static inline bool valid_state(suspend_state_t state) { return false; }
#endif /* !CONFIG_SUSPEND */

#ifdef CONFIG_PM_TEST_SUSPEND
/* kernel/power/suspend_test.c */
extern void suspend_test_start(void);
extern void suspend_test_finish(const char *label);
#else /* !CONFIG_PM_TEST_SUSPEND */
static inline void suspend_test_start(void) {}
static inline void suspend_test_finish(const char *label) {}
#endif /* !CONFIG_PM_TEST_SUSPEND */

#ifdef CONFIG_PM_SLEEP
/* kernel/power/main.c */
extern int pm_notifier_call_chain(unsigned long val);
#endif

#ifdef CONFIG_HIGHMEM
int restore_highmem(void);
#else
static inline unsigned int count_highmem_pages(void) { return 0; }
static inline int restore_highmem(void) { return 0; }
#endif

/*
 * Suspend test levels
 */
enum {
	/* keep first */
	TEST_NONE,
	TEST_CORE,
	TEST_CPUS,
	TEST_PLATFORM,
	TEST_DEVICES,
	TEST_FREEZER,
	/* keep last */
	__TEST_AFTER_LAST
};

#define TEST_FIRST	TEST_NONE
#define TEST_MAX	(__TEST_AFTER_LAST - 1)

extern int pm_test_level;

#ifdef CONFIG_SUSPEND_FREEZER
static inline int suspend_freeze_processes(void)
{
	int error;

	error = freeze_processes();

	/*
	 * freeze_processes() automatically thaws every task if freezing
	 * fails. So we need not do anything extra upon error.
	 */
	if (error)
		goto Finish;

	error = freeze_kernel_threads();

	/*
	 * freeze_kernel_threads() thaws only kernel threads upon freezing
	 * failure. So we have to thaw the userspace tasks ourselves.
	 */
	if (error)
		thaw_processes();

 Finish:
	return error;
}

static inline void suspend_thaw_processes(void)
{
	thaw_processes();
}
#else
static inline int suspend_freeze_processes(void)
{
	return 0;
}

static inline void suspend_thaw_processes(void)
{
}
#endif

#ifdef CONFIG_WAKELOCK
/* kernel/power/wakelock.c */
extern struct workqueue_struct *suspend_work_queue;
extern struct wake_lock main_wake_lock;
extern struct workqueue_struct *sync_work_queue;
extern struct wake_lock sync_wake_lock;
extern suspend_state_t requested_suspend_state;
#endif

#ifdef CONFIG_USER_WAKELOCK
ssize_t wake_lock_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
ssize_t wake_lock_store(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
ssize_t wake_unlock_show(struct kobject *kobj, struct kobj_attribute *attr,
			char *buf);
ssize_t  wake_unlock_store(struct kobject *kobj, struct kobj_attribute *attr,
			const char *buf, size_t n);
#endif

#ifdef CONFIG_EARLYSUSPEND
/* kernel/power/earlysuspend.c */
void request_suspend_state(suspend_state_t state);
suspend_state_t get_suspend_state(void);
#endif

struct pm_wd_data {
	struct task_struct *tsk;
	int timeout;
};
#ifdef CONFIG_PM_WATCHDOG_TIMEOUT
void pm_wd_timeout(unsigned long data);
void pm_wd_add_timer(struct timer_list *timer, struct pm_wd_data *data,
			int timeout);
void pm_wd_del_timer(struct timer_list *timer);
#else
static inline void pm_wd_timeout(unsigned long data) { }
static inline void pm_wd_add_timer(struct timer_list *timer,
				struct pm_wd_data *data, int timeout) { }
static inline void pm_wd_del_timer(struct timer_list *timer) { }
#endif

/* Yank555.lu - Make current max limit available globally */
#ifdef CONFIG_DVFS_LIMIT
int get_cpufreq_level(unsigned int freq, unsigned int *level);
extern int cpufreq_max_limit_val;
extern int cpufreq_max_limit_coupled;
#endif
