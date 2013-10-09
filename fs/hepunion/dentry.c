/*
 * Copyright (c) 1998-2013 Erez Zadok
 * Copyright (c) 2009	   Shrikar Archak
 * Copyright (c) 2003-2013 Stony Brook University
 * Copyright (c) 2003-2013 The Research Foundation of SUNY
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include "hepunion.h"

/*
 * returns: -ERRNO if error (returned to user)
 *          0: tell VFS to invalidate dentry
 *          1: dentry is valid
 */
static int wrapfs_d_revalidate(struct dentry *dentry, unsigned int flags)
{
	struct path lower_path;
	struct dentry *lower_dentry;
	int err = 1;
	pr_info("wrapfs d_revalidate\n");

	if (flags & LOOKUP_RCU)
		return -ECHILD;

	wrapfs_get_lower_path(dentry, &lower_path);
	lower_dentry = lower_path.dentry;
	if (!lower_dentry->d_op || !lower_dentry->d_op->d_revalidate)
		goto out;
	err = lower_dentry->d_op->d_revalidate(lower_dentry, flags);
out:
	wrapfs_put_lower_path(dentry, &lower_path);
	return err;
}

static void wrapfs_d_release(struct dentry *dentry)
{
	pr_info("wrapfs d_release\n");	
	/* release and reset the lower paths */
	wrapfs_put_reset_lower_path(dentry);
	free_dentry_private_data(dentry);
	return;
}

const struct dentry_operations wrapfs_dops = {
	.d_revalidate	= wrapfs_d_revalidate,
	.d_release	= wrapfs_d_release,
};
