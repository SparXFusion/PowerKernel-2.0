/*
 * Detect hard and soft lockups on a system
 *
 * started by Don Zickus, Copyright (C) 2010 Red Hat, Inc.
 *
 * Note: Most of this code is borrowed heavily from the original softlockup
 * detector, so thanks to Ingo for the initial implementation.
 * Some chunks also taken from the old x86-specific nmi watchdog code, thanks
 * to those contributors as well.
 */

#define pr_fmt(fmt) "NMI watchdog: " fmt

#include <linux/mm.h>
#include <linux/cpu.h>
#include <linux/nmi.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/lockdep.h>
#include <linux/notifier.h>
#include <linux/module.h>
#include <linux/sysctl.h>

#include <asm/irq_regs.h>
#include <linux/perf_event.h>

int watchdog_enabled = 1;
int __read_mostly watchdog_thresh = 10;

static DEFINE_PER_CPU(unsigned long, watchdog_touch_ts);
static DEFINE_PER_CPU(struct task_struct *, softlockup_watchdog);
static DEFINE_PER_CPU(struct hrtimer, watchdog_hrtimer);
static DEFINE_PER_CPU(bool, softlockup_touch_sync);
static DEFINE_PER_CPU(bool, soft_watchdog_warn);
#ifdef CONFIG_HARDLOCKUP_DETECTOR
static DEFINE_PER_CPU(bool, hard_watchdog_warn);
static DEFINE_PER_CPU(bool, watchdog_nmi_touch);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts);
static DEFINE_PER_CPU(unsigned long, hrtimer_interrupts_saved);
#endif
#ifdef CONFIG_HARDLOCKUP_DETECTOR_NMI
static DEFINE_PER_CPU(struct perf_event *, watchdog_ev);
#endif

/* boot commands */
/*
 * Should we panic when a soft-lockup or hard-lockup occurs:
 */
#ifdef CONFIG_HARDLOCKUP_DETECTOR
static int hardlockup_panic =
			CONFIG_BOOTPARAM_HARDLOCKUP_PANIC_VALUE;

static int __init hardlockup_panic_setup(char *str)
{
	if (!strncmp(str, "panic", 5))
		hardlockup_panic = 1;
	else if (!strncmp(str, "nopanic", 7))
		hardlockup_panic = 0;
	else if (!strncmp(str, "0", 1))
		watchdog_enabled = 0;
	return 1;
}
__setup("nmi_watchdog=", hardlockup_panic_setup);
#endif

unsigned int __read_mostly softlockup_panic =
			CONFIG_BOOTPARAM_SOFTLOCKUP_PANIC_VALUE;

static int __init softlockup_panic_setup(char *str)
{
	softlockup_panic = simple_strtoul(str, NULL, 0);

	return 1;
}
__setup("softlockup_panic=", softlockup_panic_setup);

static int __init nowatchdog_setup(char *str)
{
	watchdog_enabled = 0;
	return 1;
}
__setup("nowatchdog", nowatchdog_setup);

/* deprecated */
static int __init nosoftlockup_setup(char *str)
{
	watchdog_enabled = 0;
	return 1;
}
__setup("nosoftlockup", nosoftlockup_setup);
/*  */

/*
 * Hard-lockup warnings should be triggered after just a few seconds. Soft-
 * lockups can have false positives under extreme conditions. So we generally
 * want a higher threshold for soft lockups than for hard lockups. So we couple
 * the thresholds with a factor: we make the soft threshold twice the amount of
 * time the hard threshold is.
 */
static int get_softlockup_thresh(void)
{
	return watchdog_thresh * 2;
}

/*
 * Returns seconds, approximately.  We don't need nanosecond
 * resolution, and we don't need to waste time with a big divide when
 * 2^30ns == 1.074s.
 */
static unsigned long get_timestamp(int this_cpu)
{
	return cpu_clock(this_cpu) >> 30LL;  /* 2^30 ~= 10^9 */
}

static u64 get_sample_period(void)
{
	/*
	 * convert watchdog_thresh from seconds to ns
	 * the divide by 5 is to give hrtimer several chances (two
	 * or three with the current relation between the soft
	 * and hard thresholds) to increment before the
	 * hardlockup detector generates a warning
	 */
	return get_softlockup_thresh() * ((u64)NSEC_PER_SEC / 5);
}

/* Commands for resetting the watchdog */
static void __touch_watchdog(void)
{
	int this_cpu = smp_processor_id();

	__this_cpu_write(watchdog_touch_ts, get_timestamp(this_cpu));
}

void touch_softlockup_watchdog(void)
{
	__this_cpu_write(watchdog_touch_ts, 0);
}
EXPORT_SYMBOL(touch_softlockup_watchdog);

void touch_all_softlockup_watchdogs(void)
{
	int cpu;

	/*
	 * this is done lockless
	 * do we care if a 0 races with a timestamp?
	 * all it means is the softlock check starts one cycle later
	 */
	for_each_online_cpu(cpu)
		per_cpu(watchdog_touch_ts, cpu) = 0;
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR
void touch_nmi_watchdog(void)
{
	/*
	 * Using __raw here because some code paths have
	 * preemption enabled.  If preemption is enabled
	 * then interrupts should be enabled too, in which
	 * case we shouldn't have to worry about the watchdog
	 * going off.
	 */
	__raw_get_cpu_var(watchdog_nmi_touch) = true;
	touch_softlockup_watchdog();
}
EXPORT_SYMBOL(touch_nmi_watchdog);

#endif

void touch_softlockup_watchdog_sync(void)
{
	__raw_get_cpu_var(softlockup_touch_sync) = true;
	__raw_get_cpu_var(watchdog_touch_ts) = 0;
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR_NMI
/* watchdog detector functions */
static int is_hardlockup(void)
{
	unsigned long hrint = __this_cpu_read(hrtimer_interrupts);

	if (__this_cpu_read(hrtimer_interrupts_saved) == hrint)
		return 1;

	__this_cpu_write(hrtimer_interrupts_saved, hrint);
	return 0;
}
#endif

#ifdef CONFIG_HARDLOCKUP_DETECTOR_OTHER_CPU
static int is_hardlockup_other_cpu(int cpu)
{
	unsigned long hrint = per_cpu(hrtimer_interrupts, cpu);

	if (per_cpu(hrtimer_interrupts_saved, cpu) == hrint)
		return 1;

	per_cpu(hrtimer_interrupts_saved, cpu) = hrint;
	return 0;
}

static void watchdog_check_hardlockup_other_cpu(void)
{
	int cpu;

	/*
	 * Test for hardlockups every 3 samples.  The sample period is
	 *  watchdog_thresh * 2 / 5, so 3 samples gets us back to slightly over
	 *  watchdog_thresh (over by 20%).
	 */
	if (__this_cpu_read(hrtimer_interrupts) % 3 != 0)
		return;

	/* check for a hardlockup on the next cpu */
	cpu = cpumask_next(smp_processor_id(), cpu_online_mask);
	if (cpu >= nr_cpu_ids)
		cpu = cpumask_first(cpu_online_mask);
	if (cpu == smp_processor_id())
		return;

	if (per_cpu(watchdog_nmi_touch, cpu) == true) {
		per_cpu(watchdog_nmi_touch, cpu) = false;
		return;
	}

	if (is_hardlockup_other_cpu(cpu)) {
		/* only warn once */
		if (per_cpu(hard_watchdog_warn, cpu) == true)
			return;

		/*
		if (hardlockup_panic)
			panic("Watchdog detected hard LOCKUP on cpu %d", cpu);
		else
			WARN(1, "Watchdog detected hard LOCKUP on cpu %d", cpu);
		*/
		pr_emerg("Watchdog detected hard LOCKUP on cpu %d", cpu);
		WARN(1, "Watchdog detected hard LOCKUP on cpu %d", cpu);

		per_cpu(hard_watchdog_warn, cpu) = true;
	} else {
		per_cpu(hard_watchdog_warn, cpu) = false;
	}
}
#else
static inline void watchdog_check_hardlockup_other_cpu(void) { return; }
#endif

static int is_softlockup(unsigned long touch_ts)
{
	unsigned long now = get_timestamp(smp_processor_id());

	/* Warn about unreasonable delays: */
	if (time_after(now, touch_ts + get_softlockup_thresh()))
		return now - touch_ts;

	return 0;
}

#ifdef CONFIG_HARDLOCKUP_DETECTOR_NMI

static struct perf_event_attr wd_hw_attr = {
	.type		= PERF_TYPE_HARDWARE,
	.config		= PERF_COUNT_HW_CPU_CYCLES,
	.size		= sizeof(struct perf_event_attr),
	.pinned		= 1,
	.disabled	= 1,
};

/* Callback function for perf event subsystem */
static void watchdog_overflow_callback(struct perf_event *event,
		 struct perf_sample_data *data,
		 struct pt_regs *regs)
{
	/* Ensure the watchdog never gets throttled */
	event->hw.interrupts = 0;

	if (__this_cpu_read(watchdog_nmi_touch) == true) {
		__this_cpu_write(watchdog_nmi_touch, false);
		return;
	}

	/* check for a hardlockup
	 * This is done by making sure our timer interrupt
	 * is incrementing.  The timer interrupt should have
	 * fired multiple times before we overflow'd.  If it hasn't
	 * then this is a good indication the cpu is stuck
	 */
	if (is_hardlockup()) {
		int this_cpu = smp_processor_id();

		/* only print hardlockups once */
		if (__this_cpu_read(hard_watchdog_warn) == true)
			return;

		pr_emerg("Watchdog detected hard LOCKUP on cpu %d", cpu);
		WARN(1, "Watchdog detected hard LOCKUP on cpu %d", cpu);
		/*
		if (hardlockup_panic)
			panic("Watchdog detected hard LOCKUP on cpu %d", this_cpu);
		else
			WARN(1, "Watchdog detected hard LOCKUP on cpu %d", this_cpu);*/

		__this_cpu_write(hard_watchdog_warn, true);
		return;
	}

	__this_cpu_write(hard_watchdog_warn, false);
	return;
}
#endif

#ifdef CONFIG_HARDLOCKUP_DETECTOR
static void watchdog_interrupt_count(void)
{
	__this_cpu_inc(hrtimer_interrupts);
}
#else
static inline void watchdog_interrupt_count(void) { return; }
#endif /* CONFIG_HARDLOCKUP_DETECTOR */

/* watchdog kicker functions */
static enum hrtimer_restart watchdog_timer_fn(struct hrtimer *hrtimer)
{
	unsigned long touch_ts = __this_cpu_read(watchdog_touch_ts);
	struct pt_regs *regs = get_irq_regs();
	int duration;

	/* kick the hardlockup detector */
	watchdog_interrupt_count();

	/* test for hardlockups on the next cpu */
	watchdog_check_hardlockup_other_cpu();

	/* kick the softlockup detector */
	wake_up_process(__this_cpu_read(softlockup_watchdog));

	/* .. and repeat */
	hrtimer_forward_now(hrtimer, ns_to_ktime(get_sample_period()));

	if (touch_ts == 0) {
		if (unlikely(__this_cpu_read(softlockup_touch_sync))) {
			/*
			 * If the time stamp was touched atomically
			 * make sure the scheduler tick is up to date.
			 */
			__this_cpu_write(softlockup_touch_sync, false);
			sched_clock_tick();
		}
		__touch_watchdog();
		return HRTIMER_RESTART;
	}

	/* check for a softlockup
	 * This is done by making sure a high priority task is
	 * being scheduled.  The task touches the watchdog to
	 * indicate it is getting cpu time.  If it hasn't then
	 * this is a good indication some task is hogging the cpu
	 */
	duration = is_softlockup(touch_ts);
	if (unlikely(duration)) {
		/* only warn once */
		if (__this_cpu_read(soft_watchdog_warn) == true)
			return HRTIMER_RESTART;

		printk(KERN_EMERG "BUG: soft lockup - CPU#%d stuck for %us! [%s:%d]\n",
			smp_processor_id(), duration,
			current->comm, task_pid_nr(current));
		print_modules();
		print_irqtrace_events(current);
		if (regs)
			show_regs(regs);
		else
			dump_stack();

		if (softlockup_panic)
			panic("softlockup: hung tasks");
		__this_cpu_write(soft_watchdog_warn, true);
	} else
		__this_cpu_write(soft_watchdog_warn, false);

	return HRTIMER_RESTART;
}


/*
 * The watchdog thread - touches the timestamp.
 */
static int watchdog(void *unused)
{
	struct sched_param param = { .sched_priority = 0 };
	struct hrtimer *hrtimer = &__raw_get_cpu_var(watchdog_hrtimer);

	/* initialize timestamp */
	__touch_watchdog();

	/* kick off the timer for the hardlockup detector */
	/* done here because hrtimer_start can only pin to smp_processor_id() */
	hrtimer_start(hrtimer, ns_to_ktime(get_sample_period()),
		      HRTIMER_MODE_REL_PINNED);

	set_current_state(TASK_INTERRUPTIBLE);
	/*
	 * Run briefly (kicked by the hrtimer callback function) once every
	 * get_sample_period() seconds (4 seconds by default) to reset the
	 * softlockup timestamp. If this gets delayed for more than
	 * 2*watchdog_thresh seconds then the debug-printout triggers in
	 * watchdog_timer_fn().
	 */
	while (!kthread_should_stop()) {
		__touch_watchdog();
		schedule();

		if (kthread_should_stop())
			break;

		set_current_state(TASK_INTERRUPTIBLE);
	}
	/*
	 * Drop the policy/priority elevation during thread exit to avoid a
	 * scheduling latency spike.
	 */
	__set_current_state(TASK_RUNNING);
	sched_setscheduler(current, SCHED_NORMAL, &param);
	return 0;
}


#ifdef CONFIG_HARDLOCKUP_DETECTOR_NMI
static int watchdog_nmi_enable(int cpu)
{
	struct perf_event_attr *wd_attr;
	struct perf_event *event = per_cpu(watchdog_ev, cpu);

	/* is it already setup and enabled? */
	if (event && event->state > PERF_EVENT_STATE_OFF)
		goto out;

	/* it is setup but not enabled */
	if (event != NULL)
		goto out_enable;

	wd_attr = &wd_hw_attr;
	wd_attr->sample_period = hw_nmi_get_sample_period(watchdog_thresh);

	/* Try to register using hardware perf events */
	event = perf_event_create_kernel_counter(wd_attr, cpu, NULL, watchdog_overflow_callback, NULL);
	if (!IS_ERR(event)) {
		pr_info("enabled, takes one hw-pmu counter.\n");
		goto out_save;
	}


	/* vary the KERN level based on the returned errno */
	if (PTR_ERR(event) == -EOPNOTSUPP)
		pr_info("disabled (cpu%i): not supported (no LAPIC?)\n", cpu);
	else if (PTR_ERR(event) == -ENOENT)
		pr_warning("disabled (cpu%i): hardware events not enabled\n",
			 cpu);
	else
		pr_err("disabled (cpu%i): unable to create perf event: %ld\n",
			cpu, PTR_ERR(event));
	return PTR_ERR(event);

	/* success path */
out_save:
	per_cpu(watchdog_ev, cpu) = event;
out_enable:
	perf_event_enable(per_cpu(watchdog_ev, cpu));
out:
	return 0;
}

static void watchdog_nmi_disable(int cpu)
{
	struct perf_event *event = per_cpu(watchdog_ev, cpu);

	if (event) {
		perf_event_disable(event);
		per_cpu(watchdog_ev, cpu) = NULL;

		/* should be in cleanup, but blocks oprofile */
		perf_event_release_kernel(event);
	}
	return;
}
#else
static int watchdog_nmi_enable(int cpu) { return 0; }
static void watchdog_nmi_disable(int cpu) { return; }
#endif /* CONFIG_HARDLOCKUP_DETECTOR */

/* prepare/enable/disable routines */
static void watchdog_prepare_cpu(int cpu)
{
	struct hrtimer *hrtimer = &per_cpu(watchdog_hrtimer, cpu);

	WARN_ON(per_cpu(softlockup_watchdog, cpu));
	hrtimer_init(hrtimer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	hrtimer->function = watchdog_timer_fn;

#ifdef CONFIG_HARDLOCKUP_DETECTOR_OTHER_CPU
	/*
	 * The new cpu will be marked online before the first hrtimer interrupt
	 * runs on it.  If another cpu tests for a hardlockup on the new cpu
	 * before it has run its first hrtimer, it will get a false positive.
	 * Touch the watchdog on the new cpu to delay the first check for at
	 * least 3 sampling periods to guarantee one hrtimer has run on the new
	 * cpu.
	 */
	per_cpu(watchdog_nmi_touch, cpu) = true;
#endif
}

static int watchdog_enable(int cpu)
{
	struct task_struct *p = per_cpu(softlockup_watchdog, cpu);
	int err = 0;

	/* enable the perf event */
	err = watchdog_nmi_enable(cpu);

	/* Regardless of err above, fall through and start softlockup */

	/* create the watchdog thread */
	if (!p) {
		struct sched_param param = { .sched_priority = MAX_RT_PRIO-1 };
		p = kthread_create_on_node(watchdog, NULL, cpu_to_node(cpu), "watchdog/%d", cpu);
		if (IS_ERR(p)) {
			pr_err("softlockup watchdog for %i failed\n", cpu);
			if (!err) {
				/* if hardlockup hasn't already set this */
				err = PTR_ERR(p);
				/* and disable the perf event */
				watchdog_nmi_disable(cpu);
			}
			goto out;
		}
		sched_setscheduler(p, SCHED_FIFO, &param);
		kthread_bind(p, cpu);
		per_cpu(watchdog_touch_ts, cpu) = 0;
		per_cpu(softlockup_watchdog, cpu) = p;
		wake_up_process(p);
	}

out:
	return err;
}

static void watchdog_disable(int cpu)
{
	struct task_struct *p = per_cpu(softlockup_watchdog, cpu);
	struct hrtimer *hrtimer = &per_cpu(watchdog_hrtimer, cpu);

	/*
	 * cancel the timer first to stop incrementing the stats
	 * and waking up the kthread
	 */
	hrtimer_cancel(hrtimer);

	/* disable the perf event */
	watchdog_nmi_disable(cpu);

	/* stop the watchdog thread */
	if (p) {
		per_cpu(softlockup_watchdog, cpu) = NULL;
		kthread_stop(p);
	}
}

/* sysctl functions */
#ifdef CONFIG_SYSCTL
static void watchdog_enable_all_cpus(void)
{
	int cpu;

	watchdog_enabled = 0;

	for_each_online_cpu(cpu)
		if (!watchdog_enable(cpu))
			/* if any cpu succeeds, watchdog is considered
			   enabled for the system */
			watchdog_enabled = 1;

	if (!watchdog_enabled)
		pr_err("failed to be enabled on some cpus\n");

}

static void watchdog_disable_all_cpus(void)
{
	int cpu;

	for_each_online_cpu(cpu)
		watchdog_disable(cpu);

	/* if all watchdogs are disabled, then they are disabled for the system */
	watchdog_enabled = 0;
}


/*
 * proc handler for /proc/sys/kernel/nmi_watchdog,watchdog_thresh
 */

int proc_dowatchdog(struct ctl_table *table, int write,
		    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec_minmax(table, write, buffer, lenp, ppos);
	if (ret || !write)
		goto out;

	if (watchdog_enabled && watchdog_thresh)
		watchdog_enable_all_cpus();
	else
		watchdog_disable_all_cpus();

out:
	return ret;
}
#endif /* CONFIG_SYSCTL */


/*
 * Create/destroy watchdog threads as CPUs come and go:
 */
static int __cpuinit
cpu_callback(struct notifier_block *nfb, unsigned long action, void *hcpu)
{
	int hotcpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_UP_PREPARE:
	case CPU_UP_PREPARE_FROZEN:
		watchdog_prepare_cpu(hotcpu);
		break;
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		if (watchdog_enabled)
			watchdog_enable(hotcpu);
		break;
#ifdef CONFIG_HOTPLUG_CPU
	case CPU_UP_CANCELED:
	case CPU_UP_CANCELED_FROZEN:
		watchdog_disable(hotcpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		watchdog_disable(hotcpu);
		break;
#endif /* CONFIG_HOTPLUG_CPU */
	}

	/*
	 * hardlockup and softlockup are not important enough
	 * to block cpu bring up.  Just always succeed and
	 * rely on printk output to flag problems.
	 */
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata cpu_nfb = {
	.notifier_call = cpu_callback
};

void __init lockup_detector_init(void)
{
	void *cpu = (void *)(long)smp_processor_id();
	int err;

	err = cpu_callback(&cpu_nfb, CPU_UP_PREPARE, cpu);
	WARN_ON(notifier_to_errno(err));

	cpu_callback(&cpu_nfb, CPU_ONLINE, cpu);
	register_cpu_notifier(&cpu_nfb);

	return;
}
