#include "stdafx.h"

#if defined(__USE_EPOLL__)

static uint32_t fdwatch_epoll_events_for(LPFDWATCH fdw, socket_t fd)
{
	uint32_t events = 0;

	if (fdw->fd_rw[fd] & FDW_READ)
		events |= EPOLLIN;

	if (fdw->fd_rw[fd] & FDW_WRITE)
		events |= EPOLLOUT;

	if (fdw->fd_rw[fd] & FDW_WRITE_ONESHOT)
		events |= EPOLLONESHOT;

	return events;
}

static int fdwatch_epoll_apply(LPFDWATCH fdw, socket_t fd, int op)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));
	struct epoll_event* ev_ptr = NULL;

	if (op != EPOLL_CTL_DEL)
	{
		ev.events = fdwatch_epoll_events_for(fdw, fd);
		ev.data.fd = fd;
		ev_ptr = &ev;
	}

	if (epoll_ctl(fdw->epfd, op, fd, ev_ptr) == -1)
	{
		if (!(op == EPOLL_CTL_DEL && errno == ENOENT))
			sys_err("epoll_ctl(%d, fd:%d) failed: %s", op, fd, strerror(errno));
		return -1;
	}

	return 0;
}

static void fdwatch_epoll_refresh(LPFDWATCH fdw, socket_t fd, int previous_flags)
{
	if (fdw->fd_rw[fd])
	{
		int op = previous_flags ? EPOLL_CTL_MOD : EPOLL_CTL_ADD;

		if (fdwatch_epoll_apply(fdw, fd, op) != 0 && op == EPOLL_CTL_MOD)
		{
			fdwatch_epoll_apply(fdw, fd, EPOLL_CTL_ADD);
		}
	}
	else if (previous_flags)
	{
		fdwatch_epoll_apply(fdw, fd, EPOLL_CTL_DEL);
	}
}

LPFDWATCH fdwatch_new(int nfiles)
{
	int epfd = epoll_create1(0);
	if (epfd == -1)
	{
		sys_err("epoll_create1 failed: %s", strerror(errno));
		return NULL;
	}

	LPFDWATCH fdw;
	CREATE(fdw, FDWATCH, 1);

	fdw->epfd = epfd;
	fdw->nfiles = nfiles;
	fdw->nevents = 0;

	CREATE(fdw->events, struct epoll_event, nfiles);
	CREATE(fdw->fd_data, void*, nfiles);
	CREATE(fdw->fd_rw, int, nfiles);

	return fdw;
}

void fdwatch_delete(LPFDWATCH fdw)
{
	if (!fdw)
		return;

	close(fdw->epfd);
	free(fdw->events);
	free(fdw->fd_data);
	free(fdw->fd_rw);
	free(fdw);
}

void fdwatch_clear_fd(LPFDWATCH fdw, socket_t fd)
{
	if (fd < 0 || fd >= fdw->nfiles)
		return;

	fdw->fd_data[fd] = NULL;
	fdw->fd_rw[fd] = 0;
}

void fdwatch_add_fd(LPFDWATCH fdw, socket_t fd, void* client_data, int rw, int oneshot)
{
	if (fd < 0 || fd >= fdw->nfiles)
	{
		sys_err("fd overflow %d", fd);
		return;
	}

	if (fdw->fd_rw[fd] & rw)
		return;

	int previous = fdw->fd_rw[fd];

	fdw->fd_rw[fd] |= rw;

	if (oneshot && (rw & FDW_WRITE))
		fdw->fd_rw[fd] |= FDW_WRITE_ONESHOT;

	fdw->fd_data[fd] = client_data;

	fdwatch_epoll_refresh(fdw, fd, previous);
}

void fdwatch_del_fd(LPFDWATCH fdw, socket_t fd)
{
	if (fd < 0 || fd >= fdw->nfiles)
		return;

	if (fdw->fd_rw[fd])
		fdwatch_epoll_apply(fdw, fd, EPOLL_CTL_DEL);

	fdwatch_clear_fd(fdw, fd);
}

static int fdwatch_epoll_mask(LPFDWATCH fdw, const struct epoll_event& ev)
{
	int fd = ev.data.fd;
	if (fd < 0 || fd >= fdw->nfiles)
		return 0;

	if (ev.events & (EPOLLERR | EPOLLHUP))
		return FDW_EOF;

	int mask = 0;

	if ((ev.events & EPOLLIN) && (fdw->fd_rw[fd] & FDW_READ))
		mask |= FDW_READ;

	if ((ev.events & EPOLLOUT) && (fdw->fd_rw[fd] & FDW_WRITE))
		mask |= FDW_WRITE;

	return mask;
}

int fdwatch(LPFDWATCH fdw, struct timeval *timeout)
{
	int timeout_ms = -1;

	if (timeout)
	{
		long long ms = static_cast<long long>(timeout->tv_sec) * 1000LL + timeout->tv_usec / 1000LL;
		if (ms < 0)
			ms = 0;
		timeout_ms = static_cast<int>(ms);
	}

	int r;
	do
	{
		r = epoll_wait(fdw->epfd, fdw->events, fdw->nfiles, timeout_ms);
	} while (r == -1 && errno == EINTR);

	if (r == -1)
		return -1;

	fdw->nevents = r;
	return r;
}

int fdwatch_check_fd(LPFDWATCH fdw, socket_t fd)
{
	for (int i = 0; i < fdw->nevents; ++i)
	{
		if (fdw->events[i].data.fd != fd)
			continue;

		return fdwatch_epoll_mask(fdw, fdw->events[i]);
	}

	return 0;
}

void* fdwatch_get_client_data(LPFDWATCH fdw, unsigned int event_idx)
{
	if (event_idx >= static_cast<unsigned int>(fdw->nevents))
		return NULL;

	int fd = fdw->events[event_idx].data.fd;
	if (fd < 0 || fd >= fdw->nfiles)
		return NULL;

	return fdw->fd_data[fd];
}

int fdwatch_get_ident(LPFDWATCH fdw, unsigned int event_idx)
{
	if (event_idx >= static_cast<unsigned int>(fdw->nevents))
		return 0;

	return fdw->events[event_idx].data.fd;
}

void fdwatch_clear_event(LPFDWATCH fdw, socket_t fd, unsigned int event_idx)
{
	if (event_idx >= static_cast<unsigned int>(fdw->nevents))
		return;

	if (fdw->events[event_idx].data.fd != fd)
		return;

	fdw->events[event_idx].events = 0;
}

int fdwatch_check_event(LPFDWATCH fdw, socket_t fd, unsigned int event_idx)
{
	if (event_idx >= static_cast<unsigned int>(fdw->nevents))
		return 0;

	struct epoll_event& ev = fdw->events[event_idx];
	if (ev.data.fd != fd)
		return 0;

	int mask = fdwatch_epoll_mask(fdw, ev);

	if (mask & FDW_EOF)
		return FDW_EOF;

	if (mask & FDW_READ)
		return FDW_READ;

	if (mask & FDW_WRITE)
	{
		if (fdw->fd_rw[fd] & FDW_WRITE_ONESHOT)
		{
			int previous = fdw->fd_rw[fd];
			fdw->fd_rw[fd] &= ~(FDW_WRITE | FDW_WRITE_ONESHOT);
			fdwatch_epoll_refresh(fdw, fd, previous);
		}

		return FDW_WRITE;
	}

	return 0;
}

int fdwatch_get_buffer_size(LPFDWATCH fdw, socket_t fd)
{
	(void)fdw;
	(void)fd;
	return INT_MAX;
}

#elif !defined(__USE_SELECT__)

LPFDWATCH fdwatch_new(int nfiles)
{
    LPFDWATCH fdw;
    int kq;

    kq = kqueue();

    if (kq == -1)
    {
	sys_err("%s", strerror(errno));
	return NULL;
    }

    CREATE(fdw, FDWATCH, 1);

    fdw->kq = kq;
    fdw->nfiles = nfiles;
    fdw->nkqevents = 0;

    CREATE(fdw->kqevents, KEVENT, nfiles * 2);
    CREATE(fdw->kqrevents, KEVENT, nfiles * 2);
    CREATE(fdw->fd_event_idx, int, nfiles);
    CREATE(fdw->fd_rw, int, nfiles);
    CREATE(fdw->fd_data, void*, nfiles);

    return (fdw);
}

void fdwatch_delete(LPFDWATCH fdw)
{
    free(fdw->fd_data);
    free(fdw->fd_rw);
    free(fdw->kqevents);
    free(fdw->kqrevents);
    free(fdw->fd_event_idx);
    free(fdw);
}

int fdwatch(LPFDWATCH fdw, struct timeval *timeout)
{
    int	i, r;
    struct timespec ts;

    if (fdw->nkqevents)
	sys_log(2, "fdwatch: nkqevents %d", fdw->nkqevents);

    if (!timeout)
    {
	ts.tv_sec = 0;
	ts.tv_nsec = 0;

	r = kevent(fdw->kq, fdw->kqevents, fdw->nkqevents, fdw->kqrevents, fdw->nfiles, &ts);
    }
    else
    {
	ts.tv_sec = timeout->tv_sec;
	ts.tv_nsec = timeout->tv_usec;

	r = kevent(fdw->kq, fdw->kqevents, fdw->nkqevents, fdw->kqrevents, fdw->nfiles, &ts);
    }

    fdw->nkqevents = 0;

    if (r == -1)
	return -1;

    memset(fdw->fd_event_idx, 0, sizeof(int) * fdw->nfiles);

    for (i = 0; i < r; i++)
    {
	int fd = fdw->kqrevents[i].ident;

	if (fd >= fdw->nfiles)
	    sys_err("ident overflow %d nfiles: %d", fdw->kqrevents[i].ident, fdw->nfiles);
	else
	{
	    if (fdw->kqrevents[i].filter == EVFILT_WRITE)
		fdw->fd_event_idx[fd] = i;
	}
    }

    return (r);
}

void fdwatch_register(LPFDWATCH fdw, int flag, int fd, int rw)
{
    if (flag == EV_DELETE)
    {
	if (fdw->fd_rw[fd] & FDW_READ)
	{
	    fdw->kqevents[fdw->nkqevents].ident = fd;
	    fdw->kqevents[fdw->nkqevents].flags = flag;
	    fdw->kqevents[fdw->nkqevents].filter = EVFILT_READ;
	    ++fdw->nkqevents;
	}

	if (fdw->fd_rw[fd] & FDW_WRITE)
	{
	    fdw->kqevents[fdw->nkqevents].ident = fd;
	    fdw->kqevents[fdw->nkqevents].flags = flag;
	    fdw->kqevents[fdw->nkqevents].filter = EVFILT_WRITE;
	    ++fdw->nkqevents;
	}
    }
    else
    {
	fdw->kqevents[fdw->nkqevents].ident = fd;
	fdw->kqevents[fdw->nkqevents].flags = flag;
	fdw->kqevents[fdw->nkqevents].filter = (rw == FDW_READ) ? EVFILT_READ : EVFILT_WRITE; 

	++fdw->nkqevents;
    }
}

void fdwatch_clear_fd(LPFDWATCH fdw, socket_t fd)
{
    fdw->fd_data[fd] = NULL;
    fdw->fd_rw[fd] = 0;
}

void fdwatch_add_fd(LPFDWATCH fdw, socket_t fd, void * client_data, int rw, int oneshot)
{
	int flag;

	if (fd >= fdw->nfiles)
	{
		sys_err("fd overflow %d", fd);
		return;
	}

	if (fdw->fd_rw[fd] & rw)
		return;

	fdw->fd_rw[fd] |= rw;
	sys_log(2, "FDWATCH_fdw %p fd %d rw %d data %p", fdw, fd, rw, client_data);

	if (!oneshot)
		flag = EV_ADD;
	else
	{
		sys_log(2, "ADD ONESHOT fd_rw %d", fdw->fd_rw[fd]);
		flag = EV_ADD | EV_ONESHOT;
		fdw->fd_rw[fd] |= FDW_WRITE_ONESHOT;
	}

	fdw->fd_data[fd] = client_data;
	fdwatch_register(fdw, flag, fd, rw);
}

void fdwatch_del_fd(LPFDWATCH fdw, socket_t fd)
{
    fdwatch_register(fdw, EV_DELETE, fd, 0);
    fdwatch_clear_fd(fdw, fd);
}

void fdwatch_clear_event(LPFDWATCH fdw, socket_t fd, unsigned int event_idx)
{
    assert(event_idx < fdw->nfiles * 2);

    if (fdw->kqrevents[event_idx].ident != fd)
	return;

    fdw->kqrevents[event_idx].ident = 0;
}

int fdwatch_check_event(LPFDWATCH fdw, socket_t fd, unsigned int event_idx)
{
    assert(event_idx < fdw->nfiles * 2);

    if (fdw->kqrevents[event_idx].ident != fd)
	return 0;

    if (fdw->kqrevents[event_idx].flags & EV_ERROR)
	return FDW_EOF;

    if (fdw->kqrevents[event_idx].flags & EV_EOF)
	return FDW_EOF;

    if (fdw->kqrevents[event_idx].filter == EVFILT_READ)
    {
	if (fdw->fd_rw[fd] & FDW_READ)
	    return FDW_READ;
    }
    else if (fdw->kqrevents[event_idx].filter == EVFILT_WRITE)
    {   
	if (fdw->fd_rw[fd] & FDW_WRITE)
	{ 
	    if (fdw->fd_rw[fd] & FDW_WRITE_ONESHOT)
		fdw->fd_rw[fd] &= ~FDW_WRITE;

	    return FDW_WRITE;
	}
    }
    else
	sys_err("fdwatch_check_event: Unknown filter %d (descriptor %d)", fdw->kqrevents[event_idx].filter, fd);

    return 0;
}

int fdwatch_get_ident(LPFDWATCH fdw, unsigned int event_idx)
{
    assert(event_idx < fdw->nfiles * 2);
    return fdw->kqrevents[event_idx].ident;
}

int fdwatch_get_buffer_size(LPFDWATCH fdw, socket_t fd)
{
#if defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
    int size = 0;
    socklen_t len = sizeof(size);

    if (ioctl(fd, FIONREAD, &size) < 0)
	return -1;

    return size;
#else
    (void) fdw;
    (void) fd;
    return 1024;
#endif
}

#else

LPFDWATCH fdwatch_new(int nfiles)
{
    LPFDWATCH fdw;

    CREATE(fdw, FDWATCH, 1);

    fdw->nfiles = nfiles;
    fdw->nselect_fds = 0;

    CREATE(fdw->select_fds, socket_t, nfiles);
    CREATE(fdw->select_rfdidx, int, nfiles);

    CREATE(fdw->fd_data, void*, nfiles);
    CREATE(fdw->fd_rw, int, nfiles);

    FD_ZERO(&fdw->rfd_set);
    FD_ZERO(&fdw->wfd_set);

    return (fdw);
}

void fdwatch_delete(LPFDWATCH fdw)
{
    free(fdw->fd_data);
    free(fdw->fd_rw);
    free(fdw->select_fds);
    free(fdw->select_rfdidx);
    free(fdw);
}

static int fdwatch_get_fdidx(LPFDWATCH fdw, socket_t fd)
{
    int i;

    for (i = 0; i < fdw->nselect_fds; ++i)
    {
	if (fdw->select_fds[i] == fd)
	    return i;
    }

    return -1;
}

void fdwatch_clear_fd(LPFDWATCH fdw, socket_t fd)
{
    int idx = fdwatch_get_fdidx(fdw, fd);

    if (idx == -1)
	return;

    fdw->fd_data[idx] = NULL;
    fdw->fd_rw[idx] = 0;
}

void fdwatch_add_fd(LPFDWATCH fdw, socket_t fd, void* client_data, int rw, int oneshot)
{
	int idx;

	if (fd >= fdw->nfiles)
	{
		sys_err("fd overflow %d", fd);
		return;
	}

	idx = fdwatch_get_fdidx(fdw, fd);

	if (idx == -1)
	{
		if (fdw->nselect_fds >= fdw->nfiles) {
			return;
		}
		idx = fdw->nselect_fds;
		fdw->select_fds[fdw->nselect_fds++] = fd;
	}

	fdw->fd_data[idx] = client_data;

	if (rw & FDW_READ)
	{
		fdw->fd_rw[idx] |= FDW_READ;
		FD_SET(fd, &fdw->rfd_set);
	}

	if (rw & FDW_WRITE)
	{
		fdw->fd_rw[idx] |= FDW_WRITE;
		FD_SET(fd, &fdw->wfd_set);
	}

	if (oneshot && (rw & FDW_WRITE))
		fdw->fd_rw[idx] |= FDW_WRITE_ONESHOT;
}

void fdwatch_del_fd(LPFDWATCH fdw, socket_t fd)
{
	if (fdw->nselect_fds <= 0) {
		return;
	}
    int idx = fdwatch_get_fdidx(fdw, fd);
	if (idx < 0) {
		return;
	}

	--fdw->nselect_fds;

	fdw->select_fds[idx] = fdw->select_fds[fdw->nselect_fds];
    fdw->fd_data[idx] = fdw->fd_data[fdw->nselect_fds];
    fdw->fd_rw[idx] = fdw->fd_rw[fdw->nselect_fds];

    FD_CLR(fd, &fdw->rfd_set);
    FD_CLR(fd, &fdw->wfd_set);
}

int fdwatch(LPFDWATCH fdw, struct timeval *timeout)
{
    int r, i, event_idx;
    struct timeval tv;

    fdw->working_rfd_set = fdw->rfd_set;
    fdw->working_wfd_set = fdw->wfd_set;

    if (!timeout)
    {
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	r = select(0, &fdw->working_rfd_set, &fdw->working_wfd_set, (fd_set*) 0, &tv);
    }
    else
    {
	tv = *timeout;
	r = select(0, &fdw->working_rfd_set, &fdw->working_wfd_set, (fd_set*) 0, &tv);
    }

    if (r == -1)
	return -1;

    event_idx = 0;

    for (i = 0; i < fdw->nselect_fds; ++i)
    {
		if (fdwatch_check_fd(fdw, fdw->select_fds[i]))
			fdw->select_rfdidx[event_idx++] = i;
    }

    return event_idx;
}

int fdwatch_check_fd(LPFDWATCH fdw, socket_t fd)
{
    int idx = fdwatch_get_fdidx(fdw, fd);
	if (idx < 0) {
		return 0;
	}
	int result = 0;
	if ((fdw->fd_rw[idx] & FDW_READ) && FD_ISSET(fd, &fdw->working_rfd_set)) {
		result |= FDW_READ;
	}
	if ((fdw->fd_rw[idx] & FDW_WRITE) && FD_ISSET(fd, &fdw->working_wfd_set)) {
		result |= FDW_WRITE;
	}
    return result;
}

void * fdwatch_get_client_data(LPFDWATCH fdw, unsigned int event_idx)
{
	int idx = fdw->select_rfdidx[event_idx];
	if (idx < 0 || fdw->nfiles <= idx) {
		return NULL;
	}
    return fdw->fd_data[idx];
}

int fdwatch_get_ident(LPFDWATCH fdw, unsigned int event_idx)
{
	int idx = fdw->select_rfdidx[event_idx];
	if (idx < 0 || fdw->nfiles <= idx) {
		return 0;
	}
	return (int)fdw->select_fds[idx];
}

void fdwatch_clear_event(LPFDWATCH fdw, socket_t fd, unsigned int event_idx)
{
	int idx = fdw->select_rfdidx[event_idx];
	if (idx < 0 || fdw->nfiles <= idx) {
		return;
	}
	socket_t rfd = fdw->select_fds[idx];
	if (fd != rfd) {
		return;
	}
    FD_CLR(fd, &fdw->working_rfd_set);
    FD_CLR(fd, &fdw->working_wfd_set);
}

int fdwatch_check_event(LPFDWATCH fdw, socket_t fd, unsigned int event_idx)
{
	int idx = fdw->select_rfdidx[event_idx];
	if (idx < 0 || fdw->nfiles <= idx) {
		return 0;
	}
	socket_t rfd = fdw->select_fds[idx];
	if (fd != rfd) {
		return 0;
	}
	int result = fdwatch_check_fd(fdw, fd);
	if (result & FDW_READ) {
		return FDW_READ;
	} else if (result & FDW_WRITE) {
		return FDW_WRITE;
	}
	return 0;
}

int fdwatch_get_buffer_size(LPFDWATCH fdw, socket_t fd)
{
    return INT_MAX; // XXX TODO
}

#endif
