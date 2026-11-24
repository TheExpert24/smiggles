#ifndef SMIGGLES_FILESYSTEM_H
#define SMIGGLES_FILESYSTEM_H

#include "../filesystem_new.h"

#define FS_O_READ   0x01
#define FS_O_WRITE  0x02
#define FS_O_CREATE 0x04
#define FS_O_TRUNC  0x08
#define FS_O_APPEND 0x10

int fs_mkdir(const char* path);
void fs_fd_init(void);
int fs_fd_open(const char* path, int flags);
int fs_fd_close(int fd);
int fs_fd_read(int fd, char* buffer, int count);
int fs_fd_write(int fd, const char* buffer, int count);
int fs_fd_seek(int fd, int offset, int whence);
int fs_fd_stat(int fd, FInode* stat_out);
void fs_fd_close_for_pid(int pid);
int fs_stat(const char* path, FInode* stat_out);
int fs_unlink(const char* path);
int fs_rmdir(const char* path);

#endif
