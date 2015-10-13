/* $Id: sequencer.c,v 1.13 2015/10/13 20:12:03 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkilä <je@turnipsi.no-ip.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/queue.h>
#include <sys/socket.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "sequencer.h"

#define MIDIEVENT_SIZE		3
#define SEQ_CHANNEL_MAX		15
#define SEQ_NOTE_MAX		127
#define SEQ_VELOCITY_MAX	127
#define SEQ_NOTEOFF_BASE	0x80
#define SEQ_NOTEON_BASE		0x90

struct eventblock {
	SIMPLEQ_ENTRY(eventblock) entries;
	struct midievent events[1024];
	size_t readcount;
};
SIMPLEQ_HEAD(eventstream, eventblock);

struct notestate {
	unsigned int state    : 1;
	unsigned int velocity : 7;
};

static struct notestate
  sequencer_notestates[SEQ_CHANNEL_MAX+1][SEQ_NOTE_MAX+1];

static struct mio_hdl  *mio = NULL;
static float		tempo = 120;

static ssize_t	read_to_eventstream(struct eventstream *,
				    struct eventblock **,
				    int);

static int	play_music(struct eventstream *);
static int	receive_fd_through_socket(int);
static void	sequencer_assert_range(u_int8_t, u_int8_t, u_int8_t);
static void	sequencer_close(void);
static int	sequencer_init(void);
static int	sequencer_noteevent(u_int8_t, u_int8_t, u_int8_t, u_int8_t);
static int	sequencer_noteoff(u_int8_t, u_int8_t);
static int	sequencer_noteon(u_int8_t, u_int8_t, u_int8_t);
static int	sequencer_send_midievent(const u_int8_t *);

static int
receive_fd_through_socket(int socket)
{
	struct msghdr	msg;
	struct cmsghdr *cmsg;
	union {                           
		struct cmsghdr	hdr;
		unsigned char	buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;
	int fd;

	memset(&msg, 0, sizeof(msg));
	msg.msg_control    = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	if (recvmsg(socket, &msg, 0) == -1) {
		warn("receiving fd through socket, recvmsg");
		return -1;
	}

	if ((msg.msg_flags & MSG_TRUNC) || (msg.msg_flags & MSG_CTRUNC)) {
		warn("receiving fd through socket," \
                       "  control message truncated");
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg
              && cmsg->cmsg_len   == CMSG_LEN(sizeof(int))
              && cmsg->cmsg_level == SOL_SOCKET
              && cmsg->cmsg_type  == SCM_RIGHTS) {
		fd = *(int *)CMSG_DATA(cmsg);
		return fd;
	}

	warnx("did not receive fd while expecting for it");
	return -1;
}

static int
sequencer_init(void)
{
	if ((mio = mio_open(MIO_PORTANY, MIO_OUT, 0)) == NULL) {
		warnx("could not open midi device");
		return 1;
	}

	return 0;
}

int
sequencer_loop(int main_socket)
{
	struct eventstream evstream1, evstream2;
	struct eventstream *playback_eventstream;
	struct eventblock *current_block;
	int interp_fd, nr;

	SIMPLEQ_INIT(&evstream1);
	SIMPLEQ_INIT(&evstream2);

	playback_eventstream = &evstream1;
	current_block = NULL;

	if (sequencer_init() != 0) {
		warnx("problem initializing sequencer, exiting");
		return 1;
	}

	if ((interp_fd = receive_fd_through_socket(main_socket)) == -1) {
		warn("error receiving interpreter socket for sequencer");
		sequencer_close();
		return 1;
	}

	for (;;) {
		nr = read_to_eventstream(playback_eventstream,
					 &current_block,
					 interp_fd);
		if (nr == -1) {
			warn("reading to playback_evbuf");
			if (close(interp_fd) == -1)
				warn("closing interpreter fd");
			sequencer_close();
			return 1;
		}
		if (nr == 0)
			break;
	}

	(void) play_music(playback_eventstream);

	sequencer_close();

	return 0;
}

static int
play_music(struct eventstream *es)
{
	struct eventblock *eb;
	struct midievent *ev;
	int evcount, i;

	SIMPLEQ_FOREACH(eb, es, entries) {
		if ((eb->readcount % sizeof(struct midievent)) > 0) {
			warnx("received music stream which is not complete");
			return 1;
		}

		evcount = eb->readcount / sizeof(struct midievent); 

		ev = (struct midievent *) eb->events;
		for (i = 0; i < evcount; i++, ev++) {
			printf("received %d %d %d %d %f\n",
			       ev->eventtype,
			       ev->channel,
			       ev->note,
			       ev->velocity,
			       ev->time_as_measures);
			fflush(stdout);
		}
	}

	return 0;
}

static void
sequencer_assert_range(u_int8_t channel, u_int8_t min, u_int8_t max)
{
	assert(min <= channel && channel <= max);
}

static int
sequencer_send_midievent(const u_int8_t *midievent)
{
	int ret;

	assert(mio != NULL);

	ret = mio_write(mio, midievent, MIDIEVENT_SIZE);
	if (ret != MIDIEVENT_SIZE) {
		warnx("midi error, tried to write exactly %d bytes, wrote %d",
		      MIDIEVENT_SIZE,
		      ret);
		return 1;
	}

	return 0;
}

static int
sequencer_noteevent(u_int8_t note_on,
		    u_int8_t channel,
		    u_int8_t note,
		    u_int8_t velocity)
{
	u_int8_t midievent[MIDIEVENT_SIZE];
	int ret;
	u_int8_t eventbase;

	sequencer_assert_range(channel,  0, SEQ_CHANNEL_MAX);
	sequencer_assert_range(note,     0, SEQ_NOTE_MAX);
	sequencer_assert_range(velocity, 0, SEQ_VELOCITY_MAX);

	eventbase = note_on ? SEQ_NOTEON_BASE : SEQ_NOTEOFF_BASE;

	midievent[0] = (u_int8_t) (eventbase + channel);
	midievent[1] = note;
	midievent[2] = velocity;

	ret = sequencer_send_midievent(midievent);
	if (ret == 0) {
		sequencer_notestates[channel][note].state    = note_on ? 1 : 0;
		sequencer_notestates[channel][note].velocity = velocity;
	}

	return ret;
}

static int
sequencer_noteon(u_int8_t channel, u_int8_t note, u_int8_t velocity)
{
	return sequencer_noteevent(1, channel, note, velocity);
}

static int
sequencer_noteoff(u_int8_t channel, u_int8_t note)
{
	return sequencer_noteevent(0, channel, note, 0);
}

static void
sequencer_close(void)
{
	int c, n;

	for (c = 0; c < SEQ_CHANNEL_MAX; c++)
		for (n = 0; n < SEQ_NOTE_MAX; n++)
			if (sequencer_notestates[c][n].state)
				if (sequencer_noteoff(c, n) != 0)
					warnx("error in turning off note"
						" %d on channel %d", n, c);

	mio_close(mio);
}

static ssize_t
read_to_eventstream(struct eventstream *es,
                    struct eventblock **cur_eb,
		    int fd)
{
	ssize_t nr;

	assert(fd >= 0);
	assert(es != NULL);

	if (cur_eb == NULL || *cur_eb == NULL
	      || (*cur_eb)->readcount == sizeof((*cur_eb)->events)) {
		*cur_eb = malloc(sizeof(struct eventblock));
		if (*cur_eb == NULL) {
			warn("malloc failure in read_to_eventstream");
			return -1;
		}

		(*cur_eb)->readcount = 0;
		SIMPLEQ_INSERT_TAIL(es, *cur_eb, entries);
	}

	nr = read(fd,
		  (char *) (*cur_eb)->events + (*cur_eb)->readcount,
		  sizeof((*cur_eb)->events) - (*cur_eb)->readcount);
	if (nr == 0 || nr == -1)
		return nr;

	(*cur_eb)->readcount += nr;

	return nr;
}
