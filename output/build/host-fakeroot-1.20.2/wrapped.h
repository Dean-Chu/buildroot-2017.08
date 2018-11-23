/* Automatically generated file. Do not edit. Edit wrapawk/wrapfunc.inp. */
#ifndef WRAPPED_H
#define WRAPPED_H


extern int (*NEXT_LSTAT_NOARG)LSTAT_ARG(int ver, const char *file_name, struct stat *buf);
extern int (*NEXT_STAT_NOARG)STAT_ARG(int ver, const char *file_name, struct stat *buf);
extern int (*NEXT_FSTAT_NOARG)FSTAT_ARG(int ver, int fd, struct stat *buf);
#ifdef HAVE_FSTATAT
extern int (*NEXT_FSTATAT_NOARG)FSTATAT_ARG(int ver, int dir_fd, const char *path, struct stat *buf, int flags);
#endif /* HAVE_FSTATAT */

#ifdef STAT64_SUPPORT
extern int (*NEXT_LSTAT64_NOARG)LSTAT64_ARG(int ver, const char *file_name, struct stat64 *buf);
extern int (*NEXT_STAT64_NOARG)STAT64_ARG(int ver, const char *file_name, struct stat64 *buf);
extern int (*NEXT_FSTAT64_NOARG)FSTAT64_ARG(int ver, int fd, struct stat64 *buf);
#ifdef HAVE_FSTATAT
extern int (*NEXT_FSTATAT64_NOARG)FSTATAT64_ARG(int ver, int dir_fd, const char *path, struct stat64 *buf, int flags);
#endif /* HAVE_FSTATAT */
#endif /* STAT64_SUPPORT */

#ifdef __APPLE__
#ifdef __LP64__
extern int (*next_getattrlist)(const char *path, void *attrList, void *attrBuf, size_t attrBufSize, unsigned int options);
#ifdef HAVE_FGETATTRLIST
extern int (*next_fgetattrlist)(int fd, void *attrList, void *attrBuf, size_t attrBufSize, unsigned int options);
#endif
#else
extern int (*next_getattrlist)(const char *path, void *attrList, void *attrBuf, size_t attrBufSize, unsigned long options);
#ifdef HAVE_FGETATTRLIST
extern int (*next_fgetattrlist)(int fd, void *attrList, void *attrBuf, size_t attrBufSize, unsigned long options);
#endif
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
extern int (*next_getattrlist$UNIX2003)(const char *path, void *attrList, void *attrBuf, size_t attrBufSize, unsigned long options);
#endif
#endif
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
extern int (*next_lstat$INODE64)(const char *file_name, struct stat *buf);
extern int (*next_stat$INODE64)(const char *file_name, struct stat *buf);
extern int (*next_fstat$INODE64)(int fd, struct stat *buf);
extern int (*next_posix_spawn)(pid_t * __restrict pid, const char * __restrict path, const posix_spawn_file_actions_t *file_actions, const posix_spawnattr_t * __restrict attrp, char *const argv[ __restrict], char *const envp[ __restrict]);
extern int (*next_posix_spawnp)(pid_t * __restrict pid, const char * __restrict path, const posix_spawn_file_actions_t *file_actions, const posix_spawnattr_t * __restrict attrp, char *const argv[ __restrict], char *const envp[ __restrict]);
#endif
extern int (*next_execve)(const char *path, char *const argv[], char *const envp[]);
extern int (*next_execv)(const char *path, char *const argv[]);
extern int (*next_execvp)(const char *file, char *const argv[]);
extern int (*next_execvP)(const char *file, const char *search_path, char *const argv[]);
#endif /* ifdef __APPLE__ */

extern int (*NEXT_MKNOD_NOARG)MKNOD_ARG(int ver, const char *pathname, mode_t mode, dev_t XMKNOD_FRTH_ARG dev);

#ifdef HAVE_FSTATAT
#ifdef HAVE_MKNODAT
extern int (*NEXT_MKNODAT_NOARG)MKNODAT_ARG(int ver, int dir_fd, const char *pathname, mode_t mode, dev_t dev);
#endif /* HAVE_MKNODAT */
#endif /* HAVE_FSTATAT */


extern int (*next_chown)(const char *path, uid_t owner, gid_t group);
extern int (*next_lchown)(const char *path, uid_t owner, gid_t group);
extern int (*next_fchown)(int fd, uid_t owner, gid_t group);
extern int (*next_chmod)(const char *path, mode_t mode);
extern int (*next_fchmod)(int fd, mode_t mode);
#if defined HAVE_LCHMOD
extern int (*next_lchmod)(const char *path, mode_t mode);
#endif
#if defined __APPLE__ && !defined __LP64__
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
extern int (*next_lchown$UNIX2003)(const char *path, uid_t owner, gid_t group);
extern int (*next_chmod$UNIX2003)(const char *path, mode_t mode);
extern int (*next_fchmod$UNIX2003)(int fd, mode_t mode);
#endif
#endif /* if defined __APPLE__ && !defined __LP64__ */
extern int (*next_mkdir)(const char *path, mode_t mode);
extern int (*next_unlink)(const char *pathname);
extern int (*next_rmdir)(const char *pathname);
extern int (*next_remove)(const char *pathname);
extern int (*next_rename)(const char *oldpath, const char *newpath);

#ifdef FAKEROOT_FAKENET
extern pid_t (*next_fork)(void);
extern pid_t (*next_vfork)(void);
extern int (*next_close)(int fd);
extern int (*next_dup2)(int oldfd, int newfd);
#endif /* FAKEROOT_FAKENET */


extern uid_t (*next_getuid)(void);
extern gid_t (*next_getgid)(void);
extern uid_t (*next_geteuid)(void);
extern gid_t (*next_getegid)(void);
extern int (*next_setuid)(uid_t id);
extern int (*next_setgid)(gid_t id);
extern int (*next_seteuid)(uid_t id);
extern int (*next_setegid)(gid_t id);
extern int (*next_setreuid)(SETREUID_ARG ruid, SETREUID_ARG euid);
extern int (*next_setregid)(SETREGID_ARG rgid, SETREGID_ARG egid);
#if defined __APPLE__ && !defined __LP64__
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
extern int (*next_setreuid$UNIX2003)(SETREUID_ARG ruid, SETREUID_ARG euid);
extern int (*next_setregid$UNIX2003)(SETREGID_ARG rgid, SETREGID_ARG egid);
#endif
#endif /* if defined __APPLE__ && !defined __LP64__ */
#ifdef HAVE_GETRESUID
extern int (*next_getresuid)(uid_t *ruid, uid_t *euid, uid_t *suid);
#endif /* HAVE_GETRESUID */
#ifdef HAVE_GETRESGID
extern int (*next_getresgid)(gid_t *rgid, gid_t *egid, gid_t *sgid);
#endif /* HAVE_GETRESGID */
#ifdef HAVE_SETRESUID
extern int (*next_setresuid)(uid_t ruid, uid_t euid, uid_t suid);
#endif /* HAVE_SETRESUID */
#ifdef HAVE_SETRESGID
extern int (*next_setresgid)(gid_t rgid, gid_t egid, gid_t sgid);
#endif /* HAVE_SETRESGID */
#ifdef HAVE_SETFSUID
extern uid_t (*next_setfsuid)(uid_t fsuid);
#endif /* HAVE_SETFSUID */
#ifdef HAVE_SETFSGID
extern gid_t (*next_setfsgid)(gid_t fsgid);
#endif /* HAVE_SETFSGID */
extern int (*next_initgroups)(const char *user, INITGROUPS_SECOND_ARG group);
extern int (*next_setgroups)(SETGROUPS_SIZE_TYPE size, const gid_t *list);
extern int (*next_setpriority)(int which, int who, int prio);
#ifdef HAVE_CAPSET
extern int (*next_capset)(cap_user_header_t hdrp, const cap_user_data_t datap);
#endif /* HAVE_CAPSET */
#ifdef HAVE_LISTXATTR
extern  ssize_t (*next_listxattr)(const char *path, char *list, size_t size);
#endif /* HAVE_LISTXATTR */
#ifdef HAVE_LLISTXATTR
extern  ssize_t (*next_llistxattr)(const char *path, char *list, size_t size);
#endif /* HAVE_LLISTXATTR */
#ifdef HAVE_FLISTXATTR
extern  ssize_t (*next_flistxattr)(int fd, char *list, size_t size);
#endif /* HAVE_FLISTXATTR */
#ifdef HAVE_GETXATTR
extern ssize_t (*next_getxattr)(const char *path, const char *name, void *value, size_t size);
#endif /* HAVE_GETXATTR */
#ifdef HAVE_LGETXATTR
extern ssize_t (*next_lgetxattr)(const char *path, const char *name, void *value, size_t size);
#endif /* HAVE_LGETXATTR */
#ifdef HAVE_FGETXATTR
extern ssize_t (*next_fgetxattr)(int fd, const char *name, void *value, size_t size);
#endif /* HAVE_FGETXATTR */
#ifdef HAVE_SETXATTR
extern ssize_t (*next_setxattr)(const char *path, const char *name, void *value, size_t size, int flags);
#endif /* HAVE_SETXATTR */
#ifdef HAVE_LSETXATTR
extern ssize_t (*next_lsetxattr)(const char *path, const char *name, void *value, size_t size, int flags);
#endif /* HAVE_LSETXATTR */
#ifdef HAVE_FSETXATTR
extern ssize_t (*next_fsetxattr)(int fd, const char *name, void *value, size_t size, int flags);
#endif /* HAVE_FSETXATTR */
#ifdef HAVE_REMOVEXATTR
extern ssize_t (*next_removexattr)(const char *path, const char *name);
#endif /* HAVE_REMOVEXATTR */
#ifdef HAVE_LREMOVEXATTR
extern ssize_t (*next_lremovexattr)(const char *path, const char *name);
#endif /* HAVE_LREMOVEXATTR */
#ifdef HAVE_FREMOVEXATTR
extern ssize_t (*next_fremovexattr)(int fd, const char *name);
#endif /* HAVE_FREMOVEXATTR */

#ifdef HAVE_FSTATAT
#ifdef HAVE_FCHMODAT
extern int (*next_fchmodat)(int dir_fd, const char *path, mode_t mode, int flags);
#endif /* HAVE_FCHMODAT */
#ifdef HAVE_FCHOWNAT
extern int (*next_fchownat)(int dir_fd, const char *path, uid_t owner, gid_t group, int flags);
#endif /* HAVE_FCHOWNAT */
#ifdef HAVE_MKDIRAT
extern int (*next_mkdirat)(int dir_fd, const char *pathname, mode_t mode);
#endif /* HAVE_MKDIRAT */
#ifdef HAVE_OPENAT
extern int (*next_openat)(int dir_fd, const char *pathname, int flags);
#endif /* HAVE_OPENAT */
#ifdef HAVE_RENAMEAT
extern int (*next_renameat)(int olddir_fd, const char *oldpath, int newdir_fd, const char *newpath);
#endif /* HAVE_RENAMEAT */
#ifdef HAVE_UNLINKAT
extern int (*next_unlinkat)(int dir_fd, const char *pathname, int flags);
#endif /* HAVE_UNLINKAT */
#endif /* HAVE_FSTATAT */

#ifdef HAVE_ACL_T
extern acl_t (*next_acl_get_fd)(int fd);
extern acl_t (*next_acl_get_file)(const char *path_p, acl_type_t type);
extern int (*next_acl_set_fd)(int fd, acl_t acl);
extern int (*next_acl_set_file)(const char *path_p, acl_type_t type, acl_t acl);
#endif /* HAVE_ACL_T */

#ifdef HAVE_FTS_READ
extern FTSENT * (*next_fts_read)(FTS *ftsp);
#ifdef __APPLE__
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
extern FTSENT * (*next_fts_read$INODE64)(FTS *ftsp);
#endif
#endif /* ifdef __APPLE__ */
#endif /* HAVE_FTS_READ */
#ifdef HAVE_FTS_CHILDREN
extern FTSENT * (*next_fts_children)(FTS *ftsp, int options);
#ifdef __APPLE__
#if MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_5
extern FTSENT * (*next_fts_children$INODE64)(FTS *ftsp, int options);
#endif
#endif /* ifdef __APPLE__ */
#endif /* HAVE_FTS_CHILDREN */

#ifdef __sun
extern int (*next_sysinfo)(int command, char *buf, long count);
#endif
#endif
