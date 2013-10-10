/*
  * Copyright (c) 1-01 Erez Zadok
  * Copyright (c) 00      Shrikar Archak
  * Copyright (c) 00-01 Stony Brook University
  * Copyright (c) 00-01 The Research Foundation of SUNY
  *
  * This program is free software; you can redistribute it and/or modify
  * it under the terms of the GNU General Public License version  as
  * published by the Free Software Foundation.
  */

 #include "hepunion.h"
 #include <linux/module.h>
 #include <linux/fs.h>	

static int make_path(const char *s, size_t n, char **path)
{
	pr_info("make_path: %s, %zu, %p\n", s, n, path);

	/* Zero output */
	*path = NULL;

	/* First of all, look if it is relative path */
	if (s[0] != '/')
	 {
		pr_info("Received a relative path - forbidden: %s\n", s);
		return -EINVAL;
	}

	/* Tailing has to be removed */
	if (s[n - 1] == '/') 
	{
	--n;
	}
#if 1
	/* Allocate one more ('\0') */
	*path = kmalloc((n + 1) * sizeof(char), GFP_NOFS);
	if (*path)
	{
		memcpy(*path, s, n);
		(*path)[n] = '\0';
        	return n;
	}
#endif
       pr_info("Failed allocating new path\n");

       return 1;//-ENOMEM;
}

 /*
  * There is no need to lock the wrapfs_super_info's rwsem as there is no
  * way anyone can have a reference to the superblock at this point in time.
  */
 static int wrapfs_read_super(struct super_block *sb, void *raw_data, int silent)
 {
         int err = 0, forced_ro = 0;
         struct super_block *lower_sb;
         struct path lower_path;
	 struct path upper_path;
         char *arg = (char *) raw_data;
         struct inode *inode;
	 char *output, *type, *part2;
	 char *read_write_branch, *read_only_branch;
         size_t rw_len=0, ro_len=0;
	 struct file *filp;	
	 struct inode * rw_inode;
	 struct inode * ro_inode;
	 struct wrapfs_inode_info *inode_info;
	 

         if (!arg) {
                 printk(KERN_ERR
                        "wrapfs: read_super: missing arg argument\n");
                 err = -EINVAL;
                 goto out;
         }
	 
	 pr_info("get_branches: %p, %s\n", sb, arg);

        /* We are expecting 2 branches, separated by : */
        part2 = strchr(arg, ':');
        if (!part2) 
	{
		pr_err("Failed finding ':'\n");
		return -EINVAL;
	}

	/* Look for first branch type */
	type = strchr(arg, '=');

	/* First branch has a type */
	if (type && type < part2) 
	{
		/* Get branch name */
		err = make_path(arg, type - arg, &output);
		if (err < 0 || !output) 
			return err;
		if (!strncmp(type + 1, "RW", 2))
		 {
			read_write_branch = output;
			rw_len = err;
		 }
		else if (strncmp(type + 1, "RO", 2))
		 {
			pr_err("Unrecognized branch type: %.2s\n", type + 1);
			return -EINVAL;
		 }
		else
		 {
			read_only_branch = output;
			ro_len = err;
			forced_ro = 1;
		 }

		/* Get type for second branch */
		type = strchr(part2, '=');
	}
	/* It has no type => RO */
	else
	{
		/* Get branch name */
		err = make_path(arg, part2 - arg, &read_only_branch);
		if (err < 0 || !read_only_branch) 
			return err;

		ro_len = err;
	}

	/* Skip : */
	part2++;

	/* If second branch has a type */
	if (type)
	 {
		/* Get branch name */
		err = make_path(part2, type - part2, &output);
		if (err < 0 || !output) 
			return err;

		if (!strncmp(type + 1, "RW", 2))
		 {
			if (read_write_branch) 
			{
				pr_err("Attempted to provide two RW branches\n");
				return -EINVAL;
			}
			read_write_branch = output;
			rw_len = err;
		}
		else if (strncmp(type + 1, "RO", 2))
		{
			pr_err("Unrecognized branch type: %.2s\n", type + 1);
			return -EINVAL;
		}
		else 
		{	
			if (forced_ro)
         		 {
				pr_err("No RW branch provided\n");
				return -EINVAL;
			 }
			read_only_branch = output;
			ro_len = err;
		}
	}
	else 
	{
		/* It has no type, adapt given the situation */
		if (read_write_branch)
		 {
			err = make_path(part2, strlen(part2), &read_only_branch);
			if (err < 0 || !read_only_branch) 
				return err;
			ro_len = err;
		}
		else if (read_only_branch)
		{
			err = make_path(part2, strlen(part2), &read_write_branch);
			if (err < 0 || !read_write_branch)
				return err;
			rw_len = err;
		}
	}

	/* At this point, we should have the two branches set */
	if (!read_only_branch || !read_write_branch)
	 {
		pr_err("One branch missing. Read-write: %s\nRead-only: %s\n", read_write_branch, read_only_branch);
		return -EINVAL;
	 }

	pr_info("Read-write: %s\nRead-only: %s\n", read_write_branch, read_only_branch);
	pr_info("Read-write length: %zu\nRead-only length: %zu\n", rw_len, ro_len);

	

         /* parse lower path */
         err = kern_path(read_only_branch, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
                         &lower_path);
         if (err) {
                 printk(KERN_ERR "wrapfs: error accessing "
                        "lower directory '%s'\n", arg);
                 goto out;
         }

         /* parse upper path */
         err = kern_path(read_write_branch, LOOKUP_FOLLOW | LOOKUP_DIRECTORY,
                         &upper_path);
         if (err) {
                 printk(KERN_ERR "wrapfs: error accessing "
                        "upper directory '%s'\n", arg);
                 goto out;
         }


         
         /* set the lower superblock field of upper superblock */
         lower_sb = lower_path.dentry->d_sb;
         atomic_inc(&lower_sb->s_active);
         
         /* inherit maxbytes from lower file system */
         sb->s_maxbytes = lower_sb->s_maxbytes;

         /*
          * Our c/m/atime granularity is 1 ns because we may stack on file
          * systems whose granularity is as good.
          */
         sb->s_time_gran = 1;

         //sb->s_op = &wrapfs_sops;
         
	 //RW INODE 
	 filp = filp_open(read_write_branch, O_RDONLY, 0);
	 if (IS_ERR(filp)) 
	 {
		pr_err("Failed opening RW branch!\n");
		return PTR_ERR(filp);
	 }
 	 rw_inode = filp->f_dentry->d_inode;
	 filp_close(filp, NULL);
         
         //RO INODE
	 filp = filp_open(read_only_branch, O_RDONLY, 0);
	 if (IS_ERR(filp)) 
	 {
		pr_err("Failed opening RO branch!\n");
		return PTR_ERR(filp);
	 }
 	 ro_inode = filp->f_dentry->d_inode;
	 filp_close(filp, NULL);

         /* get a new inode and allocate our root dentry */
         inode = wrapfs_iget(sb, lower_path.dentry->d_inode);
         if (IS_ERR(inode)) {
                 err = PTR_ERR(inode);
                 goto out_sput;
         }
        
	
	sb->s_root = d_make_root(inode);
         if (!sb->s_root) {
                 err = -ENOMEM;
                 goto out_iput;
         }
       
         sb->s_root->d_fsdata = NULL;
         err = new_dentry_private_data(sb->s_root);
         if (err)
                 goto out_freeroot;

         /* if get here: cannot have error */

         /* set the lower dentries for s_root */
         wrapfs_set_lower_path(sb->s_root, &lower_path);

         /*
          * No need to call interpose because we already have a positive
          * dentry, which was instantiated by d_make_root.  Just need to
          * d_rehash it.
          */
         d_rehash(sb->s_root);
         if (!silent)
                 printk(KERN_INFO
                      "wrapfs: mounted on top of %s type %s\n",
                        arg, lower_sb->s_type->name);
         goto out; /* all is well */

	 inode_info->upper_inode = rw_inode;	
  	 inode_info->lower_inode = ro_inode;	
	 sb->s_fs_info = inode_info;

 out_freeroot:
         dput(sb->s_root);
 out_iput:
         iput(inode);
 out_sput:
         /* drop refs we took earlier */
         atomic_dec(&lower_sb->s_active);
         kfree(WRAPFS_SB(sb));
         sb->s_fs_info = NULL;
 
 out:
         return err;
 }

 struct dentry *wrapfs_mount(struct file_system_type *fs_type, int flags,
                             const char *arg, void *raw_data)
 {
         void *lower_path_name = (void *) arg;

         return mount_nodev(fs_type, flags, lower_path_name,
                            wrapfs_read_super);
 }

static struct file_system_type hepunion_fs_type = {
	.owner		= THIS_MODULE,
	.name		= HEPUNION_NAME,//4 change
 	.mount		= wrapfs_mount,
 	.kill_sb	= generic_shutdown_super,
	.fs_flags	= FS_REVAL_DOT
};
//5 change
static int __init init_hepunion_fs(void) {
	int err;
	
	err = wrapfs_init_dentry_cache();
	if (err)
		goto out;
	err =  register_filesystem(&hepunion_fs_type);
out:
	if (err) {
		wrapfs_destroy_dentry_cache();
	}
	return err;
}

static void __exit exit_hepunion_fs(void) {
	wrapfs_destroy_dentry_cache();
	unregister_filesystem(&hepunion_fs_type);
}

MODULE_AUTHOR("Erez Zadok, Filesystems and Storage Lab, Stony Brook University"
               " (http://www.fsl.cs.sunysb.edu/)");
 
MODULE_LICENSE("GPL");

module_init(init_hepunion_fs);
module_exit(exit_hepunion_fs);
