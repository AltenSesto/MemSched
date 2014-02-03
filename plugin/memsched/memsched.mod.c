/* 
 * Copyright (c) 2014 MDH.
 * All rights reserved. This program and the accompanying materials
 * are made available under the terms of the GNU Public License v3.0
 * which accompanies this distribution, and is available at
 * http://www.gnu.org/licenses/gpl.html
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
	{ 0x35b6b772, "param_ops_charp" },
	{ 0x8edb28cd, "active_next_prio_task" },
	{ 0x528c904, "install_scheduler" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xc917e655, "debug_smp_processor_id" },
	{ 0xb3267a31, "dequeue_task" },
	{ 0x8834396c, "mod_timer" },
	{ 0xfb0e29f, "init_timer_key" },
	{ 0xc996d097, "del_timer" },
	{ 0xccb19959, "active_queue_unlock" },
	{ 0x5d6a57a9, "active_highest_prio_task" },
	{ 0xc2f60ae1, "active_queue_lock" },
	{ 0xbe13da8a, "uninstall_scheduler" },
	{ 0x7d11c268, "jiffies" },
	{ 0x68e2f221, "_raw_spin_unlock" },
	{ 0x33dd0f4c, "enqueue_task" },
	{ 0x93985dfa, "wake_up_process" },
	{ 0xd5f2172f, "del_timer_sync" },
	{ 0x67f7403e, "_raw_spin_lock" },
	{ 0x12c4fdf8, "perf_event_release_kernel" },
	{ 0x4f68e5c9, "do_gettimeofday" },
	{ 0x46608fa0, "getnstimeofday" },
	{ 0x2fa8dfc5, "getCurrentTask" },
	{ 0x707f93dd, "preempt_schedule" },
	{ 0xbed60566, "sub_preempt_count" },
	{ 0xf4547af3, "perf_event_create_kernel_counter" },
	{ 0x4c6ff041, "add_preempt_count" },
	{ 0x2a07b1d, "migrate_task" },
	{ 0x3bd1b1f6, "msecs_to_jiffies" },
	{ 0x50eedeb8, "printk" },
	{ 0xb4390f9a, "mcount" },
};

static const char __module_depends[]
__used
__attribute__((section(".modinfo"))) =
"depends=resch";


MODULE_INFO(srcversion, "35A19226024C8B5BBA1B804");
