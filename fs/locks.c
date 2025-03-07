#ifdef CONFIG_GOD_MODE
#include <linux/god_mode.h>
#endif
/*
 *  linux/fs/locks.c
 *
 *  Provide support for fcntl()'s F_GETLK, F_SETLK, and F_SETLKW calls.
 *  Doug Evans (dje@spiff.uucp), August 07, 1992
 *
 *  Deadlock detection added.
 *  FIXME: one thing isn't handled yet:
 *	- mandatory locks (requires lots of changes elsewhere)
 *  Kelly Carmichael (kelly@[142.24.8.65]), September 17, 1994.
 *
 *  Miscellaneous edits, and a total rewrite of posix_lock_file() code.
 *  Kai Petzke (wpp@marie.physik.tu-berlin.de), 1994
 *  
 *  Converted file_lock_table to a linked list from an array, which eliminates
 *  the limits on how many active file locks are open.
 *  Chad Page (pageone@netcom.com), November 27, 1994
 * 
 *  Removed dependency on file descriptors. dup()'ed file descriptors now
 *  get the same locks as the original file descriptors, and a close() on
 *  any file descriptor removes ALL the locks on the file for the current
 *  process. Since locks still depend on the process id, locks are inherited
 *  after an exec() but not after a fork(). This agrees with POSIX, and both
 *  BSD and SVR4 practice.
 *  Andy Walker (andy@lysaker.kvaerner.no), February 14, 1995
 *
 *  Scrapped free list which is redundant now that we allocate locks
 *  dynamically with kmalloc()/kfree().
 *  Andy Walker (andy@lysaker.kvaerner.no), February 21, 1995
 *
 *  Implemented two lock personalities - FL_FLOCK and FL_POSIX.
 *
 *  FL_POSIX locks are created with calls to fcntl() and lockf() through the
 *  fcntl() system call. They have the semantics described above.
 *
 *  FL_FLOCK locks are created with calls to flock(), through the flock()
 *  system call, which is new. Old C libraries implement flock() via fcntl()
 *  and will continue to use the old, broken implementation.
 *
 *  FL_FLOCK locks follow the 4.4 BSD flock() semantics. They are associated
 *  with a file pointer (filp). As a result they can be shared by a parent
 *  process and its children after a fork(). They are removed when the last
 *  file descriptor referring to the file pointer is closed (unless explicitly
 *  unlocked). 
 *
 *  FL_FLOCK locks never deadlock, an existing lock is always removed before
 *  upgrading from shared to exclusive (or vice versa). When this happens
 *  any processes blocked by the current lock are woken up and allowed to
 *  run before the new lock is applied.
 *  Andy Walker (andy@lysaker.kvaerner.no), June 09, 1995
 *
 *  Removed some race conditions in flock_lock_file(), marked other possible
 *  races. Just grep for FIXME to see them. 
 *  Dmitry Gorodchanin (pgmdsg@ibi.com), February 09, 1996.
 *
 *  Addressed Dmitry's concerns. Deadlock checking no longer recursive.
 *  Lock allocation changed to GFP_ATOMIC as we can't afford to sleep
 *  once we've checked for blocking and deadlocking.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 03, 1996.
 *
 *  Initial implementation of mandatory locks. SunOS turned out to be
 *  a rotten model, so I implemented the "obvious" semantics.
 *  See 'Documentation/filesystems/mandatory-locking.txt' for details.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 06, 1996.
 *
 *  Don't allow mandatory locks on mmap()'ed files. Added simple functions to
 *  check if a file has mandatory locks, used by mmap(), open() and creat() to
 *  see if system call should be rejected. Ref. HP-UX/SunOS/Solaris Reference
 *  Manual, Section 2.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 09, 1996.
 *
 *  Tidied up block list handling. Added '/proc/locks' interface.
 *  Andy Walker (andy@lysaker.kvaerner.no), April 24, 1996.
 *
 *  Fixed deadlock condition for pathological code that mixes calls to
 *  flock() and fcntl().
 *  Andy Walker (andy@lysaker.kvaerner.no), April 29, 1996.
 *
 *  Allow only one type of locking scheme (FL_POSIX or FL_FLOCK) to be in use
 *  for a given file at a time. Changed the CONFIG_LOCK_MANDATORY scheme to
 *  guarantee sensible behaviour in the case where file system modules might
 *  be compiled with different options than the kernel itself.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  Added a couple of missing wake_up() calls. Thanks to Thomas Meckel
 *  (Thomas.Meckel@mni.fh-giessen.de) for spotting this.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 15, 1996.
 *
 *  Changed FL_POSIX locks to use the block list in the same way as FL_FLOCK
 *  locks. Changed process synchronisation to avoid dereferencing locks that
 *  have already been freed.
 *  Andy Walker (andy@lysaker.kvaerner.no), Sep 21, 1996.
 *
 *  Made the block list a circular list to minimise searching in the list.
 *  Andy Walker (andy@lysaker.kvaerner.no), Sep 25, 1996.
 *
 *  Made mandatory locking a mount option. Default is not to allow mandatory
 *  locking.
 *  Andy Walker (andy@lysaker.kvaerner.no), Oct 04, 1996.
 *
 *  Some adaptations for NFS support.
 *  Olaf Kirch (okir@monad.swb.de), Dec 1996,
 *
 *  Fixed /proc/locks interface so that we can't overrun the buffer we are handed.
 *  Andy Walker (andy@lysaker.kvaerner.no), May 12, 1997.
 *
 *  Use slab allocator instead of kmalloc/kfree.
 *  Use generic list implementation from <linux/list.h>.
 *  Sped up posix_locks_deadlock by only considering blocked locks.
 *  Matthew Wilcox <willy@debian.org>, March, 2000.
 *
 *  Leases and LOCK_MAND
 *  Matthew Wilcox <willy@debian.org>, June, 2000.
 *  Stephen Rothwell <sfr@canb.auug.org.au>, June, 2000.
 */

#include <linux/capability.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/security.h>
#include <linux/slab.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/rcupdate.h>
#include <linux/pid_namespace.h>

#include <asm/uaccess.h>

#define IS_POSIX(fl)	(fl->fl_flags & FL_POSIX)
#define IS_FLOCK(fl)	(fl->fl_flags & FL_FLOCK)
#define IS_LEASE(fl)	(fl->fl_flags & FL_LEASE)

static bool lease_breaking(struct file_lock *fl)
{
	return fl->fl_flags & (FL_UNLOCK_PENDING | FL_DOWNGRADE_PENDING);
}

static int target_leasetype(struct file_lock *fl)
{
	if (fl->fl_flags & FL_UNLOCK_PENDING)
		return F_UNLCK;
	if (fl->fl_flags & FL_DOWNGRADE_PENDING)
		return F_RDLCK;
	return fl->fl_type;
}

int leases_enable = 1;
int lease_break_time = 45;

#define for_each_lock(inode, lockp) \
	for (lockp = &inode->i_flock; *lockp != NULL; lockp = &(*lockp)->fl_next)

static LIST_HEAD(file_lock_list);
static LIST_HEAD(blocked_list);
static DEFINE_SPINLOCK(file_lock_lock);

/*
 * Protects the two list heads above, plus the inode->i_flock list
 */
void lock_flocks(void)
{
	spin_lock(&file_lock_lock);
}
EXPORT_SYMBOL_GPL(lock_flocks);

void unlock_flocks(void)
{
	spin_unlock(&file_lock_lock);
}
EXPORT_SYMBOL_GPL(unlock_flocks);

static struct kmem_cache *filelock_cache __read_mostly;

static void locks_init_lock_heads(struct file_lock *fl)
{
	INIT_LIST_HEAD(&fl->fl_link);
	INIT_LIST_HEAD(&fl->fl_block);
	init_waitqueue_head(&fl->fl_wait);
}

/* Allocate an empty lock structure. */
struct file_lock *locks_alloc_lock(void)
{
	struct file_lock *fl = kmem_cache_zalloc(filelock_cache, GFP_KERNEL);

	if (fl)
		locks_init_lock_heads(fl);

	return fl;
}
EXPORT_SYMBOL_GPL(locks_alloc_lock);

void locks_release_private(struct file_lock *fl)
{
	if (fl->fl_ops) {
		if (fl->fl_ops->fl_release_private)
			fl->fl_ops->fl_release_private(fl);
		fl->fl_ops = NULL;
	}
	if (fl->fl_lmops) {
		if (fl->fl_lmops->lm_release_private)
			fl->fl_lmops->lm_release_private(fl);
		fl->fl_lmops = NULL;
	}

}
EXPORT_SYMBOL_GPL(locks_release_private);

/* Free a lock which is not in use. */
void locks_free_lock(struct file_lock *fl)
{
	BUG_ON(waitqueue_active(&fl->fl_wait));
	BUG_ON(!list_empty(&fl->fl_block));
	BUG_ON(!list_empty(&fl->fl_link));

	locks_release_private(fl);
	kmem_cache_free(filelock_cache, fl);
}
EXPORT_SYMBOL(locks_free_lock);

void locks_init_lock(struct file_lock *fl)
{
	memset(fl, 0, sizeof(struct file_lock));
	locks_init_lock_heads(fl);
}

EXPORT_SYMBOL(locks_init_lock);

static void locks_copy_private(struct file_lock *new, struct file_lock *fl)
{
	if (fl->fl_ops) {
		if (fl->fl_ops->fl_copy_lock)
			fl->fl_ops->fl_copy_lock(new, fl);
		new->fl_ops = fl->fl_ops;
	}
	if (fl->fl_lmops)
		new->fl_lmops = fl->fl_lmops;
}

/*
 * Initialize a new lock from an existing file_lock structure.
 */
void __locks_copy_lock(struct file_lock *new, const struct file_lock *fl)
{
	new->fl_owner = fl->fl_owner;
	new->fl_pid = fl->fl_pid;
	new->fl_file = NULL;
	new->fl_flags = fl->fl_flags;
	new->fl_type = fl->fl_type;
	new->fl_start = fl->fl_start;
	new->fl_end = fl->fl_end;
	new->fl_ops = NULL;
	new->fl_lmops = NULL;
}
EXPORT_SYMBOL(__locks_copy_lock);

void locks_copy_lock(struct file_lock *new, struct file_lock *fl)
{
	locks_release_private(new);

	__locks_copy_lock(new, fl);
	new->fl_file = fl->fl_file;
	new->fl_ops = fl->fl_ops;
	new->fl_lmops = fl->fl_lmops;

	locks_copy_private(new, fl);
}

EXPORT_SYMBOL(locks_copy_lock);

static inline int flock_translate_cmd(int cmd) {
	if (cmd & LOCK_MAND)
		return cmd & (LOCK_MAND | LOCK_RW);
	switch (cmd) {
	case LOCK_SH:
		return F_RDLCK;
	case LOCK_EX:
		return F_WRLCK;
	case LOCK_UN:
		return F_UNLCK;
	}
	return -EINVAL;
}

/* Fill in a file_lock structure with an appropriate FLOCK lock. */
static int flock_make_lock(struct file *filp, struct file_lock **lock,
		unsigned int cmd)
{
	struct file_lock *fl;
	int type = flock_translate_cmd(cmd);
	if (type < 0)
		return type;
	
	fl = locks_alloc_lock();
	if (fl == NULL)
		return -ENOMEM;

	fl->fl_file = filp;
	fl->fl_pid = current->tgid;
	fl->fl_flags = FL_FLOCK;
	fl->fl_type = type;
	fl->fl_end = OFFSET_MAX;
	
	*lock = fl;
	return 0;
}

static int assign_type(struct file_lock *fl, long type)
{
	switch (type) {
	case F_RDLCK:
	case F_WRLCK:
	case F_UNLCK:
		fl->fl_type = type;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* Verify a "struct flock" and copy it to a "struct file_lock" as a POSIX
 * style lock.
 */
static int flock_to_posix_lock(struct file *filp, struct file_lock *fl,
			       struct flock *l)
{
	off_t start, end;

	switch (l->l_whence) {
	case SEEK_SET:
		start = 0;
		break;
	case SEEK_CUR:
		start = filp->f_pos;
		break;
	case SEEK_END:
		start = i_size_read(filp->f_path.dentry->d_inode);
		break;
	default:
		return -EINVAL;
	}

	/* POSIX-1996 leaves the case l->l_len < 0 undefined;
	   POSIX-2001 defines it. */
	start += l->l_start;
	if (start < 0)
		return -EINVAL;
	fl->fl_end = OFFSET_MAX;
	if (l->l_len > 0) {
		end = start + l->l_len - 1;
		fl->fl_end = end;
	} else if (l->l_len < 0) {
		end = start - 1;
		fl->fl_end = end;
		start += l->l_len;
		if (start < 0)
			return -EINVAL;
	}
	fl->fl_start = start;	/* we record the absolute position */
	if (fl->fl_end < fl->fl_start)
		return -EOVERFLOW;
	
	fl->fl_owner = current->files;
	fl->fl_pid = current->tgid;
	fl->fl_file = filp;
	fl->fl_flags = FL_POSIX;
	fl->fl_ops = NULL;
	fl->fl_lmops = NULL;

	return assign_type(fl, l->l_type);
}

#if BITS_PER_LONG == 32
static int flock64_to_posix_lock(struct file *filp, struct file_lock *fl,
				 struct flock64 *l)
{
	loff_t start;

	switch (l->l_whence) {
	case SEEK_SET:
		start = 0;
		break;
	case SEEK_CUR:
		start = filp->f_pos;
		break;
	case SEEK_END:
		start = i_size_read(filp->f_path.dentry->d_inode);
		break;
	default:
		return -EINVAL;
	}

	start += l->l_start;
	if (start < 0)
		return -EINVAL;
	fl->fl_end = OFFSET_MAX;
	if (l->l_len > 0) {
		fl->fl_end = start + l->l_len - 1;
	} else if (l->l_len < 0) {
		fl->fl_end = start - 1;
		start += l->l_len;
		if (start < 0)
			return -EINVAL;
	}
	fl->fl_start = start;	/* we record the absolute position */
	if (fl->fl_end < fl->fl_start)
		return -EOVERFLOW;
	
	fl->fl_owner = current->files;
	fl->fl_pid = current->tgid;
	fl->fl_file = filp;
	fl->fl_flags = FL_POSIX;
	fl->fl_ops = NULL;
	fl->fl_lmops = NULL;

	return assign_type(fl, l->l_type);
}
#endif

/* default lease lock manager operations */
static void lease_break_callback(struct file_lock *fl)
{
	kill_fasync(&fl->fl_fasync, SIGIO, POLL_MSG);
}

static void lease_release_private_callback(struct file_lock *fl)
{
	if (!fl->fl_file)
		return;

	f_delown(fl->fl_file);
	fl->fl_file->f_owner.signum = 0;
}

static const struct lock_manager_operations lease_manager_ops = {
	.lm_break = lease_break_callback,
	.lm_release_private = lease_release_private_callback,
	.lm_change = lease_modify,
};

/*
 * Initialize a lease, use the default lock manager operations
 */
static int lease_init(struct file *filp, long type, struct file_lock *fl)
 {
	if (assign_type(fl, type) != 0)
		return -EINVAL;

	fl->fl_owner = current->files;
	fl->fl_pid = current->tgid;

	fl->fl_file = filp;
	fl->fl_flags = FL_LEASE;
	fl->fl_start = 0;
	fl->fl_end = OFFSET_MAX;
	fl->fl_ops = NULL;
	fl->fl_lmops = &lease_manager_ops;
	return 0;
}

/* Allocate a file_lock initialised to this type of lease */
static struct file_lock *lease_alloc(struct file *filp, long type)
{
	struct file_lock *fl = locks_alloc_lock();
	int error = -ENOMEM;

	if (fl == NULL)
		return ERR_PTR(error);

	error = lease_init(filp, type, fl);
	if (error) {
		locks_free_lock(fl);
		return ERR_PTR(error);
	}
	return fl;
}

/* Check if two locks overlap each other.
 */
static inline int locks_overlap(struct file_lock *fl1, struct file_lock *fl2)
{
	return ((fl1->fl_end >= fl2->fl_start) &&
		(fl2->fl_end >= fl1->fl_start));
}

/*
 * Check whether two locks have the same owner.
 */
static int posix_same_owner(struct file_lock *fl1, struct file_lock *fl2)
{
	if (fl1->fl_lmops && fl1->fl_lmops->lm_compare_owner)
		return fl2->fl_lmops == fl1->fl_lmops &&
			fl1->fl_lmops->lm_compare_owner(fl1, fl2);
	return fl1->fl_owner == fl2->fl_owner;
}

/* Remove waiter from blocker's block list.
 * When blocker ends up pointing to itself then the list is empty.
 */
static void __locks_delete_block(struct file_lock *waiter)
{
	list_del_init(&waiter->fl_block);
	list_del_init(&waiter->fl_link);
	waiter->fl_next = NULL;
}

/*
 */
void locks_delete_block(struct file_lock *waiter)
{
	lock_flocks();
	__locks_delete_block(waiter);
	unlock_flocks();
}
EXPORT_SYMBOL(locks_delete_block);

/* Insert waiter into blocker's block list.
 * We use a circular list so that processes can be easily woken up in
 * the order they blocked. The documentation doesn't require this but
 * it seems like the reasonable thing to do.
 */
static void locks_insert_block(struct file_lock *blocker, 
			       struct file_lock *waiter)
{
	BUG_ON(!list_empty(&waiter->fl_block));
	list_add_tail(&waiter->fl_block, &blocker->fl_block);
	waiter->fl_next = blocker;
	if (IS_POSIX(blocker))
		list_add(&waiter->fl_link, &blocked_list);
}

/* Wake up processes blocked waiting for blocker.
 * If told to wait then schedule the processes until the block list
 * is empty, otherwise empty the block list ourselves.
 */
static void locks_wake_up_blocks(struct file_lock *blocker)
{
	while (!list_empty(&blocker->fl_block)) {
		struct file_lock *waiter;

		waiter = list_first_entry(&blocker->fl_block,
				struct file_lock, fl_block);
		__locks_delete_block(waiter);
		if (waiter->fl_lmops && waiter->fl_lmops->lm_notify)
			waiter->fl_lmops->lm_notify(waiter);
		else
			wake_up(&waiter->fl_wait);
	}
}

/* Insert file lock fl into an inode's lock list at the position indicated
 * by pos. At the same time add the lock to the global file lock list.
 */
static void locks_insert_lock(struct file_lock **pos, struct file_lock *fl)
{
	list_add(&fl->fl_link, &file_lock_list);

	fl->fl_nspid = get_pid(task_tgid(current));

	/* insert into file's list */
	fl->fl_next = *pos;
	*pos = fl;
}

/*
 * Delete a lock and then free it.
 * Wake up processes that are blocked waiting for this lock,
 * notify the FS that the lock has been cleared and
 * finally free the lock.
 */
static void locks_delete_lock(struct file_lock **thisfl_p)
{
	struct file_lock *fl = *thisfl_p;

	*thisfl_p = fl->fl_next;
	fl->fl_next = NULL;
	list_del_init(&fl->fl_link);

	fasync_helper(0, fl->fl_file, 0, &fl->fl_fasync);
	if (fl->fl_fasync != NULL) {
		printk(KERN_ERR "locks_delete_lock: fasync == %p\n", fl->fl_fasync);
		fl->fl_fasync = NULL;
	}

	if (fl->fl_nspid) {
		put_pid(fl->fl_nspid);
		fl->fl_nspid = NULL;
	}

	locks_wake_up_blocks(fl);
	locks_free_lock(fl);
}

/* Determine if lock sys_fl blocks lock caller_fl. Common functionality
 * checks for shared/exclusive status of overlapping locks.
 */
static int locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	if (sys_fl->fl_type == F_WRLCK)
		return 1;
	if (caller_fl->fl_type == F_WRLCK)
		return 1;
	return 0;
}

/* Determine if lock sys_fl blocks lock caller_fl. POSIX specific
 * checking before calling the locks_conflict().
 */
static int posix_locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	/* POSIX locks owned by the same process do not conflict with
	 * each other.
	 */
	if (!IS_POSIX(sys_fl) || posix_same_owner(caller_fl, sys_fl))
		return (0);

	/* Check whether they overlap */
	if (!locks_overlap(caller_fl, sys_fl))
		return 0;

	return (locks_conflict(caller_fl, sys_fl));
}

/* Determine if lock sys_fl blocks lock caller_fl. FLOCK specific
 * checking before calling the locks_conflict().
 */
static int flock_locks_conflict(struct file_lock *caller_fl, struct file_lock *sys_fl)
{
	/* FLOCK locks referring to the same filp do not conflict with
	 * each other.
	 */
	if (!IS_FLOCK(sys_fl) || (caller_fl->fl_file == sys_fl->fl_file))
		return (0);
	if ((caller_fl->fl_type & LOCK_MAND) || (sys_fl->fl_type & LOCK_MAND))
		return 0;

	return (locks_conflict(caller_fl, sys_fl));
}

void
posix_test_lock(struct file *filp, struct file_lock *fl)
{
	struct file_lock *cfl;

	lock_flocks();
	for (cfl = filp->f_path.dentry->d_inode->i_flock; cfl; cfl = cfl->fl_next) {
		if (!IS_POSIX(cfl))
			continue;
		if (posix_locks_conflict(fl, cfl))
			break;
	}
	if (cfl) {
		__locks_copy_lock(fl, cfl);
		if (cfl->fl_nspid)
			fl->fl_pid = pid_vnr(cfl->fl_nspid);
	} else
		fl->fl_type = F_UNLCK;
	unlock_flocks();
	return;
}
EXPORT_SYMBOL(posix_test_lock);

/*
 * Deadlock detection:
 *
 * We attempt to detect deadlocks that are due purely to posix file
 * locks.
 *
 * We assume that a task can be waiting for at most one lock at a time.
 * So for any acquired lock, the process holding that lock may be
 * waiting on at most one other lock.  That lock in turns may be held by
 * someone waiting for at most one other lock.  Given a requested lock
 * caller_fl which is about to wait for a conflicting lock block_fl, we
 * follow this chain of waiters to ensure we are not about to create a
 * cycle.
 *
 * Since we do this before we ever put a process to sleep on a lock, we
 * are ensured that there is never a cycle; that is what guarantees that
 * the while() loop in posix_locks_deadlock() eventually completes.
 *
 * Note: the above assumption may not be true when handling lock
 * requests from a broken NFS client. It may also fail in the presence
 * of tasks (such as posix threads) sharing the same open file table.
 *
 * To handle those cases, we just bail out after a few iterations.
 */

#define MAX_DEADLK_ITERATIONS 10

/* Find a lock that the owner of the given block_fl is blocking on. */
static struct file_lock *what_owner_is_waiting_for(struct file_lock *block_fl)
{
	struct file_lock *fl;

	list_for_each_entry(fl, &blocked_list, fl_link) {
		if (posix_same_owner(fl, block_fl))
			return fl->fl_next;
	}
	return NULL;
}

static int posix_locks_deadlock(struct file_lock *caller_fl,
				struct file_lock *block_fl)
{
	int i = 0;

	while ((block_fl = what_owner_is_waiting_for(block_fl))) {
		if (i++ > MAX_DEADLK_ITERATIONS)
			return 0;
		if (posix_same_owner(caller_fl, block_fl))
			return 1;
	}
	return 0;
}

/* Try to create a FLOCK lock on filp. We always insert new FLOCK locks
 * after any leases, but before any posix locks.
 *
 * Note that if called with an FL_EXISTS argument, the caller may determine
 * whether or not a lock was successfully freed by testing the return
 * value for -ENOENT.
 */
static int flock_lock_file(struct file *filp, struct file_lock *request)
{
	struct file_lock *new_fl = NULL;
	struct file_lock **before;
	struct inode * inode = filp->f_path.dentry->d_inode;
	int error = 0;
	int found = 0;

	if (!(request->fl_flags & FL_ACCESS) && (request->fl_type != F_UNLCK)) {
		new_fl = locks_alloc_lock();
		if (!new_fl)
			return -ENOMEM;
	}

	lock_flocks();
	if (request->fl_flags & FL_ACCESS)
		goto find_conflict;

	for_each_lock(inode, before) {
		struct file_lock *fl = *before;
		if (IS_POSIX(fl))
			break;
		if (IS_LEASE(fl))
			continue;
		if (filp != fl->fl_file)
			continue;
		if (request->fl_type == fl->fl_type)
			goto out;
		found = 1;
		locks_delete_lock(before);
		break;
	}

	if (request->fl_type == F_UNLCK) {
		if ((request->fl_flags & FL_EXISTS) && !found)
			error = -ENOENT;
		goto out;
	}

	/*
	 * If a higher-priority process was blocked on the old file lock,
	 * give it the opportunity to lock the file.
	 */
	if (found) {
		unlock_flocks();
		cond_resched();
		lock_flocks();
	}

find_conflict:
	for_each_lock(inode, before) {
		struct file_lock *fl = *before;
		if (IS_POSIX(fl))
			break;
		if (IS_LEASE(fl))
			continue;
		if (!flock_locks_conflict(request, fl))
			continue;
		error = -EAGAIN;
		if (!(request->fl_flags & FL_SLEEP))
			goto out;
		error = FILE_LOCK_DEFERRED;
		locks_insert_block(fl, request);
		goto out;
	}
	if (request->fl_flags & FL_ACCESS)
		goto out;
	locks_copy_lock(new_fl, request);
	locks_insert_lock(before, new_fl);
	new_fl = NULL;
	error = 0;

out:
	unlock_flocks();
	if (new_fl)
		locks_free_lock(new_fl);
	return error;
}

static int __posix_lock_file(struct inode *inode, struct file_lock *request, struct file_lock *conflock)
{
	struct file_lock *fl;
	struct file_lock *new_fl = NULL;
	struct file_lock *new_fl2 = NULL;
	struct file_lock *left = NULL;
	struct file_lock *right = NULL;
	struct file_lock **before;
	int error, added = 0;

	/*
	 * We may need two file_lock structures for this operation,
	 * so we get them in advance to avoid races.
	 *
	 * In some cases we can be sure, that no new locks will be needed
	 */
	if (!(request->fl_flags & FL_ACCESS) &&
	    (request->fl_type != F_UNLCK ||
	     request->fl_start != 0 || request->fl_end != OFFSET_MAX)) {
		new_fl = locks_alloc_lock();
		new_fl2 = locks_alloc_lock();
	}

	lock_flocks();
	if (request->fl_type != F_UNLCK) {
		for_each_lock(inode, before) {
			fl = *before;
			if (!IS_POSIX(fl))
				continue;
			if (!posix_locks_conflict(request, fl))
				continue;
			if (conflock)
				__locks_copy_lock(conflock, fl);
			error = -EAGAIN;
			if (!(request->fl_flags & FL_SLEEP))
				goto out;
			error = -EDEADLK;
			if (posix_locks_deadlock(request, fl))
				goto out;
			error = FILE_LOCK_DEFERRED;
			locks_insert_block(fl, request);
			goto out;
  		}
  	}

	/* If we're just looking for a conflict, we're done. */
	error = 0;
	if (request->fl_flags & FL_ACCESS)
		goto out;

	/*
	 * Find the first old lock with the same owner as the new lock.
	 */
	
	before = &inode->i_flock;

	/* First skip locks owned by other processes.  */
	while ((fl = *before) && (!IS_POSIX(fl) ||
				  !posix_same_owner(request, fl))) {
		before = &fl->fl_next;
	}

	/* Process locks with this owner.  */
	while ((fl = *before) && posix_same_owner(request, fl)) {
		/* Detect adjacent or overlapping regions (if same lock type)
		 */
		if (request->fl_type == fl->fl_type) {
			/* In all comparisons of start vs end, use
			 * "start - 1" rather than "end + 1". If end
			 * is OFFSET_MAX, end + 1 will become negative.
			 */
			if (fl->fl_end < request->fl_start - 1)
				goto next_lock;
			/* If the next lock in the list has entirely bigger
			 * addresses than the new one, insert the lock here.
			 */
			if (fl->fl_start - 1 > request->fl_end)
				break;

			/* If we come here, the new and old lock are of the
			 * same type and adjacent or overlapping. Make one
			 * lock yielding from the lower start address of both
			 * locks to the higher end address.
			 */
			if (fl->fl_start > request->fl_start)
				fl->fl_start = request->fl_start;
			else
				request->fl_start = fl->fl_start;
			if (fl->fl_end < request->fl_end)
				fl->fl_end = request->fl_end;
			else
				request->fl_end = fl->fl_end;
			if (added) {
				locks_delete_lock(before);
				continue;
			}
			request = fl;
			added = 1;
		}
		else {
			/* Processing for different lock types is a bit
			 * more complex.
			 */
			if (fl->fl_end < request->fl_start)
				goto next_lock;
			if (fl->fl_start > request->fl_end)
				break;
			if (request->fl_type == F_UNLCK)
				added = 1;
			if (fl->fl_start < request->fl_start)
				left = fl;
			/* If the next lock in the list has a higher end
			 * address than the new one, insert the new one here.
			 */
			if (fl->fl_end > request->fl_end) {
				right = fl;
				break;
			}
			if (fl->fl_start >= request->fl_start) {
				/* The new lock completely replaces an old
				 * one (This may happen several times).
				 */
				if (added) {
					locks_delete_lock(before);
					continue;
				}
				/* Replace the old lock with the new one.
				 * Wake up anybody waiting for the old one,
				 * as the change in lock type might satisfy
				 * their needs.
				 */
				locks_wake_up_blocks(fl);
				fl->fl_start = request->fl_start;
				fl->fl_end = request->fl_end;
				fl->fl_type = request->fl_type;
				locks_release_private(fl);
				locks_copy_private(fl, request);
				request = fl;
				added = 1;
			}
		}
		/* Go on to next lock.
		 */
	next_lock:
		before = &fl->fl_next;
	}

	/*
	 * The above code only modifies existing locks in case of
	 * merging or replacing.  If new lock(s) need to be inserted
	 * all modifications are done bellow this, so it's safe yet to
	 * bail out.
	 */
	error = -ENOLCK; /* "no luck" */
	if (right && left == right && !new_fl2)
		goto out;

	error = 0;
	if (!added) {
		if (request->fl_type == F_UNLCK) {
			if (request->fl_flags & FL_EXISTS)
				error = -ENOENT;
			goto out;
		}

		if (!new_fl) {
			error = -ENOLCK;
			goto out;
		}
		locks_copy_lock(new_fl, request);
		locks_insert_lock(before, new_fl);
		new_fl = NULL;
	}
	if (right) {
		if (left == right) {
			/* The new lock breaks the old one in two pieces,
			 * so we have to use the second new lock.
			 */
			left = new_fl2;
			new_fl2 = NULL;
			locks_copy_lock(left, right);
			locks_insert_lock(before, left);
		}
		right->fl_start = request->fl_end + 1;
		locks_wake_up_blocks(right);
	}
	if (left) {
		left->fl_end = request->fl_start - 1;
		locks_wake_up_blocks(left);
	}
 out:
	unlock_flocks();
	/*
	 * Free any unused locks.
	 */
	if (new_fl)
		locks_free_lock(new_fl);
	if (new_fl2)
		locks_free_lock(new_fl2);
	return error;
}

/**
 * posix_lock_file - Apply a POSIX-style lock to a file
 * @filp: The file to apply the lock to
 * @fl: The lock to be applied
 * @conflock: Place to return a copy of the conflicting lock, if found.
 *
 * Add a POSIX style lock to a file.
 * We merge adjacent & overlapping locks whenever possible.
 * POSIX locks are sorted by owner task, then by starting address
 *
 * Note that if called with an FL_EXISTS argument, the caller may determine
 * whether or not a lock was successfully freed by testing the return
 * value for -ENOENT.
 */
int posix_lock_file(struct file *filp, struct file_lock *fl,
			struct file_lock *conflock)
{
	return __posix_lock_file(filp->f_path.dentry->d_inode, fl, conflock);
}
EXPORT_SYMBOL(posix_lock_file);

/**
 * posix_lock_file_wait - Apply a POSIX-style lock to a file
 * @filp: The file to apply the lock to
 * @fl: The lock to be applied
 *
 * Add a POSIX style lock to a file.
 * We merge adjacent & overlapping locks whenever possible.
 * POSIX locks are sorted by owner task, then by starting address
 */
int posix_lock_file_wait(struct file *filp, struct file_lock *fl)
{
	int error;
	might_sleep ();
	for (;;) {
		error = posix_lock_file(filp, fl, NULL);
		if (error != FILE_LOCK_DEFERRED)
			break;
		error = wait_event_interruptible(fl->fl_wait, !fl->fl_next);
		if (!error)
			continue;

		locks_delete_block(fl);
		break;
	}
	return error;
}
EXPORT_SYMBOL(posix_lock_file_wait);

/**
 * locks_mandatory_locked - Check for an active lock
 * @inode: the file to check
 *
 * Searches the inode's list of locks to find any POSIX locks which conflict.
 * This function is called from locks_verify_locked() only.
 */
int locks_mandatory_locked(struct inode *inode)
{
	fl_owner_t owner = current->files;
	struct file_lock *fl;

	/*
	 * Search the lock list for this inode for any POSIX locks.
	 */
	lock_flocks();
	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (!IS_POSIX(fl))
			continue;
		if (fl->fl_owner != owner)
			break;
	}
	unlock_flocks();
	return fl ? -EAGAIN : 0;
}

/**
 * locks_mandatory_area - Check for a conflicting lock
 * @read_write: %FLOCK_VERIFY_WRITE for exclusive access, %FLOCK_VERIFY_READ
 *		for shared
 * @inode:      the file to check
 * @filp:       how the file was opened (if it was)
 * @offset:     start of area to check
 * @count:      length of area to check
 *
 * Searches the inode's list of locks to find any POSIX locks which conflict.
 * This function is called from rw_verify_area() and
 * locks_verify_truncate().
 */
int locks_mandatory_area(int read_write, struct inode *inode,
			 struct file *filp, loff_t offset,
			 size_t count)
{
	struct file_lock fl;
	int error;

	locks_init_lock(&fl);
	fl.fl_owner = current->files;
	fl.fl_pid = current->tgid;
	fl.fl_file = filp;
	fl.fl_flags = FL_POSIX | FL_ACCESS;
	if (filp && !(filp->f_flags & O_NONBLOCK))
		fl.fl_flags |= FL_SLEEP;
	fl.fl_type = (read_write == FLOCK_VERIFY_WRITE) ? F_WRLCK : F_RDLCK;
	fl.fl_start = offset;
	fl.fl_end = offset + count - 1;

	for (;;) {
		error = __posix_lock_file(inode, &fl, NULL);
		if (error != FILE_LOCK_DEFERRED)
			break;
		error = wait_event_interruptible(fl.fl_wait, !fl.fl_next);
		if (!error) {
			/*
			 * If we've been sleeping someone might have
			 * changed the permissions behind our back.
			 */
			if (__mandatory_lock(inode))
				continue;
		}

		locks_delete_block(&fl);
		break;
	}

	return error;
}

EXPORT_SYMBOL(locks_mandatory_area);

static void lease_clear_pending(struct file_lock *fl, int arg)
{
	switch (arg) {
	case F_UNLCK:
		fl->fl_flags &= ~FL_UNLOCK_PENDING;
		/* fall through: */
	case F_RDLCK:
		fl->fl_flags &= ~FL_DOWNGRADE_PENDING;
	}
}

/* We already had a lease on this file; just change its type */
int lease_modify(struct file_lock **before, int arg)
{
	struct file_lock *fl = *before;
	int error = assign_type(fl, arg);

	if (error)
		return error;
	lease_clear_pending(fl, arg);
	locks_wake_up_blocks(fl);
	if (arg == F_UNLCK)
		locks_delete_lock(before);
	return 0;
}

EXPORT_SYMBOL(lease_modify);

static bool past_time(unsigned long then)
{
	if (!then)
		/* 0 is a special value meaning "this never expires": */
		return false;
	return time_after(jiffies, then);
}

static void time_out_leases(struct inode *inode)
{
	struct file_lock **before;
	struct file_lock *fl;

	before = &inode->i_flock;
	while ((fl = *before) && IS_LEASE(fl) && lease_breaking(fl)) {
		if (past_time(fl->fl_downgrade_time))
			lease_modify(before, F_RDLCK);
		if (past_time(fl->fl_break_time))
			lease_modify(before, F_UNLCK);
		if (fl == *before)	/* lease_modify may have freed fl */
			before = &fl->fl_next;
	}
}

/**
 *	__break_lease	-	revoke all outstanding leases on file
 *	@inode: the inode of the file to return
 *	@mode: the open mode (read or write)
 *
 *	break_lease (inlined for speed) has checked there already is at least
 *	some kind of lock (maybe a lease) on this file.  Leases are broken on
 *	a call to open() or truncate().  This function can sleep unless you
 *	specified %O_NONBLOCK to your open().
 */
int __break_lease(struct inode *inode, unsigned int mode)
{
	int error = 0;
	struct file_lock *new_fl, *flock;
	struct file_lock *fl;
	unsigned long break_time;
	int i_have_this_lease = 0;
	int want_write = (mode & O_ACCMODE) != O_RDONLY;

	new_fl = lease_alloc(NULL, want_write ? F_WRLCK : F_RDLCK);
	if (IS_ERR(new_fl))
		return PTR_ERR(new_fl);

	lock_flocks();

	time_out_leases(inode);

	flock = inode->i_flock;
	if ((flock == NULL) || !IS_LEASE(flock))
		goto out;

	if (!locks_conflict(flock, new_fl))
		goto out;

	for (fl = flock; fl && IS_LEASE(fl); fl = fl->fl_next)
		if (fl->fl_owner == current->files)
			i_have_this_lease = 1;

	break_time = 0;
	if (lease_break_time > 0) {
		break_time = jiffies + lease_break_time * HZ;
		if (break_time == 0)
			break_time++;	/* so that 0 means no break time */
	}

	for (fl = flock; fl && IS_LEASE(fl); fl = fl->fl_next) {
		if (want_write) {
			if (fl->fl_flags & FL_UNLOCK_PENDING)
				continue;
			fl->fl_flags |= FL_UNLOCK_PENDING;
			fl->fl_break_time = break_time;
		} else {
			if (lease_breaking(flock))
				continue;
			fl->fl_flags |= FL_DOWNGRADE_PENDING;
			fl->fl_downgrade_time = break_time;
		}
		fl->fl_lmops->lm_break(fl);
	}

	if (i_have_this_lease || (mode & O_NONBLOCK)) {
		error = -EWOULDBLOCK;
		goto out;
	}

restart:
	break_time = flock->fl_break_time;
	if (break_time != 0)
		break_time -= jiffies;
	if (break_time == 0)
		break_time++;
	locks_insert_block(flock, new_fl);
	unlock_flocks();
	error = wait_event_interruptible_timeout(new_fl->fl_wait,
						!new_fl->fl_next, break_time);
	lock_flocks();
	__locks_delete_block(new_fl);
	if (error >= 0) {
		if (error == 0)
			time_out_leases(inode);
		/*
		 * Wait for the next conflicting lease that has not been
		 * broken yet
		 */
		for (flock = inode->i_flock; flock && IS_LEASE(flock);
				flock = flock->fl_next) {
			if (locks_conflict(new_fl, flock))
				goto restart;
		}
		error = 0;
	}

out:
	unlock_flocks();
	locks_free_lock(new_fl);
	return error;
}

EXPORT_SYMBOL(__break_lease);

/**
 *	lease_get_mtime - get the last modified time of an inode
 *	@inode: the inode
 *      @time:  pointer to a timespec which will contain the last modified time
 *
 * This is to force NFS clients to flush their caches for files with
 * exclusive leases.  The justification is that if someone has an
 * exclusive lease, then they could be modifying it.
 */
void lease_get_mtime(struct inode *inode, struct timespec *time)
{
	struct file_lock *flock = inode->i_flock;
	if (flock && IS_LEASE(flock) && (flock->fl_type & F_WRLCK))
		*time = current_fs_time(inode->i_sb);
	else
		*time = inode->i_mtime;
}

EXPORT_SYMBOL(lease_get_mtime);

/**
 *	fcntl_getlease - Enquire what lease is currently active
 *	@filp: the file
 *
 *	The value returned by this function will be one of
 *	(if no lease break is pending):
 *
 *	%F_RDLCK to indicate a shared lease is held.
 *
 *	%F_WRLCK to indicate an exclusive lease is held.
 *
 *	%F_UNLCK to indicate no lease is held.
 *
 *	(if a lease break is pending):
 *
 *	%F_RDLCK to indicate an exclusive lease needs to be
 *		changed to a shared lease (or removed).
 *
 *	%F_UNLCK to indicate the lease needs to be removed.
 *
 *	XXX: sfr & willy disagree over whether F_INPROGRESS
 *	should be returned to userspace.
 */
int fcntl_getlease(struct file *filp)
{
	struct file_lock *fl;
	int type = F_UNLCK;

	lock_flocks();
	time_out_leases(filp->f_path.dentry->d_inode);
	for (fl = filp->f_path.dentry->d_inode->i_flock; fl && IS_LEASE(fl);
			fl = fl->fl_next) {
		if (fl->fl_file == filp) {
			type = target_leasetype(fl);
			break;
		}
	}
	unlock_flocks();
	return type;
}

int generic_add_lease(struct file *filp, long arg, struct file_lock **flp)
{
	struct file_lock *fl, **before, **my_before = NULL, *lease;
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	int error;

	lease = *flp;

	error = -EAGAIN;
	if ((arg == F_RDLCK) && (atomic_read(&inode->i_writecount) > 0))
		goto out;
	if ((arg == F_WRLCK)
	    && ((dentry->d_count > 1)
		|| (atomic_read(&inode->i_count) > 1)))
		goto out;

	/*
	 * At this point, we know that if there is an exclusive
	 * lease on this file, then we hold it on this filp
	 * (otherwise our open of this file would have blocked).
	 * And if we are trying to acquire an exclusive lease,
	 * then the file is not open by anyone (including us)
	 * except for this filp.
	 */
	error = -EAGAIN;
	for (before = &inode->i_flock;
			((fl = *before) != NULL) && IS_LEASE(fl);
			before = &fl->fl_next) {
		if (fl->fl_file == filp) {
			my_before = before;
			continue;
		}
		/*
		 * No exclusive leases if someone else has a lease on
		 * this file:
		 */
		if (arg == F_WRLCK)
			goto out;
		/*
		 * Modifying our existing lease is OK, but no getting a
		 * new lease if someone else is opening for write:
		 */
		if (fl->fl_flags & FL_UNLOCK_PENDING)
			goto out;
	}

	if (my_before != NULL) {
		error = lease->fl_lmops->lm_change(my_before, arg);
		if (!error)
			*flp = *my_before;
		goto out;
	}

	error = -EINVAL;
	if (!leases_enable)
		goto out;

	locks_insert_lock(before, lease);
	return 0;

out:
	return error;
}

int generic_delete_lease(struct file *filp, struct file_lock **flp)
{
	struct file_lock *fl, **before;
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;

	for (before = &inode->i_flock;
			((fl = *before) != NULL) && IS_LEASE(fl);
			before = &fl->fl_next) {
		if (fl->fl_file != filp)
			continue;
		return (*flp)->fl_lmops->lm_change(before, F_UNLCK);
	}
	return -EAGAIN;
}

/**
 *	generic_setlease	-	sets a lease on an open file
 *	@filp: file pointer
 *	@arg: type of lease to obtain
 *	@flp: input - file_lock to use, output - file_lock inserted
 *
 *	The (input) flp->fl_lmops->lm_break function is required
 *	by break_lease().
 *
 *	Called with file_lock_lock held.
 */
int generic_setlease(struct file *filp, long arg, struct file_lock **flp)
{
	struct dentry *dentry = filp->f_path.dentry;
	struct inode *inode = dentry->d_inode;
	int error;

	if ((current_fsuid() != inode->i_uid) && !capable(CAP_LEASE))
		return -EACCES;
	if (!S_ISREG(inode->i_mode))
		return -EINVAL;
	error = security_file_lock(filp, arg);
	if (error)
		return error;

	time_out_leases(inode);

	BUG_ON(!(*flp)->fl_lmops->lm_break);

	switch (arg) {
	case F_UNLCK:
		return generic_delete_lease(filp, flp);
	case F_RDLCK:
	case F_WRLCK:
		return generic_add_lease(filp, arg, flp);
	default:
		return -EINVAL;
	}
}
EXPORT_SYMBOL(generic_setlease);

static int __vfs_setlease(struct file *filp, long arg, struct file_lock **lease)
{
	if (filp->f_op && filp->f_op->setlease)
		return filp->f_op->setlease(filp, arg, lease);
	else
		return generic_setlease(filp, arg, lease);
}

/**
 *	vfs_setlease        -       sets a lease on an open file
 *	@filp: file pointer
 *	@arg: type of lease to obtain
 *	@lease: file_lock to use
 *
 *	Call this to establish a lease on the file.
 *	The (*lease)->fl_lmops->lm_break operation must be set; if not,
 *	break_lease will oops!
 *
 *	This will call the filesystem's setlease file method, if
 *	defined.  Note that there is no getlease method; instead, the
 *	filesystem setlease method should call back to setlease() to
 *	add a lease to the inode's lease list, where fcntl_getlease() can
 *	find it.  Since fcntl_getlease() only reports whether the current
 *	task holds a lease, a cluster filesystem need only do this for
 *	leases held by processes on this node.
 *
 *	There is also no break_lease method; filesystems that
 *	handle their own leases should break leases themselves from the
 *	filesystem's open, create, and (on truncate) setattr methods.
 *
 *	Warning: the only current setlease methods exist only to disable
 *	leases in certain cases.  More vfs changes may be required to
 *	allow a full filesystem lease implementation.
 */

int vfs_setlease(struct file *filp, long arg, struct file_lock **lease)
{
	int error;

	lock_flocks();
	error = __vfs_setlease(filp, arg, lease);
	unlock_flocks();

	return error;
}
EXPORT_SYMBOL_GPL(vfs_setlease);

static int do_fcntl_delete_lease(struct file *filp)
{
	struct file_lock fl, *flp = &fl;

	lease_init(filp, F_UNLCK, flp);

	return vfs_setlease(filp, F_UNLCK, &flp);
}

static int do_fcntl_add_lease(unsigned int fd, struct file *filp, long arg)
{
	struct file_lock *fl, *ret;
	struct fasync_struct *new;
	int error;

	fl = lease_alloc(filp, arg);
	if (IS_ERR(fl))
		return PTR_ERR(fl);

	new = fasync_alloc();
	if (!new) {
		locks_free_lock(fl);
		return -ENOMEM;
	}
	ret = fl;
	lock_flocks();
	error = __vfs_setlease(filp, arg, &ret);
	if (error) {
		unlock_flocks();
		locks_free_lock(fl);
		goto out_free_fasync;
	}
	if (ret != fl)
		locks_free_lock(fl);

	/*
	 * fasync_insert_entry() returns the old entry if any.
	 * If there was no old entry, then it used 'new' and
	 * inserted it into the fasync list. Clear new so that
	 * we don't release it here.
	 */
	if (!fasync_insert_entry(fd, filp, &ret->fl_fasync, new))
		new = NULL;

	error = __f_setown(filp, task_pid(current), PIDTYPE_PID, 0);
	unlock_flocks();

out_free_fasync:
	if (new)
		fasync_free(new);
	return error;
}

/**
 *	fcntl_setlease	-	sets a lease on an open file
 *	@fd: open file descriptor
 *	@filp: file pointer
 *	@arg: type of lease to obtain
 *
 *	Call this fcntl to establish a lease on the file.
 *	Note that you also need to call %F_SETSIG to
 *	receive a signal when the lease is broken.
 */
int fcntl_setlease(unsigned int fd, struct file *filp, long arg)
{
	if (arg == F_UNLCK)
		return do_fcntl_delete_lease(filp);
	return do_fcntl_add_lease(fd, filp, arg);
}

/**
 * flock_lock_file_wait - Apply a FLOCK-style lock to a file
 * @filp: The file to apply the lock to
 * @fl: The lock to be applied
 *
 * Add a FLOCK style lock to a file.
 */
int flock_lock_file_wait(struct file *filp, struct file_lock *fl)
{
	int error;
	might_sleep();
	for (;;) {
		error = flock_lock_file(filp, fl);
		if (error != FILE_LOCK_DEFERRED)
			break;
		error = wait_event_interruptible(fl->fl_wait, !fl->fl_next);
		if (!error)
			continue;

		locks_delete_block(fl);
		break;
	}
	return error;
}

EXPORT_SYMBOL(flock_lock_file_wait);

/**
 *	sys_flock: - flock() system call.
 *	@fd: the file descriptor to lock.
 *	@cmd: the type of lock to apply.
 *
 *	Apply a %FL_FLOCK style lock to an open file descriptor.
 *	The @cmd can be one of
 *
 *	%LOCK_SH -- a shared lock.
 *
 *	%LOCK_EX -- an exclusive lock.
 *
 *	%LOCK_UN -- remove an existing lock.
 *
 *	%LOCK_MAND -- a `mandatory' flock.  This exists to emulate Windows Share Modes.
 *
 *	%LOCK_MAND can be combined with %LOCK_READ or %LOCK_WRITE to allow other
 *	processes read and write access respectively.
 */
SYSCALL_DEFINE2(flock, unsigned int, fd, unsigned int, cmd)
{
	struct file *filp;
	struct file_lock *lock;
	int can_sleep, unlock;
	int error;

	error = -EBADF;
	filp = fget(fd);
	if (!filp)
		goto out;

	can_sleep = !(cmd & LOCK_NB);
	cmd &= ~LOCK_NB;
	unlock = (cmd == LOCK_UN);

	if (!unlock && !(cmd & LOCK_MAND) &&
	    !(filp->f_mode & (FMODE_READ|FMODE_WRITE)))
		goto out_putf;

	error = flock_make_lock(filp, &lock, cmd);
	if (error)
		goto out_putf;
	if (can_sleep)
		lock->fl_flags |= FL_SLEEP;

	error = security_file_lock(filp, lock->fl_type);
	if (error)
		goto out_free;

	if (filp->f_op && filp->f_op->flock)
		error = filp->f_op->flock(filp,
					  (can_sleep) ? F_SETLKW : F_SETLK,
					  lock);
	else
		error = flock_lock_file_wait(filp, lock);

 out_free:
	locks_free_lock(lock);

 out_putf:
	fput(filp);
 out:
	return error;
}

/**
 * vfs_test_lock - test file byte range lock
 * @filp: The file to test lock for
 * @fl: The lock to test; also used to hold result
 *
 * Returns -ERRNO on failure.  Indicates presence of conflicting lock by
 * setting conf->fl_type to something other than F_UNLCK.
 */
int vfs_test_lock(struct file *filp, struct file_lock *fl)
{
	if (filp->f_op && filp->f_op->lock)
		return filp->f_op->lock(filp, F_GETLK, fl);
	posix_test_lock(filp, fl);
	return 0;
}
EXPORT_SYMBOL_GPL(vfs_test_lock);

static int posix_lock_to_flock(struct flock *flock, struct file_lock *fl)
{
	flock->l_pid = fl->fl_pid;
#if BITS_PER_LONG == 32
	/*
	 * Make sure we can represent the posix lock via
	 * legacy 32bit flock.
	 */
	if (fl->fl_start > OFFT_OFFSET_MAX)
		return -EOVERFLOW;
	if (fl->fl_end != OFFSET_MAX && fl->fl_end > OFFT_OFFSET_MAX)
		return -EOVERFLOW;
#endif
	flock->l_start = fl->fl_start;
	flock->l_len = fl->fl_end == OFFSET_MAX ? 0 :
		fl->fl_end - fl->fl_start + 1;
	flock->l_whence = 0;
	flock->l_type = fl->fl_type;
	return 0;
}

#if BITS_PER_LONG == 32
static void posix_lock_to_flock64(struct flock64 *flock, struct file_lock *fl)
{
	flock->l_pid = fl->fl_pid;
	flock->l_start = fl->fl_start;
	flock->l_len = fl->fl_end == OFFSET_MAX ? 0 :
		fl->fl_end - fl->fl_start + 1;
	flock->l_whence = 0;
	flock->l_type = fl->fl_type;
}
#endif

/* Report the first existing lock that would conflict with l.
 * This implements the F_GETLK command of fcntl().
 */
int fcntl_getlk(struct file *filp, struct flock __user *l)
{
	struct file_lock file_lock;
	struct flock flock;
	int error;

	error = -EFAULT;
	if (copy_from_user(&flock, l, sizeof(flock)))
		goto out;
	error = -EINVAL;
	if ((flock.l_type != F_RDLCK) && (flock.l_type != F_WRLCK))
		goto out;

	error = flock_to_posix_lock(filp, &file_lock, &flock);
	if (error)
		goto out;

	error = vfs_test_lock(filp, &file_lock);
	if (error)
		goto out;
 
	flock.l_type = file_lock.fl_type;
	if (file_lock.fl_type != F_UNLCK) {
		error = posix_lock_to_flock(&flock, &file_lock);
		if (error)
			goto out;
	}
	error = -EFAULT;
	if (!copy_to_user(l, &flock, sizeof(flock)))
		error = 0;
out:
	return error;
}

/**
 * vfs_lock_file - file byte range lock
 * @filp: The file to apply the lock to
 * @cmd: type of locking operation (F_SETLK, F_GETLK, etc.)
 * @fl: The lock to be applied
 * @conf: Place to return a copy of the conflicting lock, if found.
 *
 * A caller that doesn't care about the conflicting lock may pass NULL
 * as the final argument.
 *
 * If the filesystem defines a private ->lock() method, then @conf will
 * be left unchanged; so a caller that cares should initialize it to
 * some acceptable default.
 *
 * To avoid blocking kernel daemons, such as lockd, that need to acquire POSIX
 * locks, the ->lock() interface may return asynchronously, before the lock has
 * been granted or denied by the underlying filesystem, if (and only if)
 * lm_grant is set. Callers expecting ->lock() to return asynchronously
 * will only use F_SETLK, not F_SETLKW; they will set FL_SLEEP if (and only if)
 * the request is for a blocking lock. When ->lock() does return asynchronously,
 * it must return FILE_LOCK_DEFERRED, and call ->lm_grant() when the lock
 * request completes.
 * If the request is for non-blocking lock the file system should return
 * FILE_LOCK_DEFERRED then try to get the lock and call the callback routine
 * with the result. If the request timed out the callback routine will return a
 * nonzero return code and the file system should release the lock. The file
 * system is also responsible to keep a corresponding posix lock when it
 * grants a lock so the VFS can find out which locks are locally held and do
 * the correct lock cleanup when required.
 * The underlying filesystem must not drop the kernel lock or call
 * ->lm_grant() before returning to the caller with a FILE_LOCK_DEFERRED
 * return code.
 */
int vfs_lock_file(struct file *filp, unsigned int cmd, struct file_lock *fl, struct file_lock *conf)
{
	if (filp->f_op && filp->f_op->lock)
		return filp->f_op->lock(filp, cmd, fl);
	else
		return posix_lock_file(filp, fl, conf);
}
EXPORT_SYMBOL_GPL(vfs_lock_file);

static int do_lock_file_wait(struct file *filp, unsigned int cmd,
			     struct file_lock *fl)
{
	int error;

	error = security_file_lock(filp, fl->fl_type);
	if (error)
		return error;

	for (;;) {
		error = vfs_lock_file(filp, cmd, fl, NULL);
		if (error != FILE_LOCK_DEFERRED)
			break;
		error = wait_event_interruptible(fl->fl_wait, !fl->fl_next);
		if (!error)
			continue;

		locks_delete_block(fl);
		break;
	}

	return error;
}

/* Apply the lock described by l to an open file descriptor.
 * This implements both the F_SETLK and F_SETLKW commands of fcntl().
 */
int fcntl_setlk(unsigned int fd, struct file *filp, unsigned int cmd,
		struct flock __user *l)
{
	struct file_lock *file_lock = locks_alloc_lock();
	struct flock flock;
	struct inode *inode;
	struct file *f;
	int error;

	if (file_lock == NULL)
		return -ENOLCK;

	/*
	 * This might block, so we do it before checking the inode.
	 */
	error = -EFAULT;
	if (copy_from_user(&flock, l, sizeof(flock)))
		goto out;

	inode = filp->f_path.dentry->d_inode;

	/* Don't allow mandatory locks on files that may be memory mapped
	 * and shared.
	 */
	if (mandatory_lock(inode) && mapping_writably_mapped(filp->f_mapping)) {
		error = -EAGAIN;
		goto out;
	}

again:
	error = flock_to_posix_lock(filp, file_lock, &flock);
	if (error)
		goto out;
	if (cmd == F_SETLKW) {
		file_lock->fl_flags |= FL_SLEEP;
	}
	
	error = -EBADF;
	switch (flock.l_type) {
	case F_RDLCK:
		if (!(filp->f_mode & FMODE_READ))
			goto out;
		break;
	case F_WRLCK:
		if (!(filp->f_mode & FMODE_WRITE))
			goto out;
		break;
	case F_UNLCK:
		break;
	default:
		error = -EINVAL;
		goto out;
	}

	error = do_lock_file_wait(filp, cmd, file_lock);

	/*
	 * Attempt to detect a close/fcntl race and recover by
	 * releasing the lock that was just acquired.
	 */
	/*
	 * we need that spin_lock here - it prevents reordering between
	 * update of inode->i_flock and check for it done in close().
	 * rcu_read_lock() wouldn't do.
	 */
	spin_lock(&current->files->file_lock);
	f = fcheck(fd);
	spin_unlock(&current->files->file_lock);
	if (!error && f != filp && flock.l_type != F_UNLCK) {
		flock.l_type = F_UNLCK;
		goto again;
	}

out:
	locks_free_lock(file_lock);
	return error;
}

#if BITS_PER_LONG == 32
/* Report the first existing lock that would conflict with l.
 * This implements the F_GETLK command of fcntl().
 */
int fcntl_getlk64(struct file *filp, struct flock64 __user *l)
{
	struct file_lock file_lock;
	struct flock64 flock;
	int error;

	error = -EFAULT;
	if (copy_from_user(&flock, l, sizeof(flock)))
		goto out;
	error = -EINVAL;
	if ((flock.l_type != F_RDLCK) && (flock.l_type != F_WRLCK))
		goto out;

	error = flock64_to_posix_lock(filp, &file_lock, &flock);
	if (error)
		goto out;

	error = vfs_test_lock(filp, &file_lock);
	if (error)
		goto out;

	flock.l_type = file_lock.fl_type;
	if (file_lock.fl_type != F_UNLCK)
		posix_lock_to_flock64(&flock, &file_lock);

	error = -EFAULT;
	if (!copy_to_user(l, &flock, sizeof(flock)))
		error = 0;
  
out:
	return error;
}

/* Apply the lock described by l to an open file descriptor.
 * This implements both the F_SETLK and F_SETLKW commands of fcntl().
 */
int fcntl_setlk64(unsigned int fd, struct file *filp, unsigned int cmd,
		struct flock64 __user *l)
{
	struct file_lock *file_lock = locks_alloc_lock();
	struct flock64 flock;
	struct inode *inode;
	struct file *f;
	int error;

	if (file_lock == NULL)
		return -ENOLCK;

	/*
	 * This might block, so we do it before checking the inode.
	 */
	error = -EFAULT;
	if (copy_from_user(&flock, l, sizeof(flock)))
		goto out;

	inode = filp->f_path.dentry->d_inode;

	/* Don't allow mandatory locks on files that may be memory mapped
	 * and shared.
	 */
	if (mandatory_lock(inode) && mapping_writably_mapped(filp->f_mapping)) {
		error = -EAGAIN;
		goto out;
	}

again:
	error = flock64_to_posix_lock(filp, file_lock, &flock);
	if (error)
		goto out;
	if (cmd == F_SETLKW64) {
		file_lock->fl_flags |= FL_SLEEP;
	}
	
	error = -EBADF;
	switch (flock.l_type) {
	case F_RDLCK:
		if (!(filp->f_mode & FMODE_READ))
			goto out;
		break;
	case F_WRLCK:
		if (!(filp->f_mode & FMODE_WRITE))
			goto out;
		break;
	case F_UNLCK:
		break;
	default:
		error = -EINVAL;
		goto out;
	}

	error = do_lock_file_wait(filp, cmd, file_lock);

	/*
	 * Attempt to detect a close/fcntl race and recover by
	 * releasing the lock that was just acquired.
	 */
	spin_lock(&current->files->file_lock);
	f = fcheck(fd);
	spin_unlock(&current->files->file_lock);
	if (!error && f != filp && flock.l_type != F_UNLCK) {
		flock.l_type = F_UNLCK;
		goto again;
	}

out:
	locks_free_lock(file_lock);
	return error;
}
#endif /* BITS_PER_LONG == 32 */

/*
 * This function is called when the file is being removed
 * from the task's fd array.  POSIX locks belonging to this task
 * are deleted at this time.
 */
void locks_remove_posix(struct file *filp, fl_owner_t owner)
{
	struct file_lock lock;

	/*
	 * If there are no locks held on this file, we don't need to call
	 * posix_lock_file().  Another process could be setting a lock on this
	 * file at the same time, but we wouldn't remove that lock anyway.
	 */
	if (!filp->f_path.dentry->d_inode->i_flock)
		return;

	lock.fl_type = F_UNLCK;
	lock.fl_flags = FL_POSIX | FL_CLOSE;
	lock.fl_start = 0;
	lock.fl_end = OFFSET_MAX;
	lock.fl_owner = owner;
	lock.fl_pid = current->tgid;
	lock.fl_file = filp;
	lock.fl_ops = NULL;
	lock.fl_lmops = NULL;

	vfs_lock_file(filp, F_SETLK, &lock, NULL);

	if (lock.fl_ops && lock.fl_ops->fl_release_private)
		lock.fl_ops->fl_release_private(&lock);
}

EXPORT_SYMBOL(locks_remove_posix);

/*
 * This function is called on the last close of an open file.
 */
void locks_remove_flock(struct file *filp)
{
	struct inode * inode = filp->f_path.dentry->d_inode;
	struct file_lock *fl;
	struct file_lock **before;

	if (!inode->i_flock)
		return;

	if (filp->f_op && filp->f_op->flock) {
		struct file_lock fl = {
			.fl_pid = current->tgid,
			.fl_file = filp,
			.fl_flags = FL_FLOCK,
			.fl_type = F_UNLCK,
			.fl_end = OFFSET_MAX,
		};
		filp->f_op->flock(filp, F_SETLKW, &fl);
		if (fl.fl_ops && fl.fl_ops->fl_release_private)
			fl.fl_ops->fl_release_private(&fl);
	}

	lock_flocks();
	before = &inode->i_flock;

	while ((fl = *before) != NULL) {
		if (fl->fl_file == filp) {
			if (IS_FLOCK(fl)) {
				locks_delete_lock(before);
				continue;
			}
			if (IS_LEASE(fl)) {
				lease_modify(before, F_UNLCK);
				continue;
			}
			/* What? */
			BUG();
 		}
		before = &fl->fl_next;
	}
	unlock_flocks();
}

/**
 *	posix_unblock_lock - stop waiting for a file lock
 *      @filp:   how the file was opened
 *	@waiter: the lock which was waiting
 *
 *	lockd needs to block waiting for locks.
 */
int
posix_unblock_lock(struct file *filp, struct file_lock *waiter)
{
	int status = 0;

	lock_flocks();
	if (waiter->fl_next)
		__locks_delete_block(waiter);
	else
		status = -ENOENT;
	unlock_flocks();
	return status;
}

EXPORT_SYMBOL(posix_unblock_lock);

/**
 * vfs_cancel_lock - file byte range unblock lock
 * @filp: The file to apply the unblock to
 * @fl: The lock to be unblocked
 *
 * Used by lock managers to cancel blocked requests
 */
int vfs_cancel_lock(struct file *filp, struct file_lock *fl)
{
	if (filp->f_op && filp->f_op->lock)
		return filp->f_op->lock(filp, F_CANCELLK, fl);
	return 0;
}

EXPORT_SYMBOL_GPL(vfs_cancel_lock);

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void lock_get_status(struct seq_file *f, struct file_lock *fl,
			    loff_t id, char *pfx)
{
	struct inode *inode = NULL;
	unsigned int fl_pid;

	if (fl->fl_nspid)
		fl_pid = pid_vnr(fl->fl_nspid);
	else
		fl_pid = fl->fl_pid;

	if (fl->fl_file != NULL)
		inode = fl->fl_file->f_path.dentry->d_inode;

	seq_printf(f, "%lld:%s ", id, pfx);
	if (IS_POSIX(fl)) {
		seq_printf(f, "%6s %s ",
			     (fl->fl_flags & FL_ACCESS) ? "ACCESS" : "POSIX ",
			     (inode == NULL) ? "*NOINODE*" :
			     mandatory_lock(inode) ? "MANDATORY" : "ADVISORY ");
	} else if (IS_FLOCK(fl)) {
		if (fl->fl_type & LOCK_MAND) {
			seq_printf(f, "FLOCK  MSNFS     ");
		} else {
			seq_printf(f, "FLOCK  ADVISORY  ");
		}
	} else if (IS_LEASE(fl)) {
		seq_printf(f, "LEASE  ");
		if (lease_breaking(fl))
			seq_printf(f, "BREAKING  ");
		else if (fl->fl_file)
			seq_printf(f, "ACTIVE    ");
		else
			seq_printf(f, "BREAKER   ");
	} else {
		seq_printf(f, "UNKNOWN UNKNOWN  ");
	}
	if (fl->fl_type & LOCK_MAND) {
		seq_printf(f, "%s ",
			       (fl->fl_type & LOCK_READ)
			       ? (fl->fl_type & LOCK_WRITE) ? "RW   " : "READ "
			       : (fl->fl_type & LOCK_WRITE) ? "WRITE" : "NONE ");
	} else {
		seq_printf(f, "%s ",
			       (lease_breaking(fl))
			       ? (fl->fl_type & F_UNLCK) ? "UNLCK" : "READ "
			       : (fl->fl_type & F_WRLCK) ? "WRITE" : "READ ");
	}
	if (inode) {
#ifdef WE_CAN_BREAK_LSLK_NOW
		seq_printf(f, "%d %s:%ld ", fl_pid,
				inode->i_sb->s_id, inode->i_ino);
#else
		/* userspace relies on this representation of dev_t ;-( */
		seq_printf(f, "%d %02x:%02x:%ld ", fl_pid,
				MAJOR(inode->i_sb->s_dev),
				MINOR(inode->i_sb->s_dev), inode->i_ino);
#endif
	} else {
		seq_printf(f, "%d <none>:0 ", fl_pid);
	}
	if (IS_POSIX(fl)) {
		if (fl->fl_end == OFFSET_MAX)
			seq_printf(f, "%Ld EOF\n", fl->fl_start);
		else
			seq_printf(f, "%Ld %Ld\n", fl->fl_start, fl->fl_end);
	} else {
		seq_printf(f, "0 EOF\n");
	}
}

static int locks_show(struct seq_file *f, void *v)
{
	struct file_lock *fl, *bfl;

	fl = list_entry(v, struct file_lock, fl_link);

	lock_get_status(f, fl, *((loff_t *)f->private), "");

	list_for_each_entry(bfl, &fl->fl_block, fl_block)
		lock_get_status(f, bfl, *((loff_t *)f->private), " ->");

	return 0;
}

static void *locks_start(struct seq_file *f, loff_t *pos)
{
	loff_t *p = f->private;

	lock_flocks();
	*p = (*pos + 1);
	return seq_list_start(&file_lock_list, *pos);
}

static void *locks_next(struct seq_file *f, void *v, loff_t *pos)
{
	loff_t *p = f->private;
	++*p;
	return seq_list_next(v, &file_lock_list, pos);
}

static void locks_stop(struct seq_file *f, void *v)
{
	unlock_flocks();
}

static const struct seq_operations locks_seq_operations = {
	.start	= locks_start,
	.next	= locks_next,
	.stop	= locks_stop,
	.show	= locks_show,
};

static int locks_open(struct inode *inode, struct file *filp)
{
	return seq_open_private(filp, &locks_seq_operations, sizeof(loff_t));
}

static const struct file_operations proc_locks_operations = {
	.open		= locks_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
};

static int __init proc_locks_init(void)
{
	proc_create("locks", 0, NULL, &proc_locks_operations);
	return 0;
}
module_init(proc_locks_init);
#endif

/**
 *	lock_may_read - checks that the region is free of locks
 *	@inode: the inode that is being read
 *	@start: the first byte to read
 *	@len: the number of bytes to read
 *
 *	Emulates Windows locking requirements.  Whole-file
 *	mandatory locks (share modes) can prohibit a read and
 *	byte-range POSIX locks can prohibit a read if they overlap.
 *
 *	N.B. this function is only ever called
 *	from knfsd and ownership of locks is never checked.
 */
int lock_may_read(struct inode *inode, loff_t start, unsigned long len)
{
	struct file_lock *fl;
	int result = 1;
	lock_flocks();
	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (IS_POSIX(fl)) {
			if (fl->fl_type == F_RDLCK)
				continue;
			if ((fl->fl_end < start) || (fl->fl_start > (start + len)))
				continue;
		} else if (IS_FLOCK(fl)) {
			if (!(fl->fl_type & LOCK_MAND))
				continue;
			if (fl->fl_type & LOCK_READ)
				continue;
		} else
			continue;
		result = 0;
		break;
	}
	unlock_flocks();
	return result;
}

EXPORT_SYMBOL(lock_may_read);

/**
 *	lock_may_write - checks that the region is free of locks
 *	@inode: the inode that is being written
 *	@start: the first byte to write
 *	@len: the number of bytes to write
 *
 *	Emulates Windows locking requirements.  Whole-file
 *	mandatory locks (share modes) can prohibit a write and
 *	byte-range POSIX locks can prohibit a write if they overlap.
 *
 *	N.B. this function is only ever called
 *	from knfsd and ownership of locks is never checked.
 */
int lock_may_write(struct inode *inode, loff_t start, unsigned long len)
{
	struct file_lock *fl;
	int result = 1;
	lock_flocks();
	for (fl = inode->i_flock; fl != NULL; fl = fl->fl_next) {
		if (IS_POSIX(fl)) {
			if ((fl->fl_end < start) || (fl->fl_start > (start + len)))
				continue;
		} else if (IS_FLOCK(fl)) {
			if (!(fl->fl_type & LOCK_MAND))
				continue;
			if (fl->fl_type & LOCK_WRITE)
				continue;
		} else
			continue;
		result = 0;
		break;
	}
	unlock_flocks();
	return result;
}

EXPORT_SYMBOL(lock_may_write);

static int __init filelock_init(void)
{
	filelock_cache = kmem_cache_create("file_lock_cache",
			sizeof(struct file_lock), 0, SLAB_PANIC, NULL);

	return 0;
}

core_initcall(filelock_init);
