/*
 * kernel/power/main.c - PM subsystem core functionality.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 * 
 * This file is released under the GPLv2
 *
 */

#include <linux/export.h>
#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>
#include <linux/hrtimer.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>

#if defined(CONFIG_CPU_EXYNOS4210)
#define CONFIG_GPU_LOCK
#define CONFIG_ROTATION_BOOSTER_SUPPORT
#endif

#if defined(CONFIG_CPU_EXYNOS4412) && defined(CONFIG_MALI400) \
			&& defined(CONFIG_MALI_DVFS)
#define CONFIG_EXYNOS4_GPU_LOCK
#endif

#include <linux/cpufreq.h>
#include <mach/cpufreq.h>

#ifdef CONFIG_GPU_LOCK
#include <mach/gpufreq.h>
#endif

#if defined(CONFIG_CPU_EXYNOS4412) && defined(CONFIG_VIDEO_MALI400MP) \
		&& defined(CONFIG_VIDEO_MALI400MP_DVFS)
#define CONFIG_PEGASUS_GPU_LOCK
extern int mali_dvfs_bottom_lock_push(int lock_step);
extern int mali_dvfs_bottom_lock_pop(void);
#endif

#include "power.h"

#define MAX_BUF 100

DEFINE_MUTEX(pm_mutex);

#ifdef CONFIG_PM_SLEEP

/* Routines for PM-transition notifications */

static BLOCKING_NOTIFIER_HEAD(pm_chain_head);

static void touch_event_fn(struct work_struct *work);
static DECLARE_WORK(touch_event_struct, touch_event_fn);

static struct hrtimer tc_ev_timer;
static int tc_ev_processed;
static ktime_t touch_evt_timer_val;

int register_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_pm_notifier);

int unregister_pm_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(&pm_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_pm_notifier);

int pm_notifier_call_chain(unsigned long val)
{
	return (blocking_notifier_call_chain(&pm_chain_head, val, NULL)
			== NOTIFY_BAD) ? -EINVAL : 0;
}

/* If set, devices may be suspended and resumed asynchronously. */
int pm_async_enabled = 1;

static ssize_t pm_async_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", pm_async_enabled);
}

static ssize_t pm_async_store(struct kobject *kobj, struct kobj_attribute *attr,
			      const char *buf, size_t n)
{
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	if (val > 1)
		return -EINVAL;

	pm_async_enabled = val;
	return n;
}

power_attr(pm_async);

static ssize_t
touch_event_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	if (tc_ev_processed == 0)
		return snprintf(buf, strnlen("touch_event", MAX_BUF) + 1,
				"touch_event");
	else
		return snprintf(buf, strnlen("null", MAX_BUF) + 1,
				"null");
}

static ssize_t
touch_event_store(struct kobject *kobj,
		  struct kobj_attribute *attr,
		  const char *buf, size_t n)
{

	hrtimer_cancel(&tc_ev_timer);
	tc_ev_processed = 0;

	/* set a timer to notify the userspace to stop processing
	 * touch event
	 */
	hrtimer_start(&tc_ev_timer, touch_evt_timer_val, HRTIMER_MODE_REL);

	/* wakeup the userspace poll */
	sysfs_notify(kobj, NULL, "touch_event");

	return n;
}

power_attr(touch_event);

static ssize_t
touch_event_timer_show(struct kobject *kobj,
		 struct kobj_attribute *attr, char *buf)
{
	return snprintf(buf, MAX_BUF, "%lld", touch_evt_timer_val.tv64);
}

static ssize_t
touch_event_timer_store(struct kobject *kobj,
			struct kobj_attribute *attr,
			const char *buf, size_t n)
{
	unsigned long val;

	if (strict_strtoul(buf, 10, &val))
		return -EINVAL;

	touch_evt_timer_val = ktime_set(0, val*1000);

	return n;
}

power_attr(touch_event_timer);

static void touch_event_fn(struct work_struct *work)
{
	/* wakeup the userspace poll */
	tc_ev_processed = 1;
	sysfs_notify(power_kobj, NULL, "touch_event");

	return;
}

static enum hrtimer_restart tc_ev_stop(struct hrtimer *hrtimer)
{

	schedule_work(&touch_event_struct);

	return HRTIMER_NORESTART;
}

#ifdef CONFIG_PM_DEBUG
int pm_test_level = TEST_NONE;

static const char * const pm_tests[__TEST_AFTER_LAST] = {
	[TEST_NONE] = "none",
	[TEST_CORE] = "core",
	[TEST_CPUS] = "processors",
	[TEST_PLATFORM] = "platform",
	[TEST_DEVICES] = "devices",
	[TEST_FREEZER] = "freezer",
};

static ssize_t pm_test_show(struct kobject *kobj, struct kobj_attribute *attr,
				char *buf)
{
	char *s = buf;
	int level;

	for (level = TEST_FIRST; level <= TEST_MAX; level++)
		if (pm_tests[level]) {
			if (level == pm_test_level)
				s += sprintf(s, "[%s] ", pm_tests[level]);
			else
				s += sprintf(s, "%s ", pm_tests[level]);
		}

	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';

	return (s - buf);
}

static ssize_t pm_test_store(struct kobject *kobj, struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	const char * const *s;
	int level;
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	mutex_lock(&pm_mutex);

	level = TEST_FIRST;
	for (s = &pm_tests[level]; level <= TEST_MAX; s++, level++)
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len)) {
			pm_test_level = level;
			error = 0;
			break;
		}

	mutex_unlock(&pm_mutex);

	return error ? error : n;
}

power_attr(pm_test);
#endif /* CONFIG_PM_DEBUG */

#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_DEBUG_FS
static char *suspend_step_name(enum suspend_stat_step step)
{
	switch (step) {
	case SUSPEND_FREEZE:
		return "freeze";
	case SUSPEND_PREPARE:
		return "prepare";
	case SUSPEND_SUSPEND:
		return "suspend";
	case SUSPEND_SUSPEND_NOIRQ:
		return "suspend_noirq";
	case SUSPEND_RESUME_NOIRQ:
		return "resume_noirq";
	case SUSPEND_RESUME:
		return "resume";
	default:
		return "";
	}
}

static int suspend_stats_show(struct seq_file *s, void *unused)
{
	int i, index, last_dev, last_errno, last_step;

	last_dev = suspend_stats.last_failed_dev + REC_FAILED_NUM - 1;
	last_dev %= REC_FAILED_NUM;
	last_errno = suspend_stats.last_failed_errno + REC_FAILED_NUM - 1;
	last_errno %= REC_FAILED_NUM;
	last_step = suspend_stats.last_failed_step + REC_FAILED_NUM - 1;
	last_step %= REC_FAILED_NUM;
	seq_printf(s, "%s: %d\n%s: %d\n%s: %d\n%s: %d\n"
			"%s: %d\n%s: %d\n%s: %d\n%s: %d\n",
			"success", suspend_stats.success,
			"fail", suspend_stats.fail,
			"failed_freeze", suspend_stats.failed_freeze,
			"failed_prepare", suspend_stats.failed_prepare,
			"failed_suspend", suspend_stats.failed_suspend,
			"failed_suspend_noirq",
				suspend_stats.failed_suspend_noirq,
			"failed_resume", suspend_stats.failed_resume,
			"failed_resume_noirq",
				suspend_stats.failed_resume_noirq);
	seq_printf(s,	"failures:\n  last_failed_dev:\t%-s\n",
			suspend_stats.failed_devs[last_dev]);
	for (i = 1; i < REC_FAILED_NUM; i++) {
		index = last_dev + REC_FAILED_NUM - i;
		index %= REC_FAILED_NUM;
		seq_printf(s, "\t\t\t%-s\n",
			suspend_stats.failed_devs[index]);
	}
	seq_printf(s,	"  last_failed_errno:\t%-d\n",
			suspend_stats.errno[last_errno]);
	for (i = 1; i < REC_FAILED_NUM; i++) {
		index = last_errno + REC_FAILED_NUM - i;
		index %= REC_FAILED_NUM;
		seq_printf(s, "\t\t\t%-d\n",
			suspend_stats.errno[index]);
	}
	seq_printf(s,	"  last_failed_step:\t%-s\n",
			suspend_step_name(
				suspend_stats.failed_steps[last_step]));
	for (i = 1; i < REC_FAILED_NUM; i++) {
		index = last_step + REC_FAILED_NUM - i;
		index %= REC_FAILED_NUM;
		seq_printf(s, "\t\t\t%-s\n",
			suspend_step_name(
				suspend_stats.failed_steps[index]));
	}

	return 0;
}

static int suspend_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, suspend_stats_show, NULL);
}

static const struct file_operations suspend_stats_operations = {
	.open           = suspend_stats_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};

static int __init pm_debugfs_init(void)
{
	debugfs_create_file("suspend_stats", S_IFREG | S_IRUGO,
			NULL, NULL, &suspend_stats_operations);
	return 0;
}

late_initcall(pm_debugfs_init);
#endif /* CONFIG_DEBUG_FS */

struct kobject *power_kobj;

/**
 *	state - control system power state.
 *
 *	show() returns what states are supported, which is hard-coded to
 *	'standby' (Power-On Suspend), 'mem' (Suspend-to-RAM), and
 *	'disk' (Suspend-to-Disk).
 *
 *	store() accepts one of those strings, translates it into the 
 *	proper enumerated value, and initiates a suspend transition.
 */
static ssize_t state_show(struct kobject *kobj, struct kobj_attribute *attr,
			  char *buf)
{
	char *s = buf;
#ifdef CONFIG_SUSPEND
	int i;

	for (i = 0; i < PM_SUSPEND_MAX; i++) {
		if (pm_states[i] && valid_state(i))
			s += sprintf(s,"%s ", pm_states[i]);
	}
#endif
#ifdef CONFIG_HIBERNATION
	s += sprintf(s, "%s\n", "disk");
#else
	if (s != buf)
		/* convert the last space to a newline */
		*(s-1) = '\n';
#endif
	return (s - buf);
}
#ifdef CONFIG_FAST_BOOT
bool fake_shut_down = false;
EXPORT_SYMBOL(fake_shut_down);

extern void wakelock_force_suspend(void);
#endif

static ssize_t state_store(struct kobject *kobj, struct kobj_attribute *attr,
			   const char *buf, size_t n)
{
#ifdef CONFIG_SUSPEND
#ifdef CONFIG_EARLYSUSPEND
	suspend_state_t state = PM_SUSPEND_ON;
#else
	suspend_state_t state = PM_SUSPEND_STANDBY;
#endif
	const char * const *s;
#endif
	char *p;
	int len;
	int error = -EINVAL;

	p = memchr(buf, '\n', n);
	len = p ? p - buf : n;

	/* First, check if we are requested to hibernate */
	if (len == 4 && !strncmp(buf, "disk", len)) {
		error = hibernate();
  goto Exit;
	}

#ifdef CONFIG_SUSPEND
	for (s = &pm_states[state]; state < PM_SUSPEND_MAX; s++, state++) {
		if (*s && len == strlen(*s) && !strncmp(buf, *s, len))
			break;
	}

#ifdef CONFIG_FAST_BOOT
	if (len == 4 && !strncmp(buf, "dmem", len)) {
		pr_info("%s: fake shut down!!!\n", __func__);
		fake_shut_down = true;
		state = PM_SUSPEND_MEM;
	}
#endif

	if (state < PM_SUSPEND_MAX && *s) {
#ifdef CONFIG_EARLYSUSPEND
		if (state == PM_SUSPEND_ON || valid_state(state)) {
			error = 0;
			request_suspend_state(state);
		}
#ifdef CONFIG_FAST_BOOT
		if (fake_shut_down)
			wakelock_force_suspend();
#endif
#else
		error = enter_state(state);
		suspend_stats_update(error);
#endif
	}
#endif

 Exit:
	return error ? error : n;
}

power_attr(state);

#ifdef CONFIG_PM_SLEEP
/*
 * The 'wakeup_count' attribute, along with the functions defined in
 * drivers/base/power/wakeup.c, provides a means by which wakeup events can be
 * handled in a non-racy way.
 *
 * If a wakeup event occurs when the system is in a sleep state, it simply is
 * woken up.  In turn, if an event that would wake the system up from a sleep
 * state occurs when it is undergoing a transition to that sleep state, the
 * transition should be aborted.  Moreover, if such an event occurs when the
 * system is in the working state, an attempt to start a transition to the
 * given sleep state should fail during certain period after the detection of
 * the event.  Using the 'state' attribute alone is not sufficient to satisfy
 * these requirements, because a wakeup event may occur exactly when 'state'
 * is being written to and may be delivered to user space right before it is
 * frozen, so the event will remain only partially processed until the system is
 * woken up by another event.  In particular, it won't cause the transition to
 * a sleep state to be aborted.
 *
 * This difficulty may be overcome if user space uses 'wakeup_count' before
 * writing to 'state'.  It first should read from 'wakeup_count' and store
 * the read value.  Then, after carrying out its own preparations for the system
 * transition to a sleep state, it should write the stored value to
 * 'wakeup_count'.  If that fails, at least one wakeup event has occurred since
 * 'wakeup_count' was read and 'state' should not be written to.  Otherwise, it
 * is allowed to write to 'state', but the transition will be aborted if there
 * are any wakeup events detected after 'wakeup_count' was written to.
 */

static ssize_t wakeup_count_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	unsigned int val;

	return pm_get_wakeup_count(&val) ? sprintf(buf, "%u\n", val) : -EINTR;
}

static ssize_t wakeup_count_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	unsigned int val;

	if (sscanf(buf, "%u", &val) == 1) {
		if (pm_save_wakeup_count(val))
			return n;
	}
	return -EINVAL;
}

power_attr(wakeup_count);
#endif /* CONFIG_PM_SLEEP */

#ifdef CONFIG_PM_TRACE
int pm_trace_enabled;

static ssize_t pm_trace_show(struct kobject *kobj, struct kobj_attribute *attr,
			     char *buf)
{
	return sprintf(buf, "%d\n", pm_trace_enabled);
}

static ssize_t
pm_trace_store(struct kobject *kobj, struct kobj_attribute *attr,
	       const char *buf, size_t n)
{
	int val;

	if (sscanf(buf, "%d", &val) == 1) {
		pm_trace_enabled = !!val;
		return n;
	}
	return -EINVAL;
}

power_attr(pm_trace);

static ssize_t pm_trace_dev_match_show(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *buf)
{
	return show_trace_dev_match(buf, PAGE_SIZE);
}

static ssize_t
pm_trace_dev_match_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t n)
{
	return -EINVAL;
}

power_attr(pm_trace_dev_match);

#endif /* CONFIG_PM_TRACE */

#ifdef CONFIG_USER_WAKELOCK
power_attr(wake_lock);
power_attr(wake_unlock);
#endif

int cpufreq_max_limit_val = -1;
int cpufreq_max_limit_coupled = SCALING_MAX_UNDEFINED; /* Yank555.lu - not yet defined at startup */
static int cpufreq_min_limit_val = -1;
DEFINE_MUTEX(cpufreq_limit_mutex);

static ssize_t cpufreq_table_show(struct kobject *kobj,
				struct kobj_attribute *attr,
				char *buf)
{
	ssize_t count = 0;
	struct cpufreq_frequency_table *table;
	struct cpufreq_policy *policy;
	unsigned int min_freq = ~0;
	unsigned int max_freq = 0;
	unsigned int i = 0;

	table = cpufreq_frequency_get_table(0);
	if (!table) {
		printk(KERN_ERR "%s: Failed to get the cpufreq table\n",
			__func__);
		return sprintf(buf, "Failed to get the cpufreq table\n");
	}

	policy = cpufreq_cpu_get(0);
	if (policy) {
	#if 0 /* /sys/devices/system/cpu/cpu0/cpufreq/scaling_min&max_freq */
		min_freq = policy->min_freq;
		max_freq = policy->max_freq;
	#else /* /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min&max_freq */
/*		min_freq = policy->cpuinfo.min_freq;
		max_freq = policy->cpuinfo.max_freq;*/
		min_freq = policy->min; /* Yank555.lu :                                            */
		max_freq = policy->max; /*   use govenor's min/max scaling to limit the freq table */
	#endif
	}

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++) {
		if ((table[i].frequency == CPUFREQ_ENTRY_INVALID) ||
		    (table[i].frequency > max_freq) ||
		    (table[i].frequency < min_freq))
			continue;
		count += sprintf(&buf[count], "%d ", table[i].frequency);
	}
	count += sprintf(&buf[count], "\n");

	return count;
}

static ssize_t cpufreq_table_store(struct kobject *kobj,
				struct kobj_attribute *attr,
				const char *buf, size_t n)
{
	printk(KERN_ERR "%s: cpufreq_table is read-only\n", __func__);
	return -EINVAL;
}

#define VALID_LEVEL 1
int get_cpufreq_level(unsigned int freq, unsigned int *level)
{
	struct cpufreq_frequency_table *table;
	unsigned int i = 0;

	table = cpufreq_frequency_get_table(0);
	if (!table) {
		printk(KERN_ERR "%s: Failed to get the cpufreq table\n",
			__func__);
		return -EINVAL;
	}

	for (i = 0; (table[i].frequency != CPUFREQ_TABLE_END); i++)
		if (table[i].frequency == freq) {
			*level = i;
			return VALID_LEVEL;
		}

	printk(KERN_ERR "%s: %u KHz is an unsupported cpufreq\n",
		__func__, freq);
	return -EINVAL;
}

static ssize_t cpufreq_max_limit_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", cpufreq_max_limit_val);
}

static ssize_t cpufreq_max_limit_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int val;
	unsigned int cpufreq_level;
	int lock_ret;
	ssize_t ret = -EINVAL;
	struct cpufreq_policy *policy;

	mutex_lock(&cpufreq_limit_mutex);

	if (sscanf(buf, "%d", &val) != 1) {
		printk(KERN_ERR "%s: Invalid cpufreq format\n", __func__);
		goto out;
	}

	if (val == -1) { /* Unlock request */
		if (cpufreq_max_limit_val != -1) {
			exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_USER);
			/* Yank555.lu - unlock now means set lock to scaling max to support powersave mode properly */
			/* cpufreq_max_limit_val = -1; */
			policy = cpufreq_cpu_get(0);
			if (get_cpufreq_level(policy->max, &cpufreq_level) == VALID_LEVEL) {
				lock_ret = exynos_cpufreq_upper_limit(DVFS_LOCK_ID_USER, cpufreq_level);
				cpufreq_max_limit_val = policy->max;
				cpufreq_max_limit_coupled = SCALING_MAX_COUPLED;
			}
		}
	} else { /* Lock request */
		if (get_cpufreq_level((unsigned int)val, &cpufreq_level) == VALID_LEVEL) {
			if (cpufreq_max_limit_val != -1) {
				/* Unlock the previous lock */
				exynos_cpufreq_upper_limit_free(DVFS_LOCK_ID_USER);
				cpufreq_max_limit_coupled = SCALING_MAX_UNCOUPLED; /* if a limit existed, uncouple */
			} else {
				cpufreq_max_limit_coupled = SCALING_MAX_COUPLED; /* if no limit existed, we're booting, couple */
			}
			lock_ret = exynos_cpufreq_upper_limit(DVFS_LOCK_ID_USER, cpufreq_level);
			/* ret of exynos_cpufreq_upper_limit is meaningless.
			   0 is fail? success? */
			cpufreq_max_limit_val = val;
		}
	}

	ret = n;
out:
	mutex_unlock(&cpufreq_limit_mutex);
	return ret;
}

static ssize_t cpufreq_min_limit_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", cpufreq_min_limit_val);
}

static ssize_t cpufreq_min_limit_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int val;
	unsigned int cpufreq_level;
	int lock_ret;
	ssize_t ret = -EINVAL;

	mutex_lock(&cpufreq_limit_mutex);

	if (sscanf(buf, "%d", &val) != 1) {
		printk(KERN_ERR "%s: Invalid cpufreq format\n", __func__);
		goto out;
	}

	if (val == -1) { /* Unlock request */
		if (cpufreq_min_limit_val != -1) {
			exynos_cpufreq_lock_free(DVFS_LOCK_ID_USER);
			cpufreq_min_limit_val = -1;
		}
	} else { /* Lock request */
		if (get_cpufreq_level((unsigned int)val, &cpufreq_level)
			== VALID_LEVEL) {
			if (cpufreq_min_limit_val != -1)
				/* Unlock the previous lock */
				exynos_cpufreq_lock_free(DVFS_LOCK_ID_USER);
			lock_ret = exynos_cpufreq_lock(
					DVFS_LOCK_ID_USER, cpufreq_level);
			/* ret of exynos_cpufreq_lock is meaningless.
			   0 is fail? success? */
			cpufreq_min_limit_val = val;
		if ((cpufreq_max_limit_val != -1) &&
			    (cpufreq_min_limit_val > cpufreq_max_limit_val))
				printk(KERN_ERR "%s: Min lock may not work well"
					" because of Max lock\n", __func__);
		} else /* Invalid lock request --> No action */
			printk(KERN_ERR "%s: Lock request is invalid\n",
				__func__);
	}

	ret = n;
out:
	mutex_unlock(&cpufreq_limit_mutex);
	return ret;
}

power_attr(cpufreq_table);
power_attr(cpufreq_max_limit);
power_attr(cpufreq_min_limit);

#ifdef CONFIG_GPU_LOCK
static int gpu_lock_val;
DEFINE_MUTEX(gpu_lock_mutex);

static ssize_t gpu_lock_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", gpu_lock_val);
}

static ssize_t gpu_lock_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int val;
	ssize_t ret = -EINVAL;

	mutex_lock(&gpu_lock_mutex);

	if (sscanf(buf, "%d", &val) != 1) {
		pr_info("%s: Invalid mali lock format\n", __func__);
		goto out;
	}

	if (val == 0) {
		if (gpu_lock_val != 0) {
			exynos_gpufreq_unlock();
			gpu_lock_val = 0;
		}
	} else if (val == 1) {
		if (gpu_lock_val == 0) {
			exynos_gpufreq_lock();
			gpu_lock_val = val;
		}
	} else {
		pr_info("%s: Lock request is invalid\n", __func__);
	}

	ret = n;
out:
	mutex_unlock(&gpu_lock_mutex);
	return ret;
}
power_attr(gpu_lock);
#endif

#ifdef CONFIG_ROTATION_BOOSTER_SUPPORT
static inline void rotation_booster_on(void)
{
	exynos_cpufreq_lock(DVFS_LOCK_ID_ROTATION_BOOSTER, L0);
	exynos4_busfreq_lock(DVFS_LOCK_ID_ROTATION_BOOSTER, BUS_L0);
	exynos_gpufreq_lock();
}

static inline void rotation_booster_off(void)
{
	exynos_gpufreq_unlock();
	exynos4_busfreq_lock_free(DVFS_LOCK_ID_ROTATION_BOOSTER);
	exynos_cpufreq_lock_free(DVFS_LOCK_ID_ROTATION_BOOSTER);
}

static int rotation_booster_val;
DEFINE_MUTEX(rotation_booster_mutex);

static ssize_t rotation_booster_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "%d\n", rotation_booster_val);
}

static ssize_t rotation_booster_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int val;
	ssize_t ret = -EINVAL;

	mutex_lock(&rotation_booster_mutex);

	if (sscanf(buf, "%d", &val) != 1) {
		pr_info("%s: Invalid rotation_booster on, off format\n", \
			__func__);
		goto out;
	}

	if (val == 0) {
		if (rotation_booster_val != 0) {
			rotation_booster_off();
			rotation_booster_val = 0;
		} else {
			pr_info("%s: rotation_booster off request"
				" is ignored\n", __func__);
		}
	} else if (val == 1) {
		if (rotation_booster_val == 0) {
			rotation_booster_on();
			rotation_booster_val = val;
		} else {
			pr_info("%s: rotation_booster on request"
				" is ignored\n", __func__);
		}
	} else {
		pr_info("%s: rotation_booster request is invalid\n", __func__);
	}

	ret = n;
out:
	mutex_unlock(&rotation_booster_mutex);
	return ret;
}
power_attr(rotation_booster);
#else /* CONFIG_ROTATION_BOOSTER_SUPPORT */
static inline void rotation_booster_on(void){}
static inline void rotation_booster_off(void){}
#endif /* CONFIG_ROTATION_BOOSTER_SUPPORT */

#ifdef CONFIG_PEGASUS_GPU_LOCK
static int mali_lock_val;
static int mali_lock_cnt;
DEFINE_MUTEX(mali_lock_mutex);

static ssize_t mali_lock_show(struct kobject *kobj,
					struct kobj_attribute *attr,
					char *buf)
{
	return sprintf(buf, "level = %d, count = %d\n",
			mali_lock_val, mali_lock_cnt);
}

static ssize_t mali_lock_store(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t n)
{
	int val;
	ssize_t ret = -EINVAL;

	mutex_lock(&mali_lock_mutex);

	if (sscanf(buf, "%d", &val) != 1) {
		pr_info("%s: Invalid mali lock format\n", __func__);
		goto out;
	}

	if (val == 0) {	/* unlock */
		mali_lock_cnt = mali_dvfs_bottom_lock_pop();
		if (mali_lock_cnt == 0)
			mali_lock_val = 0;
	} else if (val > 0 && val < 5) { /* lock with level */
		mali_lock_cnt = mali_dvfs_bottom_lock_push(val);
		if (mali_lock_val < val)
			mali_lock_val = val;
	} else {
		pr_info("%s: Lock request is invalid\n", __func__);
	}

	ret = n;
out:
	mutex_unlock(&mali_lock_mutex);
	return ret;
}
power_attr(mali_lock);
#endif

static struct attribute *g[] = {
	&state_attr.attr,
#ifdef CONFIG_PM_TRACE
	&pm_trace_attr.attr,
	&pm_trace_dev_match_attr.attr,
#endif
#ifdef CONFIG_PM_SLEEP
	&pm_async_attr.attr,
	&wakeup_count_attr.attr,
	&touch_event_attr.attr,
	&touch_event_timer_attr.attr,
#ifdef CONFIG_PM_DEBUG
	&pm_test_attr.attr,
#endif
#ifdef CONFIG_USER_WAKELOCK
	&wake_lock_attr.attr,
	&wake_unlock_attr.attr,
#endif
#endif
	&cpufreq_table_attr.attr,
	&cpufreq_max_limit_attr.attr,
	&cpufreq_min_limit_attr.attr,
#ifdef CONFIG_PEGASUS_GPU_LOCK
	&mali_lock_attr.attr,
#endif
#ifdef CONFIG_ROTATION_BOOSTER_SUPPORT
	&rotation_booster_attr.attr,
#endif
	NULL,
};

static struct attribute_group attr_group = {
	.attrs = g,
};

#ifdef CONFIG_PM_RUNTIME
struct workqueue_struct *pm_wq;
EXPORT_SYMBOL_GPL(pm_wq);

static int __init pm_start_workqueue(void)
{
	pm_wq = alloc_workqueue("pm", WQ_FREEZABLE, 0);

	return pm_wq ? 0 : -ENOMEM;
}
#else
static inline int pm_start_workqueue(void) { return 0; }
#endif

static int __init pm_init(void)
{
	int error = pm_start_workqueue();
	if (error)
		return error;
	hibernate_image_size_init();
	hibernate_reserved_size_init();

	touch_evt_timer_val = ktime_set(2, 0);
	hrtimer_init(&tc_ev_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	tc_ev_timer.function = &tc_ev_stop;
	tc_ev_processed = 1;

	power_kobj = kobject_create_and_add("power", NULL);
	if (!power_kobj)
		return -ENOMEM;
	return sysfs_create_group(power_kobj, &attr_group);
}

core_initcall(pm_init);
