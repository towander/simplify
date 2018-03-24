/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

  This program can be distributed under the terms of the GNU GPL.
  See the file COPYING.
*/

/** @file
 *
 * This file system mirrors the existing file system hierarchy of the
 * system, starting at the root file system. This is implemented by
 * just "passing through" all requests to the corresponding user-space
 * libc functions. Its performance is terrible.
 *
 * Compile with
 *
 *     gcc -Wall passthrough.c `pkg-config fuse3 --cflags --libs` -o passthrough
 *
 * ## Source code ##
 * \include passthrough.c
 */

#include <stdio.h>
#include <stdlib.h>
#include "write_function.c"
#define FUSE_USE_VERSION 30
// #define PATH_PMFS "/mnt/pmfs"
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite()/utimensat() */
#define _XOPEN_SOURCE 700
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

static void *xmp_init(struct fuse_conn_info *conn,
		      struct fuse_config *cfg)
{
	(void) conn;
	cfg->use_ino = 1;
	return NULL;
}

static int xmp_getattr(const char *path, struct stat *stbuf,
		       struct fuse_file_info *fi)
{
	(void) fi;
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	
    printf("pmfs : %s\n", pmfs);
    printf("path : %s\n", path);
    
    res = lstat(pmfs, stbuf);
	if (res == -1){
		free(pmfs);
		return -errno;
	}

	c_list lt;
	c_iterator iter, first, last;
	c_list_create(&lt, NULL);
	read_file_info(pmfs, lt);//readfile
	last = c_list_end(&lt);
	first = c_list_begin(&lt);
	off_t Filesize = 0;
    for(iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
    	struct file_info *one=((struct file_info *)ITER_REF(iter));
    	Filesize += one->size;
    }

    printf("stbuf->st_size : %ld\n", stbuf->st_size);

    stbuf->st_size = Filesize;
    
    printf("Filesize : %ld\n", Filesize);
    
    free_list(lt);
	free(pmfs);
	return 0;
}

static int xmp_access(const char *path, int mask)
{
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	res = access(pmfs, mask);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	res = readlink(pmfs, buf, size - 1);
	if (res == -1){
		free(pmfs);
		return -errno;
	}

	buf[res] = '\0';
	free(pmfs);
	return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi,
		       enum fuse_readdir_flags flags)
{
	DIR *dp;
	struct dirent *de;

	(void) offset;
	(void) fi;
	(void) flags;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	dp = opendir(pmfs);
	if (dp == NULL){
		free(pmfs);
		return -errno;
	}

	while ((de = readdir(dp)) != NULL) {
		struct stat st;
		memset(&st, 0, sizeof(st));
		st.st_ino = de->d_ino;
		st.st_mode = de->d_type << 12;
		if (filler(buf, de->d_name, &st, 0, 0))
			break;
	}

	closedir(dp);
	free(pmfs);
	return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	/* On Linux this could just be 'mknod(path, mode, rdev)' but this
	   is more portable */
	if (S_ISREG(mode)) {
		res = open(pmfs, O_CREAT | O_EXCL | O_WRONLY, mode);
		if (res >= 0)
			res = close(res);
	} else if (S_ISFIFO(mode))
		res = mkfifo(pmfs, mode);
	else
		res = mknod(pmfs, mode, rdev);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	res = mkdir(pmfs, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_unlink(const char *path)
{
	printf("-----------------------------delete-----------------------------\n");
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);

	printf("absolute path is: %s\n", pmfs);
	printf("opposite path is: %s\n", path);

/* To delete the file we have 3 way
 * 1. when we let the num-1, we check the num, if the num==0, we delete the file_block.  // this way is most easy, but also easy bug. when the eletricity is disable suddently, so ... 
 * 2. we let all the file_block num-1, then search all(all block of all file) block, which num == 0, we delete it.    // this way cost to much time. also we can say 2 is the finish version of 3. 
 * 3. we let all the file_block num-1, at the same time, we save the block name, then we according the name list to delete the file_block.    // this way may be the best, but we need some time to to do the best. Half of thing can not do anything.
 */
	c_list lt, delete_file_block, temp_list;
	c_list_create(&lt, NULL);
	c_list_create(&temp_list, NULL);
	c_list_create(&delete_file_block, NULL);
	read_file_info(pmfs, lt);

	c_map map_new;
    c_map_create(&map_new, char_comparer);    //创建map
	read_map(temp_list, map_new);

	c_iterator iter, first, last, target, map_end;
    last = c_list_end(&lt);
    first = c_list_begin(&lt);
    map_end = c_map_end(&map_new);
    for(iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
        struct file_info *file_node=((struct file_info *)ITER_REF(iter));
        printf("oldlist_key--->%s \n", file_node->hash);
        target = c_map_find(&map_new, &file_node->hash);
        printf("寻找key值完毕%s\n", file_node->hash);
        if(strcmp("", file_node->hash) == 0)
            break;
        if(!ITER_EQUAL(map_end, target))
        {
            if(*(int*)(((c_ppair)ITER_REF(target))->second)>0)
            {
                *(int*)(((c_ppair)ITER_REF(target))->second)=*(int*)(((c_ppair)ITER_REF(target))->second)-1;
                printf(" 现在的操作是value-1\n" );
                printf("map操作成功\n");
                printf("key = %s   value=%d\n",((c_ppair)ITER_REF(target))->first, *(int *)((c_ppair)ITER_REF(target))->second);

				if ( *(int *)((c_ppair)ITER_REF(target))->second == 0 )
				{
					c_list_push_back(&delete_file_block, ((c_ppair)ITER_REF(target))->first);
				}
            }
            else
            {
                printf("map状态错误:--->");
                printf("key = %s   value=%d\n",((c_ppair)ITER_REF(target))->first, *(int *)((c_ppair)ITER_REF(target))->second);
                printf(" 现在的操作是value-1\n" );
            }
        }
        else
        {
            printf("map状态错误:--->");
            printf("key = %s   value=%d\n",((c_ppair)ITER_REF(target))->first, *(int *)((c_ppair)ITER_REF(target))->second);
            printf(" 现在的操作是value-1\n" );
            printf("请使用回滚操作恢复文件表\n");
        }
    }

	printf("----------to delele : \n");

    last = c_list_end(&delete_file_block);
    first = c_list_begin(&delete_file_block);
	int res;
    for(iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
		char* one = ((char *)ITER_REF(iter));
		char *delete_block = malloc(41 + strlen(PATH_PMFS_BUF));    // malloc(strlen(((c_ppair)ITER_REF(target))->first) + strlen(PATH_PMFS_BUF));
		strcpy(delete_block, PATH_PMFS_BUF);
		strcat(delete_block, one);
		res = unlink(delete_block);
		if (res == -1)
			printf("  删除失败 : ");
		else
			printf("  删除成功 : ");
		printf("%s\n", one);
	}

	res = unlink(pmfs);
	if (res == -1)
		printf("  删除失败 : %s\n", pmfs);
	else
		printf("  删除成功 : %s\n", pmfs);

	save_map(temp_list);
	printf("^^^^^^^^^^^^^^^^^^^^^^^^^^^^-delete-^^^^^^^^^^^^^^^^^^^^^^^^^^^^\n");
}

static int xmp_rmdir(const char *path)
{
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);

	res = rmdir(pmfs);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_rename(const char *from, const char *to, unsigned int flags)
{
	int res;

	if (flags)
		return -EINVAL;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int xmp_chmod(const char *path, mode_t mode,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	res = chmod(pmfs, mode);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid,
		     struct fuse_file_info *fi)
{
	(void) fi;
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	res = lchown(pmfs, uid, gid);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}

static int xmp_truncate(const char *path, off_t size,
			struct fuse_file_info *fi)
{
	(void) fi;
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	res = truncate(pmfs, size);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}

#ifdef HAVE_UTIMENSAT
static int xmp_utimens(const char *path, const struct timespec ts[2],
		       struct fuse_file_info *fi)
{
	(void) fi;
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, pmfs, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}
#endif

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	res = open(pmfs, fi->flags);
	if (res == -1){
		free(pmfs);
		return -errno;
	}

	close(res);
	free(pmfs);
	return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
    printf("###################################################\n");

    printf("path : %s\n", path);
    printf("size : %zu\n", size);
    printf("offset : %ld\n", offset);

    char *pmfs = malloc(strlen(path) + strlen(PATH_PMFS));    // absolute path
	strcpy(pmfs, PATH_PMFS);
	strcat(pmfs, path);

    c_list file_message;
    c_list_create(&file_message, NULL);
    read_file_info(pmfs, file_message);

    c_iterator iter, first, last;
    int file_size = 0;
    last = c_list_end(&file_message);
    first = c_list_begin(&file_message);
    for (iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
        struct file_info *one = ((struct file_info *)ITER_REF(iter));
        file_size += one->size;
    }

    if ( file_size <= offset)
    {
        return 0;
    }
    if (size <= 0)
    {
        buf[0] = '\0';
        return 0;
    }
    
    memset(buf, '\0', size);    // it is redundance ?

    if( offset < 0)
    {
        memset(buf, '\0', offset);
    }
    
    int rsize = size;
    last = c_list_end(&file_message);
    first = c_list_begin(&file_message);
    for (iter = first; !ITER_EQUAL(iter, last) && size != 0; ITER_INC(iter))
    {        
        struct file_info *one = ((struct file_info *)ITER_REF(iter));

        // check
        printf("@ before read %s\n @ buf : %lu\n", one->hash, strlen(buf));
        printf("hash : %s\n", one->hash);
        printf("last_mark : %d\n", one->last_mark);
        printf("file_size : %zu\n", one->size);

        if( offset >= granularity )
        {
            offset = offset - granularity;
            continue;
        }

        if ( 0 == one->last_mark )
        {
            char filebuf[granularity] = "\0";
            int openflag = copy_file_to_filebuf("", one->hash, one->size, filebuf);

            if ( size >= (granularity - offset))
            {
                strncat(buf, filebuf + offset, granularity - offset);
                size -= granularity;
            }
            else
            {
                strncat(buf, filebuf + offset, size);
                size = 0;
            }
            offset = 0;    // it is redundance ? when more than 1 time.
        }
        else if( 1 == one->last_mark )
        {

            char filebuf[granularity] = "\0";
            int openflag = copy_file_to_filebuf("", one->hash, one->size, filebuf);
            
            if ( size >= (one->size - offset))
            {
                strncat(buf, filebuf + offset, one->size - offset);
                size -= one->size;
            }
            else
            {
                strncat(buf, filebuf + offset, size);
                size = 0;
            }
            offset = 0;    // it is redundance ? when more than 1 time.

        }

    }

    // printf("\n%s\n", buf);
    printf("%lu\n", rsize-size);
    printf("###################################################\n");

    return rsize-size;
}

static int xmp_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	// int fd;
	// int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	(void) fi;

	//
	unsigned char hashreturn[20]={0};
	char hashtostr[41];
	memset(hashtostr,'\0',sizeof(hashtostr));
	size_t daxiao=size;//daxiao
	off_t juli=offset;//kaishi
	c_list lt;
	c_list_create(&lt, NULL);
	c_iterator iter, first, last;
	read_file_info(pmfs,lt);//readfile
	last = c_list_end(&lt);
	first = c_list_begin(&lt);
	off_t buf_juli=0;
	int i=1;
    for(iter = first; !ITER_EQUAL(iter, last); ITER_INC(iter))
    {
        printf("NO.%dFILE\n", i);
        i++;
        struct file_info *one=((struct file_info *)ITER_REF(iter));
        printf("%s\n", one->hash);
        printf("%d\n", one->last_mark);
        printf("%zu\n", one->size);
        if(one->last_mark==0)
        {
            if(granularity>juli)// 这个文件写入
            {
                printf("granularity>juli--->%ld\n", juli);
                off_t nowjuli=juli;//开始写入位置
                juli=0;
                size_t nowdaxia=granularity-nowjuli;//可以写入的的大小
                if(nowdaxia<=daxiao-buf_juli)
                {
                    char filebuf[granularity]={0};
                    if(copy_file_to_filebuf(path,one->hash,one->size,filebuf)==0)
                    {
                        memcpy(filebuf+nowjuli,buf+buf_juli,nowdaxia);//写入filebuf
                        buf_juli=buf_juli+nowdaxia;
                        printf("buf_juli--->%ld\n", buf_juli);
                        buf_sha_calculate(filebuf,granularity,hashreturn);//计算
                        saveHash(hashreturn, SHA_DIGEST_LENGTH,hashtostr); //转换为40位16进制哈希值
                        printf("%s\n",hashtostr );
                        strcpy(one->hash, hashtostr);
                        save_temporary_filebuf_to_file(hashtostr,one->size,filebuf);//先保存临时文件
                    }
                }
                else//nowdaxia>daxiao-buf_juli
                {
                    char filebuf[granularity]={0};
                    if(copy_file_to_filebuf(path,one->hash,one->size,filebuf)==0)
                    {
                        memcpy(filebuf+nowjuli,buf+buf_juli,daxiao-buf_juli);//写入filebuf
                        printf("buf_juli--->%ld\n", buf_juli);
                        buf_sha_calculate(filebuf,granularity,hashreturn);//计算
                        saveHash(hashreturn, SHA_DIGEST_LENGTH,hashtostr); //转换为40位16进制哈希值
                        printf("%s\n",hashtostr );
                        strcpy(one->hash, hashtostr);
                        buf_juli=buf_juli+daxiao-buf_juli;
                        save_temporary_filebuf_to_file(hashtostr,one->size,filebuf);//先保存临时文件
                        break;//写完了
                    }
                }
            }
            else//nowdaxia>daxiao
            {
                juli=juli-granularity;
            }
        }
        else//最后一个
        {
        // if(strcmp(one->hash,"")==0)//扩展出来的空文件
            printf("lastfile+juli--->%ld\n", juli);
            if(granularity>=juli)// 这个文件写入
            {
                printf("granularity>juli=--->%ld\n", juli);
                off_t nowjuli=juli;//开始写入位置
                juli=0;
                size_t nowdaxia=granularity-nowjuli;//可以写入的的大小
                if(nowdaxia<daxiao-buf_juli)
                {
                    printf("daxiao--->%zu\n", daxiao);
                    printf("buf_juli--->%ld\n", buf_juli);
                    char filebuf[granularity]={0};
                    int openflag=copy_file_to_filebuf(path,one->hash,one->size,filebuf);
                    if(openflag==0|strcmp(one->hash,"")==0)
                    {
                        if(one->size<nowjuli)
                        {
                            memset(filebuf+one->size, '\0', nowjuli-one->size); 
                        }
                        memcpy(filebuf+nowjuli,buf+buf_juli,nowdaxia);//写入filebuf
                        buf_juli=buf_juli+nowdaxia;
                        buf_sha_calculate(filebuf,nowjuli+nowdaxia,hashreturn);//计算
                        saveHash(hashreturn, SHA_DIGEST_LENGTH,hashtostr); //转换为40位16进制哈希值
                        printf("%s\n",hashtostr );
                        strcpy(one->hash, hashtostr);
                        one->size=nowjuli+nowdaxia;
                        one->last_mark=0;
                        save_temporary_filebuf_to_file(hashtostr,one->size,filebuf);//先保存临时文件
                        //扩展
                        insert_empty_node(lt);
                        last = c_list_end(&lt);
                    }
                    else//既不是扩展的空文件也没找到文件
                    {
                        printf("NOFOUND!!!!!ERROR!!!!!!!!--->%s\n", one->hash);
                    }
                }
                else//nowdaxia>=daxiao-buf_juli
                {
                    char filebuf[granularity]={0};
                    int openflag=copy_file_to_filebuf(path,one->hash,one->size,filebuf);
                    if(openflag==0|strcmp(one->hash,"")==0)
                    {
                        if(one->size<nowjuli)
                        {
                            memset(filebuf+one->size, '\0', nowjuli-one->size); 
                        }
                        memcpy(filebuf+nowjuli,buf+buf_juli,daxiao-buf_juli);//写入filebuf
                        printf("nowjuli+nowdaxia--->%lu\n", nowjuli+nowdaxia);
                        buf_sha_calculate(filebuf,nowjuli+daxiao-buf_juli,hashreturn);//计算
                        saveHash(hashreturn, SHA_DIGEST_LENGTH,hashtostr); //转换为40位16进制哈希值
                        printf("%s\n",hashtostr );
                        strcpy(one->hash, hashtostr);
                        one->size=nowjuli+daxiao-buf_juli;
                        buf_juli=buf_juli+daxiao-buf_juli;
                        save_temporary_filebuf_to_file(hashtostr,one->size,filebuf);//先保存临时文件
                        break;//写完了
                    }
                    else//既不是扩展的空文件也没找到文件
                    {
                        printf("NOFOUND!!!!!ERROR!!!!!!!!--->%s\n", one->hash);
                    }
                }
            }
            else//granularity<juli
            {
                char filebuf[granularity]={0};
                int openflag=copy_file_to_filebuf(path,one->hash,one->size,filebuf);
                if(openflag==0|strcmp(one->hash,"")==0)
                {
                    memset(filebuf+one->size, '\0', granularity-one->size); 
                    buf_sha_calculate(filebuf,granularity,hashreturn);//计算
                    saveHash(hashreturn, SHA_DIGEST_LENGTH,hashtostr); //转换为40位16进制哈希值
                    printf("%s\n",hashtostr );
                    strcpy(one->hash, hashtostr);
                    one->size=granularity;
                    one->last_mark=0;
                    save_temporary_filebuf_to_file(hashtostr,one->size,filebuf);//先保存临时文件
                    //扩展
                     insert_empty_node(lt);
                     last = c_list_end(&lt);
                }
                else//既不是扩展的空文件也没找到文件
                {
                    printf("NOFOUND!!!!!ERROR!!!!!!!!--->%s\n", one->hash);
                }
                juli=juli-granularity;
            }
        }
    }
    c_list lt_old, file_map_list;
    c_list_create(&lt_old, NULL);
    c_list_create(&file_map_list, NULL);
    c_map map_new;
    c_map_create(&map_new, char_comparer);    //创建map
    read_file_info(pmfs, lt_old);    //readfile
    save_file_info(pmfs, lt);
     //修改文件引用表
    printf("%s\n", "!!!!------------>读取map开始" );
    read_map(file_map_list, map_new);
    print_map(&map_new);
    change_map(lt_old, lt, map_new, file_map_list);
    print_map(&map_new);
    save_map(file_map_list);
    //
    c_map_destroy(&map_new);
    free_list(lt_old);
    free_list(file_map_list);
    free_list(lt);
    free(pmfs);
    return buf_juli;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
	int res;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);

	res = statvfs(pmfs, stbuf);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) fi;
	return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	/* Just a stub.	 This method is optional and can safely be left
	   unimplemented */

	(void) path;
	(void) isdatasync;
	(void) fi;
	return 0;
}

#ifdef HAVE_POSIX_FALLOCATE
static int xmp_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
	int fd;
	int res;

	(void) fi;

	if (mode)
		return -EOPNOTSUPP;
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	fd = open(pmfs, O_WRONLY);
	if (fd == -1){
		free(pmfs);
		return -errno;
	}

	res = -posix_fallocate(fd, offset, length);

	close(fd);
	free(pmfs);
	return res;
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	int res = lsetxattr(pmfs, name, value, size, flags);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	int res = lgetxattr(pmfs, name, value, size);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	int res = llistxattr(pmfs, list, size);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
	char *pmfs=malloc(strlen(path)+strlen(PATH_PMFS));
	strcpy(pmfs,PATH_PMFS);
	strcat(pmfs,path);
	int res = lremovexattr(pmfs, name);
	if (res == -1){
		free(pmfs);
		return -errno;
	}
	free(pmfs);
	return 0;

}
#endif /* HAVE_SETXATTR */

static struct fuse_operations xmp_oper = {
	.init           = xmp_init,
	.getattr	= xmp_getattr,
	.access		= xmp_access,
	.readlink	= xmp_readlink,
	.readdir	= xmp_readdir,
	.mknod		= xmp_mknod,
	.mkdir		= xmp_mkdir,
	.symlink	= xmp_symlink,
	.unlink		= xmp_unlink,
	.rmdir		= xmp_rmdir,
	.rename		= xmp_rename,
	.link		= xmp_link,
	.chmod		= xmp_chmod,
	.chown		= xmp_chown,
	.truncate	= xmp_truncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= xmp_utimens,
#endif
	.open		= xmp_open,
	.read		= xmp_read,
	.write		= xmp_write,
	.statfs		= xmp_statfs,
	.release	= xmp_release,
	.fsync		= xmp_fsync,
#ifdef HAVE_POSIX_FALLOCATE
	.fallocate	= xmp_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= xmp_setxattr,
	.getxattr	= xmp_getxattr,
	.listxattr	= xmp_listxattr,
	.removexattr	= xmp_removexattr,
#endif
};

int main(int argc, char *argv[])
{
	umask(0);
	return fuse_main(argc, argv, &xmp_oper, NULL);
}
