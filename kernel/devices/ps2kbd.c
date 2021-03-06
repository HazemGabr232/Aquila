/*
 *			PS/2 Keyboard driver
 *
 *
 *	This file is part of Aquila OS and is released under
 *	the terms of GNU GPLv3 - See LICENSE.
 *
 *	Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */


#include <core/system.h>
#include <core/arch.h>
#include <cpu/cpu.h>

#include <sys/proc.h>
#include <sys/sched.h>

#include <dev/dev.h>
#include <fs/devfs.h>

#include <ds/ring.h>
#include <ds/queue.h>

#include <bits/errno.h>

#define BUF_SIZE	128

static ring_t *kbd_ring = NEW_RING(BUF_SIZE);  	/* Keboard Ring Buffer */
static proc_t *proc = NULL;	/* Current process using Keboard */
static queue_t *kbd_read_queue = NEW_QUEUE;	/* Keyboard read queue */

void ps2kbd_handler(int scancode)
{
	ring_write(kbd_ring, sizeof(scancode), (char *) &scancode);
	
	if (kbd_read_queue->count)
		wakeup_queue(kbd_read_queue);
}

void ps2kbd_register()
{
	extern void i8042_register_handler(int channel, void (*fun)(int));
	i8042_register_handler(1, ps2kbd_handler);
}

static ssize_t ps2kbd_read(struct fs_node *node __unused, off_t offset __unused, size_t size, void *buf)
{
	return ring_read(kbd_ring, size, buf);
}

int ps2kbd_probe()
{
	ps2kbd_register();
	//kbd_ring = new_ring(BUF_SIZE);

    vfs.create(dev_root, "kbd");

    struct vfs_path path = (struct vfs_path) {
        .mountpoint = dev_root,
        .tokens = (char *[]) {"kbd", NULL}
    };

	struct fs_node *kbd = vfs.traverse(&path);

	kbd->dev = &ps2kbddev;
    kbd->read_queue = kbd_read_queue;

	return 0;
}

/* File Operations */

static int ps2kbd_file_open(struct file *file)
{
	if (proc) /* Only one process can open kbd */
		return -EACCES;

	proc = cur_proc;
	return generic_file_open(file);
}

dev_t ps2kbddev = (dev_t) {
	.name  = "kbddev",
	.type  = CHRDEV,
	.probe = ps2kbd_probe,
	.read  = ps2kbd_read,

	.f_ops = {
		.open = ps2kbd_file_open,
		.read = generic_file_read,
        .can_read = __can_always,
        .can_write = __can_never,
        .eof = __eof_never
	},
};
