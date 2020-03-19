/******************************************************************************
 * Copyright © 2014-2018 The SuperNET Developers.                             *
 *                                                                            *
 * See the AUTHORS, DEVELOPER-AGREEMENT and LICENSE files at                  *
 * the top-level directory of this distribution for the individual copyright  *
 * holder information and the developer policies on copyright and licensing.  *
 *                                                                            *
 * Unless otherwise agreed in a custom licensing agreement, no part of the    *
 * SuperNET software, including this file may be copied, modified, propagated *
 * or distributed except according to the terms contained in the LICENSE file *
 *                                                                            *
 * Removal or modification of this copyright notice is prohibited.            *
 *                                                                            *
 ******************************************************************************/

 /**
 * - we need to include WinSock2.h header to correctly use windows structure
 * as the application is still using 32bit structure from mingw so, we need to
 * add the include based on checking
 *
 * @author - fadedreamz@gmail.com
 * @remarks - #if (defined(_M_X64) || defined(__amd64__)) && defined(WIN32)
 *     is equivalent to #if defined(_M_X64) as _M_X64 is defined for MSVC only
 * 
 * @remarks - we need this because in win64 we are using windows provided pollfd structure
 *			  not from the mingw header, so we need to include the windows header
 *			  if we are compiling in windows 64bit
 */
#if defined(_M_X64)
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#endif

#include "OS_portable.h"


#ifdef __PNACL
int32_t OS_nonportable_syncmap(struct OS_mappedptr *mp,long len)
{
    printf("no way to sync mapped mem in pnacl\n");
    return(-1);
}

void *OS_nonportable_tmpalloc(char *dirname,char *name,struct OS_memspace *mem,long origsize)
{
    printf("no way to do tmpallocs in pnacl\n");
    return(0);
}

#elif _WIN32
#include <io.h>
#include <share.h>
#include <errno.h>
#include <string.h>
//#include <windows.h>
#include <inttypes.h>
//#include <winsock2.h>
//#include <ws2tcpip.h>
#include <fcntl.h> /*  _O_BINARY */
#include <stdlib.h>
#include <wincrypt.h>
#include <stdio.h>
#include <process.h>
#include <tlhelp32.h>
#include <time.h>

/*#include <intrin.h>
static uint32_t __inline __builtin_clzll(uint64_t x) {
    unsigned long r = 0;
    _BitScanReverse64(&r, x);
    return (63-r);
}*/

void usleep(int32_t micros)
{
    if ( micros < 1000 )
        Sleep(1);
    else Sleep(micros / 1000);
}

int
mkstemp (template)
char *template;
{
    static const char letters[]
    = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    static uint64_t value;
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
#endif
    char *XXXXXX;
    size_t len;
    int count;
    
    len = strlen (template);
    
    if ((int) len < 6
        || strncmp (&template[len - 6], "XXXXXX", 6))
    {
        return -1;
    }
    
    XXXXXX = &template[len - 6];
    
#ifdef HAVE_GETTIMEOFDAY
    /* Get some more or less random data.  */
    gettimeofday (&tv, NULL);
    value += ((uint64_t) tv.tv_usec << 16) ^ tv.tv_sec ^ getpid ();
#else
    value += getpid ();
#endif
    
    for (count = 0; count < TMP_MAX; ++count)
    {
        uint64_t v = value;
        int fd;
        
        /* Fill in the random bits.  */
        XXXXXX[0] = letters[v % 62];
        v /= 62;
        XXXXXX[1] = letters[v % 62];
        v /= 62;
        XXXXXX[2] = letters[v % 62];
        v /= 62;
        XXXXXX[3] = letters[v % 62];
        v /= 62;
        XXXXXX[4] = letters[v % 62];
        v /= 62;
        XXXXXX[5] = letters[v % 62];
        
        fd = open (template, O_RDWR|O_CREAT|O_EXCL|O_BINARY, 0600);
        if (fd >= 0)
        /* The file does not exist.  */
            return fd;
        
        /* This is a random value.  It is only necessary that the next
         TMP_MAX values generated by adding 7777 to VALUE are different
         with (module 2^32).  */
        value += 7777;
    }
    
    /* We return the null string if we can't find a unique file name.  */
    template[0] = '\0';
    return -1;
}

#include "../OSlibs/win/mman.h"

#ifndef FILE_MAP_EXECUTE
#define FILE_MAP_EXECUTE    0x0020
#endif /* FILE_MAP_EXECUTE */

static int __map_mman_error(const DWORD err, const int deferr)
{
    if (err == 0)
        return 0;
    //TODO: implement
    return err;
}

static DWORD __map_mmap_prot_page(const int prot)
{
    DWORD protect = 0;
    
    if (prot == PROT_NONE)
        return protect;
        
    if ((prot & PROT_EXEC) != 0)
    {
        protect = ((prot & PROT_WRITE) != 0) ? 
                    PAGE_EXECUTE_READWRITE : PAGE_EXECUTE_READ;
    }
    else
    {
        protect = ((prot & PROT_WRITE) != 0) ?
                    PAGE_READWRITE : PAGE_READONLY;
    }
    
    return protect;
}

static DWORD __map_mmap_prot_file(const int prot)
{
    DWORD desiredAccess = 0;
    
    if (prot == PROT_NONE)
        return desiredAccess;
        
    if ((prot & PROT_READ) != 0)
        desiredAccess |= FILE_MAP_READ;
    if ((prot & PROT_WRITE) != 0)
        desiredAccess |= FILE_MAP_WRITE;
    if ((prot & PROT_EXEC) != 0)
        desiredAccess |= FILE_MAP_EXECUTE;
    
    return desiredAccess;
}

void* mmap(void *addr, size_t len, int prot, int flags, int fildes, off_t off)
{
    HANDLE fm, h;
    
    void * map = MAP_FAILED;
    
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4293)
#endif

    const DWORD dwFileOffsetLow = (sizeof(off_t) <= sizeof(DWORD)) ? 
                    (DWORD)off : (DWORD)(off & 0xFFFFFFFFL);
    const DWORD dwFileOffsetHigh = (sizeof(off_t) <= sizeof(DWORD)) ?
                    (DWORD)0 : (DWORD)((off >> 32) & 0xFFFFFFFFL);
    const DWORD protect = __map_mmap_prot_page(prot);
    const DWORD desiredAccess = __map_mmap_prot_file(prot);

    const off_t maxSize = off + (off_t)len;

    const DWORD dwMaxSizeLow = (sizeof(off_t) <= sizeof(DWORD)) ? 
                    (DWORD)maxSize : (DWORD)(maxSize & 0xFFFFFFFFL);
    const DWORD dwMaxSizeHigh = (sizeof(off_t) <= sizeof(DWORD)) ?
                    (DWORD)0 : (DWORD)((maxSize >> 32) & 0xFFFFFFFFL);

#ifdef _MSC_VER
#pragma warning(pop)
#endif

    errno = 0;
    
    if (len == 0 
        /* Unsupported flag combinations */
        || (flags & MAP_FIXED) != 0
        /* Usupported protection combinations */
        || prot == PROT_EXEC)
    {
        errno = EINVAL;
        return MAP_FAILED;
    }
    
    h = ((flags & MAP_ANONYMOUS) == 0) ? 
                    (HANDLE)_get_osfhandle(fildes) : INVALID_HANDLE_VALUE;

    if ((flags & MAP_ANONYMOUS) == 0 && h == INVALID_HANDLE_VALUE)
    {
        errno = EBADF;
        return MAP_FAILED;
    }

    fm = CreateFileMapping(h, NULL, protect, dwMaxSizeHigh, dwMaxSizeLow, NULL);

    if (fm == NULL)
    {
        errno = __map_mman_error(GetLastError(), EPERM);
        return MAP_FAILED;
    }
  
    map = MapViewOfFile(fm, desiredAccess, dwFileOffsetHigh, dwFileOffsetLow, len);

    CloseHandle(fm);
  
    if (map == NULL)
    {
        errno = __map_mman_error(GetLastError(), EPERM);
        return MAP_FAILED;
    }

    return map;
}

int munmap(void *addr, size_t len)
{
    if (UnmapViewOfFile(addr))
        return 0;
        
    errno =  __map_mman_error(GetLastError(), EPERM);
    
    return -1;
}

int _mprotect(void *addr, size_t len, int prot)
{
    DWORD newProtect = __map_mmap_prot_page(prot);
    DWORD oldProtect = 0;
    
    if (VirtualProtect(addr, len, newProtect, &oldProtect))
        return 0;
    
    errno =  __map_mman_error(GetLastError(), EPERM);
    
    return -1;
}

/*int msync(void *addr, size_t len, int flags)
{
    if (FlushViewOfFile(addr, len))
        return 0;
    
    errno =  __map_mman_error(GetLastError(), EPERM);
    
    return -1;
}*/

int mlock(const void *addr, size_t len)
{
    if (VirtualLock((LPVOID)addr, len))
        return 0;
        
    errno =  __map_mman_error(GetLastError(), EPERM);
    
    return -1;
}

int munlock(const void *addr, size_t len)
{
    if (VirtualUnlock((LPVOID)addr, len))
        return 0;
        
    errno =  __map_mman_error(GetLastError(), EPERM);
    
    return -1;
}

#undef socket
#undef connect
#undef accept
#undef shutdown

#include <string.h>
#include <errno.h>
#include <assert.h>

int win32_poll(struct pollfd *fds, unsigned int nfds, int timo)
{
    struct timeval timeout, *toptr;
    fd_set ifds, ofds, efds, *ip, *op;
    unsigned int i, rc;

    /* Set up the file-descriptor sets in ifds, ofds and efds. */
    FD_ZERO(&ifds);
    FD_ZERO(&ofds);
    FD_ZERO(&efds);
    for (i = 0, op = ip = 0; i < nfds; ++i) {
        fds[i].revents = 0;
        if(fds[i].events & (POLLIN|POLLPRI)) {
            ip = &ifds;
            FD_SET(fds[i].fd, ip);
        }
        if(fds[i].events & POLLOUT) {
            op = &ofds;
            FD_SET(fds[i].fd, op);
        }
        FD_SET(fds[i].fd, &efds);
    } 

    /* Set up the timeval structure for the timeout parameter */
    if(timo < 0) {
        toptr = 0;
    } else {
        toptr = &timeout;
        timeout.tv_sec = timo / 1000;
        timeout.tv_usec = (timo - timeout.tv_sec * 1000) * 1000;
    }

#ifdef DEBUG_POLL
    printf("Entering select() sec=%ld usec=%ld ip=%lx op=%lx\n",
            (long)timeout.tv_sec, (long)timeout.tv_usec, (long)ip, (long)op);
#endif
    rc = select(0, ip, op, &efds, toptr);
#ifdef DEBUG_POLL
    printf("Exiting select rc=%d\n", rc);
#endif

    if(rc <= 0)
        return rc;

    if(rc > 0) {
        for ( i = 0; i < nfds; ++i) {
            int fd = fds[i].fd;
            if(fds[i].events & (POLLIN|POLLPRI) && FD_ISSET(fd, &ifds))
                fds[i].revents |= POLLIN;
            if(fds[i].events & POLLOUT && FD_ISSET(fd, &ofds))
                fds[i].revents |= POLLOUT;
            if(FD_ISSET(fd, &efds))
                /* Some error was detected ... should be some way to know. */
                fds[i].revents |= POLLHUP;
#ifdef DEBUG_POLL
            printf("%d %d %d revent = %x\n", 
                    FD_ISSET(fd, &ifds), FD_ISSET(fd, &ofds), FD_ISSET(fd, &efds), 
                    fds[i].revents
                  );
#endif
        }
    }
    return rc;
}
static void
set_connect_errno(int winsock_err)
{
    switch(winsock_err) {
    case WSAEINVAL:
    case WSAEALREADY:
    case WSAEWOULDBLOCK:
        errno = EINPROGRESS;
        break;
    default:
        errno = winsock_err;
        break;
    }
}

static void
set_socket_errno(int winsock_err)
{
    switch(winsock_err) {
    case WSAEWOULDBLOCK:
        errno = EAGAIN;
        break;
    default:
        errno = winsock_err;
        break;
    }
}
/*
 * A wrapper around the socket() function. The purpose of this wrapper
 * is to ensure that the global errno symbol is set if an error occurs,
 * even if we are using winsock.
 */
SOCKET
win32_socket(int domain, int type, int protocol)
{
    SOCKET fd = socket(domain, type, protocol);
    if(fd == INVALID_SOCKET) {
        set_socket_errno(WSAGetLastError());
    }
    return fd;
}
/*
 * A wrapper around the connect() function. The purpose of this wrapper
 * is to ensure that the global errno symbol is set if an error occurs,
 * even if we are using winsock.
 */
int
win32_connect(SOCKET fd, struct sockaddr *addr, socklen_t addr_len)
{
    int rc = connect(fd, addr, addr_len);
    assert(rc == 0 || rc == SOCKET_ERROR);
    if(rc == SOCKET_ERROR) {
        set_connect_errno(WSAGetLastError());
    }
    return rc;
}

/*
 * A wrapper around the accept() function. The purpose of this wrapper
 * is to ensure that the global errno symbol is set if an error occurs,
 * even if we are using winsock.
 */
SOCKET
win32_accept(SOCKET fd, struct sockaddr *addr, socklen_t *addr_len)
{
    SOCKET newfd = accept(fd, addr, addr_len);
    if(newfd == INVALID_SOCKET) {
        set_socket_errno(WSAGetLastError());
        newfd = -1;
    }
    return newfd;
}

/*
 * A wrapper around the shutdown() function. The purpose of this wrapper
 * is to ensure that the global errno symbol is set if an error occurs,
 * even if we are using winsock.
 */
int
win32_shutdown(SOCKET fd, int mode)
{
    int rc = shutdown(fd, mode);
    assert(rc == 0 || rc == SOCKET_ERROR);
    if(rc == SOCKET_ERROR) {
        set_socket_errno(WSAGetLastError());
    }
    return rc;
}

int win32_close_socket(SOCKET fd)
{
    int rc = closesocket(fd);
    if(rc == SOCKET_ERROR) {
        set_socket_errno(WSAGetLastError());
    }
    return rc;
}

ssize_t win32_write_socket(SOCKET fd, void *buf, int n)
{
    int rc = send(fd, buf, n, 0);
    if(rc == SOCKET_ERROR) {
        set_socket_errno(WSAGetLastError());
    }
    return rc;
}

ssize_t win32_read_socket(SOCKET fd, void *buf, int n)
{
    int rc = recv(fd, buf, n, 0);
    if(rc == SOCKET_ERROR) {
        set_socket_errno(WSAGetLastError());
    }
    return rc;
}


char * win32_strtok_r(char *s, const char *delim, char **lasts)
{
    register char *spanp;
    register int c, sc;
    char *tok;


    if (s == NULL && (s = *lasts) == NULL)
        return (NULL);

    /*
     * Skip (span) leading delimiters (s += strspn(s, delim), sort of).
     */
cont:
    c = *s++;
    for (spanp = (char *)delim; (sc = *spanp++) != 0;) {
        if (c == sc)
            goto cont;
    }

    if (c == 0) {		/* no non-delimiter characters */
        *lasts = NULL;
        return (NULL);
    }
    tok = s - 1;

    /*
     * Scan token (scan for delimiters: s += strcspn(s, delim), sort of).
     * Note that delim must have one NUL; we stop if we see that, too.
     */
    for (;;) {
        c = *s++;
        spanp = (char *)delim;
        do {
            if ((sc = *spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                *lasts = s;
                return (tok);
            }
        } while (sc != 0);
    }
    /* NOTREACHED */
}

char *win32_strsep (char **stringp, const char *delim)
{
    register char *s;
    register const char *spanp;
    register int c, sc;
    char *tok;

    if ((s = *stringp) == NULL)
        return (NULL);
    for (tok = s;;) {
        c = *s++;
        spanp = delim;
        do {
            if ((sc = *spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                *stringp = s;
                return (tok);
            }
        } while (sc != 0);
    }
    /* NOTREACHED */
}

char *OS_nonportable_path(char *str)
{
    int32_t i;
    for (i=0; str[i]!=0; i++)
        if ( str[i] == '/' )
            str[i] = '\\';
    return(str);
}

void *OS_nonportable_mapfile(char *fname,long *filesizep,int32_t enablewrite)
{
	int32_t fd,rwflags,flags = MAP_FILE|MAP_SHARED;
	long filesize;
    void *ptr = 0;
	*filesizep = 0;
	if ( enablewrite != 0 )
		fd = _sopen(fname, _O_RDWR | _O_BINARY, _SH_DENYNO);
	else fd = _sopen(fname, _O_RDONLY | _O_BINARY, _SH_DENYNO);
	if ( fd < 0 )
	{
		//printf("map_file: error opening enablewrite.%d %s\n",enablewrite,fname);
        return(0);
	}
    if ( *filesizep == 0 )
        filesize = (long)lseek(fd,0,SEEK_END);
    else filesize = *filesizep;
 	rwflags = PROT_READ;
	if ( enablewrite != 0 )
		rwflags |= PROT_WRITE;
	ptr = mmap(0,filesize,rwflags,flags,fd,0);
	_close(fd);
    if ( ptr == 0 || ptr == MAP_FAILED )
	{
        if ( enablewrite != 0 )
            printf("map_file.write%d: mapping %s failed? mp %p\n",enablewrite,fname,ptr);
		return(0);
	}
	*filesizep = filesize;
	return(ptr);
}

int32_t OS_nonportable_removefile(char *fname)
{
    char tmp[512];
    strcpy(tmp,fname);
    OS_portable_path(tmp);
    return((DeleteFileA(tmp) == 0) ? -1 : 0);
}

int32_t OS_nonportable_renamefile(char *fname,char *newfname)
{
    char tmp[1024],tmp2[1024]; int32_t retval,retvaldel;
    strcpy(tmp,fname), strcpy(tmp2,newfname);
    OS_nonportable_path(tmp), OS_nonportable_path(tmp2);
    retvaldel = OS_nonportable_removefile(tmp2);
    retval = MoveFileA(tmp,tmp2);
    //printf("call Movefile(%s -> %s) retval.%d retvaldel.%d %d\n",tmp,tmp2,retval,retvaldel,GetLastError());
    return((retval == 0) ? -1 : 0);
}

int32_t  OS_nonportable_launch(char *args[])
{
    int32_t pid;
    pid = _spawnl( _P_NOWAIT, args[0], args[0],  NULL, NULL );
    return pid;
}

void OS_nonportable_randombytes(unsigned char *x,long xlen)
{
    HCRYPTPROV prov = 0;
    CryptAcquireContextW(&prov, NULL, NULL,PROV_RSA_FULL, CRYPT_VERIFYCONTEXT | CRYPT_SILENT);
    CryptGenRandom(prov, xlen, x);
    CryptReleaseContext(prov, 0);
}

int32_t OS_nonportable_init()
{
    // Initialize Windows Sockets
    WSADATA wsadata;
    int ret = WSAStartup(MAKEWORD(2,2), &wsadata);
    if (ret != NO_ERROR)
    {
        printf("Error: TCP/IP socket library failed to start (WSAStartup returned error %d)\n", ret);
        //printf("%s\n", strError.c_str());
        return -1;
    }
    printf("WSAStartup called\n");
    return(0);
}

#else
void OS_nonportable_none() { printf("unix is the reference OS\n"); }

#endif
