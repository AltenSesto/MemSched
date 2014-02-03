/* 
 * Copyright (c) 2014 Shinpei Kato and Mikael Åsberg.
 * All rights reserved. This program and the accompanying materials are 
 * made available under the terms of the GNU Public License v3.0 which 
 * accompanies this distribution, and is available at 
 * http://www.gnu.org/licenses/gpl.htm
 */
#include <linux/module.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

MODULE_INFO(vermagic, VERMAGIC_STRING);

struct module __this_module
__attribute__((section(".gnu.linkonce.this_module"))) = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

static const struct modversion_info ____versions[]
__used
__attribute__((section("__versions"))) = {
	{ 0x18148c2e, "module_layout" },
	{ 0x7c743b8e, "cdev_del" },
	{ 0x9ab3533f, "cdev_init" },
	{ 0x68e2f221, "_raw_spin_unlock" },
	{ 0xcea98580, "send_sig" },
	{ 0x60cb2061, "find_vpid" },
	{ 0xb54533f7, "usecs_to_jiffies" },
	{ 0xfb0e29f, "init_timer_key" },
	{ 0x7485e15e, "unregister_chrdev_region" },
	{ 0x99763af1, "kthread_create_on_node" },
	{ 0x7d11c268, "jiffies" },
	{ 0xc4cf0988, "kthread_bind" },
	{ 0xd5f2172f, "del_timer_sync" },
	{ 0xf97456ea, "_raw_spin_unlock_irqrestore" },
	{ 0x76f3ebaf, "current_task" },
	{ 0x50eedeb8, "printk" },
	{ 0x2345ed10, "kthread_stop" },
	{ 0xc917e655, "debug_smp_processor_id" },
	{ 0xb4390f9a, "mcount" },
	{ 0x98503a08, "_raw_spin_unlock_irq" },
	{ 0x8faf527a, "set_cpus_allowed_ptr" },
	{ 0xdd1a2871, "down" },
	{ 0x8834396c, "mod_timer" },
	{ 0xa8cb2a35, "pid_task" },
	{ 0x82ec72f4, "cdev_add" },
	{ 0xc6cbbc89, "capable" },
	{ 0x8ff4079b, "pv_irq_ops" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0x3bd1b1f6, "msecs_to_jiffies" },
	{ 0x4292364c, "schedule" },
	{ 0xf1faac3a, "_raw_spin_lock_irq" },
	{ 0x7f24de73, "jiffies_to_usecs" },
	{ 0x93985dfa, "wake_up_process" },
	{ 0x67f7403e, "_raw_spin_lock" },
	{ 0x21fb443e, "_raw_spin_lock_irqsave" },
	{ 0x347d96a7, "sched_setscheduler" },
	{ 0xd2965f6f, "kthread_should_stop" },
	{ 0x37ff4c06, "copy_from_user_overflow" },
	{ 0x4f68e5c9, "do_gettimeofday" },
	{ 0xc4554217, "up" },
	{ 0x362ef408, "_copy_from_user" },
	{ 0x29537c9e, "alloc_chrdev_region" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=";


MODULE_INFO(srcversion, "58AE272DF2ACD518FDCB159");
