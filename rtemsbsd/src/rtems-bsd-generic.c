/**
 * @file
 *
 * @ingroup rtems_bsd_rtems
 *
 * @brief TODO.
 */

/*
 * Copyright (c) 2009, 2010 embedded brains GmbH.  All rights reserved.
 *
 *  embedded brains GmbH
 *  Obere Lagerstr. 30
 *  82178 Puchheim
 *  Germany
 *  <rtems@embedded-brains.de>
 *
 * The license and distribution terms for this file may be
 * found in the file LICENSE in this distribution or at
 * http://www.rtems.com/license/LICENSE.
 */

#include <sys/types.h> //needed for fd_mask and such
#include <freebsd/machine/rtems-bsd-config.h>

#include <freebsd/sys/types.h>
#include <freebsd/sys/systm.h>
#include <freebsd/sys/selinfo.h>
#include <freebsd/sys/fcntl.h>
#include <freebsd/sys/socket.h>
#include <freebsd/sys/socketvar.h>
#include <freebsd/sys/protosw.h>
#include <freebsd/sys/select.h>
#include <freebsd/sys/kernel.h>
#include <freebsd/sys/lock.h>
#include <freebsd/sys/mutex.h>
#include <freebsd/sys/malloc.h>

void selrecord(struct thread *selector, struct selinfo *sip)
{
	BSD_PANIC("not implemented");
}

void selwakeup(struct selinfo *sip)
{
	BSD_PANIC("not implemented");
}

void selwakeuppri(struct selinfo *sip, int pri)
{
	BSD_PANIC("not implemented");
}

void seltdfini(struct thread *td)
{
	BSD_PANIC("not implemented");
}

/*
 *********************************************************************
 *            RTEMS implementation of select() system call           *
 *********************************************************************
 */

/*
 * This implementation is quite restricted:
 *	Works on sockets only -- no support for other devices!
 *	A given socket can be in a read-select or a read/recv* by only
 *		one task at a time.
 *	A given socket can be in a write-select or a write/send* by only
 *		one task at a time.
 *
 * NOTE - select() is a very expensive system call.  It should be avoided
 *        if at all possible.  In many cases, rewriting the application
 *        to use multiple tasks (one per socket) is a better solution.
 */

struct socket *rtems_bsdnet_fdToSocket(int fd);

static int
socket_select (struct socket *so, int which, rtems_id tid)
{
	switch (which) {

	case FREAD:
		if (soreadable(so))
			return (1);
		SOCK_LOCK(so);
		so->so_rcv.sb_flags |= SB_WAIT;
		so->so_rcv.sb_sel.si_pid = tid;
		SOCK_UNLOCK(so);
		break;

	case FWRITE:
		if (sowriteable(so))
			return (1);
		SOCK_LOCK(so);
		so->so_snd.sb_flags |= SB_WAIT;
		so->so_snd.sb_sel.si_pid = tid;
		SOCK_UNLOCK(so);
		break;

	case 0:
		if (so->so_oobmark || (so->so_state & SBS_RCVATMARK))
			return (1);
		SOCK_LOCK(so);
		so->so_rcv.sb_sel.si_pid = tid;
		SOCK_UNLOCK(so);
		break;
	}
	return (0);
}

static int
selscan (rtems_id tid, fd_mask **ibits, fd_mask **obits, int nfd, int *retval)
{
	struct socket *so;
	int msk, i, fd;
	fd_mask bits, bit;
	int n = 0;
	static int flag[3] = { FREAD, FWRITE, 0 };

	for (msk = 0; msk < 3; msk++) {
		if (ibits[msk] == NULL)
			continue;
		for (i = 0; i < nfd; i += NFDBITS) {
			bits = ibits[msk][i/NFDBITS];
			for (fd = i, bit = 1 ; bits && (fd < nfd) ; fd++, bit <<= 1) {
				if ((bits & bit) == 0)
					continue;
				bits &= ~bit;
				so = rtems_bsdnet_fdToSocket (fd);
				if (so == NULL)
					return (EBADF);
				if (socket_select (so, flag[msk], tid)) {
					obits[msk][fd/NFDBITS] |=
							(1 << (fd % NFDBITS));
					n++;
				}
			}
		}
	}
	*retval = n;
	return (0);
}

int
select (int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *tv)
{
	fd_mask *ibits[3], *obits[3];
	fd_set ob[3];
	int error, timo;
	int retval = 0;
	rtems_id tid;
	rtems_interval then = 0, now;
	rtems_event_set events;

	if (nfds < 0)
		return (EINVAL);
	if (tv) {
		timo = tv->tv_sec * hz + tv->tv_usec / tick;
		if (timo == 0)
			timo = 1;
		then = rtems_clock_get_ticks_since_boot();
	}
	else {
		timo = 0;
	}

#define getbits(name,i) if (name) { \
		ibits[i] = &name->fds_bits[0]; \
		obits[i] = &ob[i].fds_bits[0]; \
		FD_ZERO(&ob[i]); \
	} \
	else ibits[i] = NULL
	getbits (readfds, 0);
	getbits (writefds, 1);
	getbits (exceptfds, 2);
#undef getbits

	//rtems_task_ident (RTEMS_SELF, 0, &tid);
	//rtems_event_receive (SBWAIT_EVENT, RTEMS_EVENT_ANY | RTEMS_NO_WAIT, RTEMS_NO_TIMEOUT, &events);
	for (;;) {
		error = selscan(tid, ibits, obits, nfds, &retval);
		if (error || retval)
			break;
		if (timo) {
			now = rtems_clock_get_ticks_since_boot();
			timo -= now - then;
			if (timo <= 0)
				break;
			then = now;
		}
		//rtems_event_receive (SBWAIT_EVENT, RTEMS_EVENT_ANY | RTEMS_WAIT, timo, &events);
	}

#define putbits(name,i) if (name) *name = ob[i]
	putbits (readfds, 0);
	putbits (writefds, 1);
	putbits (exceptfds, 2);
#undef putbits
	if (error) {
		errno = error;
		retval = -1;
	}
	return (retval);
}
