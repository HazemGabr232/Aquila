/**********************************************************************
 *                  Device Filesystem (devfs) handler
 *
 *
 *  This file is part of Aquila OS and is released under the terms of
 *  GNU GPLv3 - See LICENSE.
 *
 *  Copyright (C) 2016 Mohamed Anwar <mohamed_anwar@opmbx.org>
 */

#include <core/system.h>
#include <core/string.h>
#include <core/panic.h>

#include <mm/mm.h>

#include <dev/dev.h>
#include <fs/vfs.h>
#include <fs/devfs.h>

#include <bits/errno.h>
#include <bits/dirent.h>

/* devfs root directory (usually mounted on '/dev') */
struct fs_node *dev_root = NULL;

static struct fs_node *devfs_find(struct fs_node *dir, const char *fn)
{
    if (dir->type != FS_DIR)
        return NULL;

    struct devfs_dir *_dir = (struct devfs_dir *) dir->p;

    if (!_dir)  /* Directory not initialized */
        return NULL;

    forlinked (file, _dir, file->next) {
        if (!strcmp(file->node->name, fn))
            return file->node;
    }

    return NULL;    /* File not found */
}

static struct fs_node *devfs_traverse(struct vfs_path *path)
{
    //printk("devfs_traverse(path=%p)\n", path);
    struct fs_node *dir = path->mountpoint;

    foreach (token, path->tokens) {
        dir = devfs_find(dir, token);
    }

    return dir;
}

static ssize_t devfs_read(struct fs_node *node, off_t offset, size_t size, void *buf)
{
    if (!node->dev) /* is node connected to a device handler? */
        return -EINVAL;

    return node->dev->read(node, offset, size, buf);
}

static ssize_t devfs_write(struct fs_node *node, off_t offset, size_t size, void *buf)
{
    if (!node->dev) /* is node connected to a device handler? */
        return -EINVAL;

    return node->dev->write(node, offset, size, buf);
}

static int devfs_create(struct fs_node *dir, const char *name)
{
    struct fs_node *node = kmalloc(sizeof(struct fs_node));

    if (!node)
        return -ENOMEM;

    memset(node, 0, sizeof(struct fs_node));
    
    node->name = strdup(name);

    if (!node->name)
        goto e_nomem;

    node->type = FS_FILE;
    node->fs   = &devfs;
    node->size = 0;

    struct devfs_dir *_dir = (struct devfs_dir *) dir->p;
    struct devfs_dir *tmp  = kmalloc(sizeof(struct devfs_dir));

    if (!tmp)
        goto e_nomem;

    tmp->next = _dir;
    tmp->node = node;

    dir->p = tmp;

    return 0;

e_nomem:    /* Error: No Memory */
    if (node) {
        if (node->name)
            kfree(node->name);
        kfree(node);
    }

    if (tmp)
        kfree(tmp);

    return -ENOMEM;
}

static int devfs_mkdir(struct fs_node *parent, const char *name)
{
    int ret = 0;
    ret = devfs_create(parent, name);

    if (ret)
        return ret;

    struct fs_node *d = devfs_find(parent, name);

    if (!d) {
        panic("Directory not found!");
    }

    d->type = FS_DIR;

    return 0;
}

static int devfs_ioctl(struct fs_node *file, int request, void *argp)
{
    if (!file || !file->dev || !file->dev->ioctl)
        return -EBADFD;

    return file->dev->ioctl(file, request, argp);
}

static ssize_t devfs_readdir(struct fs_node *dir, off_t offset, struct dirent *dirent)
{
    //printk("devfs_readdir(dir=%p, offset=%d, dirent=%p)\n", dir, offset, dirent);
    int i = 0;
    struct devfs_dir *_dir = (struct devfs_dir *) dir->p;

    if (!_dir)
        return 0;

    int found = 0;

    forlinked (e, _dir, e->next) {
        if (i == offset) {
            found = 1;
            strcpy(dirent->d_name, e->node->name);   // FIXME
            break;
        }
        ++i;
    }

    return found;
}


/* ================ File Operations ================ */

static int devfs_file_open(struct file *file)
{
    if (file->node->type == FS_DIR) {
        return 0;
    } else {
        if (!file->node->dev)
            return -ENXIO;
        
        return file->node->dev->f_ops.open(file);
    }
}

static ssize_t devfs_file_read(struct file *file, void *buf, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;

    return file->node->dev->f_ops.read(file, buf, size);
}

static ssize_t devfs_file_write(struct file *file, void *buf, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;
    
    return file->node->dev->f_ops.write(file, buf, size);
}

static int devfs_file_can_read(struct file *file, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;

    return file->node->dev->f_ops.can_read(file, size);
}

static int devfs_file_can_write(struct file *file, size_t size)
{
    if (!file->node->dev)
        return -ENXIO;

    return file->node->dev->f_ops.can_write(file, size);
}

static int devfs_file_eof(struct file *file)
{
    if (!file->node->dev)
        return -ENXIO;

    return file->node->dev->f_ops.eof(file);
}

int devfs_init()
{
    //printk("[0] Kernel: devfs -> init()\n");
    dev_root = kmalloc(sizeof(struct fs_node));

    if (!dev_root)
        return -ENOMEM;

    dev_root->name = "dev";
    dev_root->type = FS_DIR;
    dev_root->size = 0;
    dev_root->fs   = &devfs;
    dev_root->p    = NULL;

    return 0;
}

struct fs devfs = {
    .name   = "devfs",
    .init   = devfs_init,
    .create = devfs_create,
    .mkdir  = devfs_mkdir,
    .find   = devfs_find,
    .traverse = devfs_traverse,
    .read   = devfs_read,
    .write  = devfs_write,
    .ioctl  = devfs_ioctl,
    .readdir = devfs_readdir,
    
    .f_ops = {
        .open  = devfs_file_open,
        .read  = devfs_file_read,
        .write = devfs_file_write, 
        .readdir = generic_file_readdir,

        .can_read = devfs_file_can_read,
        .can_write = devfs_file_can_write,
        .eof = devfs_file_eof,
    },
};
