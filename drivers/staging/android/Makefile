ccflags-y += -I$(src)			# needed for trace events


obj-$(CONFIG_ANDROID_BINDER_IPC)	+= binder.o binder32.o binder_helper.o sysprop_helper.o
CFLAGS_binder32.o := -DBINDER_IPC_32BIT
obj-$(CONFIG_ANDROID_RAM_CONSOLE)	+= ram_console.o
obj-$(CONFIG_ANDROID_TIMED_OUTPUT)	+= timed_output.o
obj-$(CONFIG_ANDROID_TIMED_GPIO)	+= timed_gpio.o
obj-$(CONFIG_ANDROID_DOZE_HELPER)	+= doze_helper.o
obj-$(CONFIG_ANDROID_LOW_MEMORY_KILLER)	+= lowmemorykiller.o
obj-$(CONFIG_ANDROID_INTF_ALARM_DEV)	+= alarm-dev.o
