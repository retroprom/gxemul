/*  gxemul: $Id: syscall_netbsd.h,v 1.3 2005-03-05 12:34:03 debug Exp $  */
/* $NetBSD: syscall.h,v 1.122 2002/05/03 00:26:49 eeh Exp $ */

#ifndef SYSCALL_NETBSD_H
#define SYSCALL_NETBSD_H

/*
 * System call numbers.
 *
 * DO NOT EDIT-- this file is automatically generated.
 * created from	NetBSD: syscalls.master,v 1.111 2002/05/03 00:20:56 eeh Exp 
 */

/* syscall: "syscall" ret: "int" args: "int" "..." */
#define	NETBSD_SYS_syscall	0

/* syscall: "exit" ret: "void" args: "int" */
#define	NETBSD_SYS_exit	1

/* syscall: "fork" ret: "int" args: */
#define	NETBSD_SYS_fork	2

/* syscall: "read" ret: "ssize_t" args: "int" "void *" "size_t" */
#define	NETBSD_SYS_read	3

/* syscall: "write" ret: "ssize_t" args: "int" "const void *" "size_t" */
#define	NETBSD_SYS_write	4

/* syscall: "open" ret: "int" args: "const char *" "int" "..." */
#define	NETBSD_SYS_open	5

/* syscall: "close" ret: "int" args: "int" */
#define	NETBSD_SYS_close	6

/* syscall: "wait4" ret: "int" args: "int" "int *" "int" "struct rusage *" */
#define	NETBSD_SYS_wait4	7

#define	NETBSD_SYS_compat_43_ocreat	8

/* syscall: "link" ret: "int" args: "const char *" "const char *" */
#define	NETBSD_SYS_link	9

/* syscall: "unlink" ret: "int" args: "const char *" */
#define	NETBSD_SYS_unlink	10

				/* 11 is obsolete execv */
/* syscall: "chdir" ret: "int" args: "const char *" */
#define	NETBSD_SYS_chdir	12

/* syscall: "fchdir" ret: "int" args: "int" */
#define	NETBSD_SYS_fchdir	13

/* syscall: "mknod" ret: "int" args: "const char *" "mode_t" "dev_t" */
#define	NETBSD_SYS_mknod	14

/* syscall: "chmod" ret: "int" args: "const char *" "mode_t" */
#define	NETBSD_SYS_chmod	15

/* syscall: "chown" ret: "int" args: "const char *" "uid_t" "gid_t" */
#define	NETBSD_SYS_chown	16

/* syscall: "break" ret: "int" args: "char *" */
#define	NETBSD_SYS_break	17

/* syscall: "getfsstat" ret: "int" args: "struct statfs *" "long" "int" */
#define	NETBSD_SYS_getfsstat	18

#define	NETBSD_SYS_compat_43_olseek	19

/* syscall: "getpid" ret: "pid_t" args: */
#define	NETBSD_SYS_getpid	20

/* syscall: "mount" ret: "int" args: "const char *" "const char *" "int" "void *" */
#define	NETBSD_SYS_mount	21

/* syscall: "unmount" ret: "int" args: "const char *" "int" */
#define	NETBSD_SYS_unmount	22

/* syscall: "setuid" ret: "int" args: "uid_t" */
#define	NETBSD_SYS_setuid	23

/* syscall: "getuid" ret: "uid_t" args: */
#define	NETBSD_SYS_getuid	24

/* syscall: "geteuid" ret: "uid_t" args: */
#define	NETBSD_SYS_geteuid	25

/* syscall: "ptrace" ret: "int" args: "int" "pid_t" "caddr_t" "int" */
#define	NETBSD_SYS_ptrace	26

/* syscall: "recvmsg" ret: "ssize_t" args: "int" "struct msghdr *" "int" */
#define	NETBSD_SYS_recvmsg	27

/* syscall: "sendmsg" ret: "ssize_t" args: "int" "const struct msghdr *" "int" */
#define	NETBSD_SYS_sendmsg	28

/* syscall: "recvfrom" ret: "ssize_t" args: "int" "void *" "size_t" "int" "struct sockaddr *" "unsigned int *" */
#define	NETBSD_SYS_recvfrom	29

/* syscall: "accept" ret: "int" args: "int" "struct sockaddr *" "unsigned int *" */
#define	NETBSD_SYS_accept	30

/* syscall: "getpeername" ret: "int" args: "int" "struct sockaddr *" "unsigned int *" */
#define	NETBSD_SYS_getpeername	31

/* syscall: "getsockname" ret: "int" args: "int" "struct sockaddr *" "unsigned int *" */
#define	NETBSD_SYS_getsockname	32

/* syscall: "access" ret: "int" args: "const char *" "int" */
#define	NETBSD_SYS_access	33

/* syscall: "chflags" ret: "int" args: "const char *" "u_long" */
#define	NETBSD_SYS_chflags	34

/* syscall: "fchflags" ret: "int" args: "int" "u_long" */
#define	NETBSD_SYS_fchflags	35

/* syscall: "sync" ret: "void" args: */
#define	NETBSD_SYS_sync	36

/* syscall: "kill" ret: "int" args: "int" "int" */
#define	NETBSD_SYS_kill	37

#define	NETBSD_SYS_compat_43_stat43	38

/* syscall: "getppid" ret: "pid_t" args: */
#define	NETBSD_SYS_getppid	39

#define	NETBSD_SYS_compat_43_lstat43	40

/* syscall: "dup" ret: "int" args: "int" */
#define	NETBSD_SYS_dup	41

/* syscall: "pipe" ret: "int" args: */
#define	NETBSD_SYS_pipe	42

/* syscall: "getegid" ret: "gid_t" args: */
#define	NETBSD_SYS_getegid	43

/* syscall: "profil" ret: "int" args: "caddr_t" "size_t" "u_long" "u_int" */
#define	NETBSD_SYS_profil	44

/* syscall: "ktrace" ret: "int" args: "const char *" "int" "int" "int" */
#define	NETBSD_SYS_ktrace	45

				/* 45 is excluded ktrace */
#define	NETBSD_SYS_compat_13_sigaction13	46

/* syscall: "getgid" ret: "gid_t" args: */
#define	NETBSD_SYS_getgid	47

#define	NETBSD_SYS_compat_13_sigprocmask13	48

/* syscall: "__getlogin" ret: "int" args: "char *" "size_t" */
#define	NETBSD_SYS___getlogin	49

/* syscall: "setlogin" ret: "int" args: "const char *" */
#define	NETBSD_SYS_setlogin	50

/* syscall: "acct" ret: "int" args: "const char *" */
#define	NETBSD_SYS_acct	51

#define	NETBSD_SYS_compat_13_sigpending13	52

#define	NETBSD_SYS_compat_13_sigaltstack13	53

/* syscall: "ioctl" ret: "int" args: "int" "u_long" "..." */
#define	NETBSD_SYS_ioctl	54

#define	NETBSD_SYS_compat_12_oreboot	55

/* syscall: "revoke" ret: "int" args: "const char *" */
#define	NETBSD_SYS_revoke	56

/* syscall: "symlink" ret: "int" args: "const char *" "const char *" */
#define	NETBSD_SYS_symlink	57

/* syscall: "readlink" ret: "int" args: "const char *" "char *" "size_t" */
#define	NETBSD_SYS_readlink	58

/* syscall: "execve" ret: "int" args: "const char *" "char *const *" "char *const *" */
#define	NETBSD_SYS_execve	59

/* syscall: "umask" ret: "mode_t" args: "mode_t" */
#define	NETBSD_SYS_umask	60

/* syscall: "chroot" ret: "int" args: "const char *" */
#define	NETBSD_SYS_chroot	61

#define	NETBSD_SYS_compat_43_fstat43	62

#define	NETBSD_SYS_compat_43_ogetkerninfo	63

#define	NETBSD_SYS_compat_43_ogetpagesize	64

#define	NETBSD_SYS_compat_12_msync	65

/* syscall: "vfork" ret: "int" args: */
#define	NETBSD_SYS_vfork	66

				/* 67 is obsolete vread */
				/* 68 is obsolete vwrite */
/* syscall: "sbrk" ret: "int" args: "intptr_t" */
#define	NETBSD_SYS_sbrk	69

/* syscall: "sstk" ret: "int" args: "int" */
#define	NETBSD_SYS_sstk	70

#define	NETBSD_SYS_compat_43_ommap	71

/* syscall: "vadvise" ret: "int" args: "int" */
#define	NETBSD_SYS_vadvise	72

/* syscall: "munmap" ret: "int" args: "void *" "size_t" */
#define	NETBSD_SYS_munmap	73

/* syscall: "mprotect" ret: "int" args: "void *" "size_t" "int" */
#define	NETBSD_SYS_mprotect	74

/* syscall: "madvise" ret: "int" args: "void *" "size_t" "int" */
#define	NETBSD_SYS_madvise	75

				/* 76 is obsolete vhangup */
				/* 77 is obsolete vlimit */
/* syscall: "mincore" ret: "int" args: "void *" "size_t" "char *" */
#define	NETBSD_SYS_mincore	78

/* syscall: "getgroups" ret: "int" args: "int" "gid_t *" */
#define	NETBSD_SYS_getgroups	79

/* syscall: "setgroups" ret: "int" args: "int" "const gid_t *" */
#define	NETBSD_SYS_setgroups	80

/* syscall: "getpgrp" ret: "int" args: */
#define	NETBSD_SYS_getpgrp	81

/* syscall: "setpgid" ret: "int" args: "int" "int" */
#define	NETBSD_SYS_setpgid	82

/* syscall: "setitimer" ret: "int" args: "int" "const struct itimerval *" "struct itimerval *" */
#define	NETBSD_SYS_setitimer	83

#define	NETBSD_SYS_compat_43_owait	84

#define	NETBSD_SYS_compat_12_oswapon	85

/* syscall: "getitimer" ret: "int" args: "int" "struct itimerval *" */
#define	NETBSD_SYS_getitimer	86

#define	NETBSD_SYS_compat_43_ogethostname	87

#define	NETBSD_SYS_compat_43_osethostname	88

#define	NETBSD_SYS_compat_43_ogetdtablesize	89

/* syscall: "dup2" ret: "int" args: "int" "int" */
#define	NETBSD_SYS_dup2	90

/* syscall: "fcntl" ret: "int" args: "int" "int" "..." */
#define	NETBSD_SYS_fcntl	92

/* syscall: "select" ret: "int" args: "int" "fd_set *" "fd_set *" "fd_set *" "struct timeval *" */
#define	NETBSD_SYS_select	93

/* syscall: "fsync" ret: "int" args: "int" */
#define	NETBSD_SYS_fsync	95

/* syscall: "setpriority" ret: "int" args: "int" "int" "int" */
#define	NETBSD_SYS_setpriority	96

/* syscall: "socket" ret: "int" args: "int" "int" "int" */
#define	NETBSD_SYS_socket	97

/* syscall: "connect" ret: "int" args: "int" "const struct sockaddr *" "unsigned int" */
#define	NETBSD_SYS_connect	98

#define	NETBSD_SYS_compat_43_oaccept	99

/* syscall: "getpriority" ret: "int" args: "int" "int" */
#define	NETBSD_SYS_getpriority	100

#define	NETBSD_SYS_compat_43_osend	101

#define	NETBSD_SYS_compat_43_orecv	102

#define	NETBSD_SYS_compat_13_sigreturn13	103

/* syscall: "bind" ret: "int" args: "int" "const struct sockaddr *" "unsigned int" */
#define	NETBSD_SYS_bind	104

/* syscall: "setsockopt" ret: "int" args: "int" "int" "int" "const void *" "unsigned int" */
#define	NETBSD_SYS_setsockopt	105

/* syscall: "listen" ret: "int" args: "int" "int" */
#define	NETBSD_SYS_listen	106

				/* 107 is obsolete vtimes */
#define	NETBSD_SYS_compat_43_osigvec	108

#define	NETBSD_SYS_compat_43_osigblock	109

#define	NETBSD_SYS_compat_43_osigsetmask	110

#define	NETBSD_SYS_compat_13_sigsuspend13	111

#define	NETBSD_SYS_compat_43_osigstack	112

#define	NETBSD_SYS_compat_43_orecvmsg	113

#define	NETBSD_SYS_compat_43_osendmsg	114

				/* 115 is obsolete vtrace */
/* syscall: "gettimeofday" ret: "int" args: "struct timeval *" "struct timezone *" */
#define	NETBSD_SYS_gettimeofday	116

/* syscall: "getrusage" ret: "int" args: "int" "struct rusage *" */
#define	NETBSD_SYS_getrusage	117

/* syscall: "getsockopt" ret: "int" args: "int" "int" "int" "void *" "unsigned int *" */
#define	NETBSD_SYS_getsockopt	118

				/* 119 is obsolete resuba */
/* syscall: "readv" ret: "ssize_t" args: "int" "const struct iovec *" "int" */
#define	NETBSD_SYS_readv	120

/* syscall: "writev" ret: "ssize_t" args: "int" "const struct iovec *" "int" */
#define	NETBSD_SYS_writev	121

/* syscall: "settimeofday" ret: "int" args: "const struct timeval *" "const struct timezone *" */
#define	NETBSD_SYS_settimeofday	122

/* syscall: "fchown" ret: "int" args: "int" "uid_t" "gid_t" */
#define	NETBSD_SYS_fchown	123

/* syscall: "fchmod" ret: "int" args: "int" "mode_t" */
#define	NETBSD_SYS_fchmod	124

#define	NETBSD_SYS_compat_43_orecvfrom	125

/* syscall: "setreuid" ret: "int" args: "uid_t" "uid_t" */
#define	NETBSD_SYS_setreuid	126

/* syscall: "setregid" ret: "int" args: "gid_t" "gid_t" */
#define	NETBSD_SYS_setregid	127

/* syscall: "rename" ret: "int" args: "const char *" "const char *" */
#define	NETBSD_SYS_rename	128

#define	NETBSD_SYS_compat_43_otruncate	129

#define	NETBSD_SYS_compat_43_oftruncate	130

/* syscall: "flock" ret: "int" args: "int" "int" */
#define	NETBSD_SYS_flock	131

/* syscall: "mkfifo" ret: "int" args: "const char *" "mode_t" */
#define	NETBSD_SYS_mkfifo	132

/* syscall: "sendto" ret: "ssize_t" args: "int" "const void *" "size_t" "int" "const struct sockaddr *" "unsigned int" */
#define	NETBSD_SYS_sendto	133

/* syscall: "shutdown" ret: "int" args: "int" "int" */
#define	NETBSD_SYS_shutdown	134

/* syscall: "socketpair" ret: "int" args: "int" "int" "int" "int *" */
#define	NETBSD_SYS_socketpair	135

/* syscall: "mkdir" ret: "int" args: "const char *" "mode_t" */
#define	NETBSD_SYS_mkdir	136

/* syscall: "rmdir" ret: "int" args: "const char *" */
#define	NETBSD_SYS_rmdir	137

/* syscall: "utimes" ret: "int" args: "const char *" "const struct timeval *" */
#define	NETBSD_SYS_utimes	138

				/* 139 is obsolete 4.2 sigreturn */
/* syscall: "adjtime" ret: "int" args: "const struct timeval *" "struct timeval *" */
#define	NETBSD_SYS_adjtime	140

#define	NETBSD_SYS_compat_43_ogetpeername	141

#define	NETBSD_SYS_compat_43_ogethostid	142

#define	NETBSD_SYS_compat_43_osethostid	143

#define	NETBSD_SYS_compat_43_ogetrlimit	144

#define	NETBSD_SYS_compat_43_osetrlimit	145

#define	NETBSD_SYS_compat_43_okillpg	146

/* syscall: "setsid" ret: "int" args: */
#define	NETBSD_SYS_setsid	147

/* syscall: "quotactl" ret: "int" args: "const char *" "int" "int" "caddr_t" */
#define	NETBSD_SYS_quotactl	148

#define	NETBSD_SYS_compat_43_oquota	149

#define	NETBSD_SYS_compat_43_ogetsockname	150

/* syscall: "nfssvc" ret: "int" args: "int" "void *" */
#define	NETBSD_SYS_nfssvc	155

				/* 155 is excluded nfssvc */
#define	NETBSD_SYS_compat_43_ogetdirentries	156

/* syscall: "statfs" ret: "int" args: "const char *" "struct statfs *" */
#define	NETBSD_SYS_statfs	157

/* syscall: "fstatfs" ret: "int" args: "int" "struct statfs *" */
#define	NETBSD_SYS_fstatfs	158

/* syscall: "getfh" ret: "int" args: "const char *" "fhandle_t *" */
#define	NETBSD_SYS_getfh	161

#define	NETBSD_SYS_compat_09_ogetdomainname	162

#define	NETBSD_SYS_compat_09_osetdomainname	163

#define	NETBSD_SYS_compat_09_ouname	164

/* syscall: "sysarch" ret: "int" args: "int" "void *" */
#define	NETBSD_SYS_sysarch	165

#define	NETBSD_SYS_compat_10_osemsys	169

				/* 169 is excluded 1.0 semsys */
#define	NETBSD_SYS_compat_10_omsgsys	170

				/* 170 is excluded 1.0 msgsys */
#define	NETBSD_SYS_compat_10_oshmsys	171

				/* 171 is excluded 1.0 shmsys */
/* syscall: "pread" ret: "ssize_t" args: "int" "void *" "size_t" "int" "off_t" */
#define	NETBSD_SYS_pread	173

/* syscall: "pwrite" ret: "ssize_t" args: "int" "const void *" "size_t" "int" "off_t" */
#define	NETBSD_SYS_pwrite	174

/* syscall: "ntp_gettime" ret: "int" args: "struct ntptimeval *" */
#define	NETBSD_SYS_ntp_gettime	175

/* syscall: "ntp_adjtime" ret: "int" args: "struct timex *" */
#define	NETBSD_SYS_ntp_adjtime	176

				/* 176 is excluded ntp_adjtime */
/* syscall: "setgid" ret: "int" args: "gid_t" */
#define	NETBSD_SYS_setgid	181

/* syscall: "setegid" ret: "int" args: "gid_t" */
#define	NETBSD_SYS_setegid	182

/* syscall: "seteuid" ret: "int" args: "uid_t" */
#define	NETBSD_SYS_seteuid	183

/* syscall: "lfs_bmapv" ret: "int" args: "fsid_t *" "struct block_info *" "int" */
#define	NETBSD_SYS_lfs_bmapv	184

/* syscall: "lfs_markv" ret: "int" args: "fsid_t *" "struct block_info *" "int" */
#define	NETBSD_SYS_lfs_markv	185

/* syscall: "lfs_segclean" ret: "int" args: "fsid_t *" "u_long" */
#define	NETBSD_SYS_lfs_segclean	186

/* syscall: "lfs_segwait" ret: "int" args: "fsid_t *" "struct timeval *" */
#define	NETBSD_SYS_lfs_segwait	187

				/* 184 is excluded lfs_bmapv */
				/* 185 is excluded lfs_markv */
				/* 186 is excluded lfs_segclean */
				/* 187 is excluded lfs_segwait */
#define	NETBSD_SYS_compat_12_stat12	188

#define	NETBSD_SYS_compat_12_fstat12	189

#define	NETBSD_SYS_compat_12_lstat12	190

/* syscall: "pathconf" ret: "long" args: "const char *" "int" */
#define	NETBSD_SYS_pathconf	191

/* syscall: "fpathconf" ret: "long" args: "int" "int" */
#define	NETBSD_SYS_fpathconf	192

/* syscall: "getrlimit" ret: "int" args: "int" "struct rlimit *" */
#define	NETBSD_SYS_getrlimit	194

/* syscall: "setrlimit" ret: "int" args: "int" "const struct rlimit *" */
#define	NETBSD_SYS_setrlimit	195

#define	NETBSD_SYS_compat_12_getdirentries	196

/* syscall: "mmap" ret: "void *" args: "void *" "size_t" "int" "int" "int" "long" "off_t" */
#define	NETBSD_SYS_mmap	197

/* syscall: "__syscall" ret: "quad_t" args: "quad_t" "..." */
#define	NETBSD_SYS___syscall	198

/* syscall: "lseek" ret: "off_t" args: "int" "int" "off_t" "int" */
#define	NETBSD_SYS_lseek	199

/* syscall: "truncate" ret: "int" args: "const char *" "int" "off_t" */
#define	NETBSD_SYS_truncate	200

/* syscall: "ftruncate" ret: "int" args: "int" "int" "off_t" */
#define	NETBSD_SYS_ftruncate	201

/* syscall: "__sysctl" ret: "int" args: "int *" "u_int" "void *" "size_t *" "void *" "size_t" */
#define	NETBSD_SYS___sysctl	202

/* syscall: "mlock" ret: "int" args: "const void *" "size_t" */
#define	NETBSD_SYS_mlock	203

/* syscall: "munlock" ret: "int" args: "const void *" "size_t" */
#define	NETBSD_SYS_munlock	204

/* syscall: "undelete" ret: "int" args: "const char *" */
#define	NETBSD_SYS_undelete	205

/* syscall: "futimes" ret: "int" args: "int" "const struct timeval *" */
#define	NETBSD_SYS_futimes	206

/* syscall: "getpgid" ret: "pid_t" args: "pid_t" */
#define	NETBSD_SYS_getpgid	207

/* syscall: "reboot" ret: "int" args: "int" "char *" */
#define	NETBSD_SYS_reboot	208

/* syscall: "poll" ret: "int" args: "struct pollfd *" "u_int" "int" */
#define	NETBSD_SYS_poll	209

				/* 210 is excluded lkmnosys */
				/* 211 is excluded lkmnosys */
				/* 212 is excluded lkmnosys */
				/* 213 is excluded lkmnosys */
				/* 214 is excluded lkmnosys */
				/* 215 is excluded lkmnosys */
				/* 216 is excluded lkmnosys */
				/* 217 is excluded lkmnosys */
				/* 218 is excluded lkmnosys */
				/* 219 is excluded lkmnosys */
#define	NETBSD_SYS_compat_14___semctl	220

/* syscall: "semget" ret: "int" args: "key_t" "int" "int" */
#define	NETBSD_SYS_semget	221

/* syscall: "semop" ret: "int" args: "int" "struct sembuf *" "size_t" */
#define	NETBSD_SYS_semop	222

/* syscall: "semconfig" ret: "int" args: "int" */
#define	NETBSD_SYS_semconfig	223

				/* 220 is excluded compat_14_semctl */
				/* 221 is excluded semget */
				/* 222 is excluded semop */
				/* 223 is excluded semconfig */
#define	NETBSD_SYS_compat_14_msgctl	224

/* syscall: "msgget" ret: "int" args: "key_t" "int" */
#define	NETBSD_SYS_msgget	225

/* syscall: "msgsnd" ret: "int" args: "int" "const void *" "size_t" "int" */
#define	NETBSD_SYS_msgsnd	226

/* syscall: "msgrcv" ret: "ssize_t" args: "int" "void *" "size_t" "long" "int" */
#define	NETBSD_SYS_msgrcv	227

				/* 224 is excluded compat_14_msgctl */
				/* 225 is excluded msgget */
				/* 226 is excluded msgsnd */
				/* 227 is excluded msgrcv */
/* syscall: "shmat" ret: "void *" args: "int" "const void *" "int" */
#define	NETBSD_SYS_shmat	228

#define	NETBSD_SYS_compat_14_shmctl	229

/* syscall: "shmdt" ret: "int" args: "const void *" */
#define	NETBSD_SYS_shmdt	230

/* syscall: "shmget" ret: "int" args: "key_t" "size_t" "int" */
#define	NETBSD_SYS_shmget	231

				/* 228 is excluded shmat */
				/* 229 is excluded compat_14_shmctl */
				/* 230 is excluded shmdt */
				/* 231 is excluded shmget */
/* syscall: "clock_gettime" ret: "int" args: "clockid_t" "struct timespec *" */
#define	NETBSD_SYS_clock_gettime	232

/* syscall: "clock_settime" ret: "int" args: "clockid_t" "const struct timespec *" */
#define	NETBSD_SYS_clock_settime	233

/* syscall: "clock_getres" ret: "int" args: "clockid_t" "struct timespec *" */
#define	NETBSD_SYS_clock_getres	234

/* syscall: "nanosleep" ret: "int" args: "const struct timespec *" "struct timespec *" */
#define	NETBSD_SYS_nanosleep	240

/* syscall: "fdatasync" ret: "int" args: "int" */
#define	NETBSD_SYS_fdatasync	241

/* syscall: "mlockall" ret: "int" args: "int" */
#define	NETBSD_SYS_mlockall	242

/* syscall: "munlockall" ret: "int" args: */
#define	NETBSD_SYS_munlockall	243

/* syscall: "__posix_rename" ret: "int" args: "const char *" "const char *" */
#define	NETBSD_SYS___posix_rename	270

/* syscall: "swapctl" ret: "int" args: "int" "const void *" "int" */
#define	NETBSD_SYS_swapctl	271

/* syscall: "getdents" ret: "int" args: "int" "char *" "size_t" */
#define	NETBSD_SYS_getdents	272

/* syscall: "minherit" ret: "int" args: "void *" "size_t" "int" */
#define	NETBSD_SYS_minherit	273

/* syscall: "lchmod" ret: "int" args: "const char *" "mode_t" */
#define	NETBSD_SYS_lchmod	274

/* syscall: "lchown" ret: "int" args: "const char *" "uid_t" "gid_t" */
#define	NETBSD_SYS_lchown	275

/* syscall: "lutimes" ret: "int" args: "const char *" "const struct timeval *" */
#define	NETBSD_SYS_lutimes	276

/* syscall: "__msync13" ret: "int" args: "void *" "size_t" "int" */
#define	NETBSD_SYS___msync13	277

/* syscall: "__stat13" ret: "int" args: "const char *" "struct stat *" */
#define	NETBSD_SYS___stat13	278

/* syscall: "__fstat13" ret: "int" args: "int" "struct stat *" */
#define	NETBSD_SYS___fstat13	279

/* syscall: "__lstat13" ret: "int" args: "const char *" "struct stat *" */
#define	NETBSD_SYS___lstat13	280

/* syscall: "__sigaltstack14" ret: "int" args: "const struct sigaltstack *" "struct sigaltstack *" */
#define	NETBSD_SYS___sigaltstack14	281

/* syscall: "__vfork14" ret: "int" args: */
#define	NETBSD_SYS___vfork14	282

/* syscall: "__posix_chown" ret: "int" args: "const char *" "uid_t" "gid_t" */
#define	NETBSD_SYS___posix_chown	283

/* syscall: "__posix_fchown" ret: "int" args: "int" "uid_t" "gid_t" */
#define	NETBSD_SYS___posix_fchown	284

/* syscall: "__posix_lchown" ret: "int" args: "const char *" "uid_t" "gid_t" */
#define	NETBSD_SYS___posix_lchown	285

/* syscall: "getsid" ret: "pid_t" args: "pid_t" */
#define	NETBSD_SYS_getsid	286

/* syscall: "__clone" ret: "pid_t" args: "int" "void *" */
#define	NETBSD_SYS___clone	287

/* syscall: "fktrace" ret: "int" args: "const int" "int" "int" "int" */
#define	NETBSD_SYS_fktrace	288

				/* 288 is excluded ktrace */
/* syscall: "preadv" ret: "ssize_t" args: "int" "const struct iovec *" "int" "int" "off_t" */
#define	NETBSD_SYS_preadv	289

/* syscall: "pwritev" ret: "ssize_t" args: "int" "const struct iovec *" "int" "int" "off_t" */
#define	NETBSD_SYS_pwritev	290

/* syscall: "__sigaction14" ret: "int" args: "int" "const struct sigaction *" "struct sigaction *" */
#define	NETBSD_SYS___sigaction14	291

/* syscall: "__sigpending14" ret: "int" args: "sigset_t *" */
#define	NETBSD_SYS___sigpending14	292

/* syscall: "__sigprocmask14" ret: "int" args: "int" "const sigset_t *" "sigset_t *" */
#define	NETBSD_SYS___sigprocmask14	293

/* syscall: "__sigsuspend14" ret: "int" args: "const sigset_t *" */
#define	NETBSD_SYS___sigsuspend14	294

/* syscall: "__sigreturn14" ret: "int" args: "struct sigcontext *" */
#define	NETBSD_SYS___sigreturn14	295

/* syscall: "__getcwd" ret: "int" args: "char *" "size_t" */
#define	NETBSD_SYS___getcwd	296

/* syscall: "fchroot" ret: "int" args: "int" */
#define	NETBSD_SYS_fchroot	297

/* syscall: "fhopen" ret: "int" args: "const fhandle_t *" "int" */
#define	NETBSD_SYS_fhopen	298

/* syscall: "fhstat" ret: "int" args: "const fhandle_t *" "struct stat *" */
#define	NETBSD_SYS_fhstat	299

/* syscall: "fhstatfs" ret: "int" args: "const fhandle_t *" "struct statfs *" */
#define	NETBSD_SYS_fhstatfs	300

/* syscall: "____semctl13" ret: "int" args: "int" "int" "int" "..." */
#define	NETBSD_SYS_____semctl13	301

				/* 301 is excluded ____semctl13 */
/* syscall: "__msgctl13" ret: "int" args: "int" "int" "struct msqid_ds *" */
#define	NETBSD_SYS___msgctl13	302

				/* 302 is excluded __msgctl13 */
/* syscall: "__shmctl13" ret: "int" args: "int" "int" "struct shmid_ds *" */
#define	NETBSD_SYS___shmctl13	303

				/* 303 is excluded __shmctl13 */
/* syscall: "lchflags" ret: "int" args: "const char *" "u_long" */
#define	NETBSD_SYS_lchflags	304

/* syscall: "issetugid" ret: "int" args: */
#define	NETBSD_SYS_issetugid	305

/* syscall: "utrace" ret: "int" args: "const char *" "void *" "size_t" */
#define	NETBSD_SYS_utrace	306

#define	NETBSD_SYS_MAXSYSCALL	340
#define	NETBSD_SYS_NSYSENT	512

#endif /*  SYSCALL_NETBSD_H  */
