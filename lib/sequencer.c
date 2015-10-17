/* $Id: sequencer.c,v 1.20 2015/10/17 20:41:35 je Exp $ */

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
#include <sys/select.h>
#include <sys/socket.h>

#include <assert.h>
#include <err.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

static ssize_t	sequencer_read_to_eventstream(struct eventstream *,
					      struct eventblock **,
					      float *,
					      int);

static int	sequencer_check_midievent(struct midievent, float *);
static int	sequencer_check_range(u_int8_t, u_int8_t, u_int8_t);
static void	sequencer_close(void);
static int	sequencer_init(void);
static int	sequencer_noteevent(u_int8_t, u_int8_t, u_int8_t, u_int8_t);
static int	sequencer_noteoff(u_int8_t, u_int8_t);
static int	sequencer_noteon(u_int8_t, u_int8_t, u_int8_t);
static int	sequencer_play_music(struct eventstream *);
static int	sequencer_prepare_eventstream(struct eventstream *);
static int	sequencer_send_midievent(const u_int8_t *);

static int	receive_fd_through_socket(int);

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
	struct eventstream *playback_es, *reading_es;
	struct eventblock *current_block, *tmp_block;
	struct timeval nextnotewait;
	fd_set readfds;
	float time_as_measures;
	int retvalue, interp_fd, prepare_eventstream, ret, nr;

	SIMPLEQ_INIT(&evstream1);
	SIMPLEQ_INIT(&evstream2);

	interp_fd = -1;
	playback_es = NULL;
	reading_es = &evstream1;
	current_block = NULL;
	prepare_eventstream = 0;
	time_as_measures = 0;

	if (sequencer_init() != 0) {
		warnx("problem initializing sequencer, exiting");
		return 1;
	}

	for (;;) {
		FD_ZERO(&readfds);
		FD_SET(main_socket, &readfds);

		if (prepare_eventstream
		      && sequencer_prepare_eventstream(reading_es))
			prepare_eventstream = 0;

		if (!prepare_eventstream && interp_fd >= 0)
			FD_SET(interp_fd, &readfds);

		nextnotewait.tv_sec = 1;
		nextnotewait.tv_usec = 0;

		ret = select(FD_SETSIZE, &readfds, NULL, NULL, &nextnotewait);
		if (ret == -1) {
			warn("error in select");
			retvalue = 1;
			goto finish;
		}

		if (FD_ISSET(main_socket, &readfds)) {
			if (interp_fd >= 0 && close(interp_fd) == -1)
				warn("closing old interpreter fd");
			interp_fd = receive_fd_through_socket(main_socket);
			if (interp_fd == -1) {
				warnx("error receiving pipe from interpreter" \
				        " to sequencer");
				retvalue = 1;
				goto finish;
			}
			continue;
		}

		if (playback_es) {
			/* XXX play all notes that should be played now */
			(void) sequencer_play_music(playback_es);
		}

		if (FD_ISSET(interp_fd, &readfds)) {
			nr = sequencer_read_to_eventstream(reading_es,
							   &current_block,
							   &time_as_measures,
							   interp_fd);
			if (nr == -1) {
				retvalue = 1;
				goto finish;
			}
			if (nr == 0) {
				/* we have a new playback stream, great! */
				playback_es = reading_es;
				reading_es = (playback_es == &evstream1)
					       ? &evstream2
					       : &evstream1;
				/* XXX synchronizing playback state should be
				 * XXX done now (based on time_as_measures) */
				prepare_eventstream = 1;
				if (close(interp_fd) == -1)
					warn("closing interpreter fd");
				interp_fd = -1;
			}
		}
	}

finish:
	if (interp_fd >= 0 && close(interp_fd) == -1)
		warn("closing interpreter fd");

	SIMPLEQ_FOREACH_SAFE(current_block, &evstream1, entries, tmp_block)
		free(current_block);
	SIMPLEQ_FOREACH_SAFE(current_block, &evstream2, entries, tmp_block)
		free(current_block);

	sequencer_close();

	return retvalue;
}

static int
sequencer_play_music(struct eventstream *es)
{
	struct eventblock *eb;
	struct midievent *ev;
	int evcount, i;

	SIMPLEQ_FOREACH(eb, es, entries) {
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

static int
sequencer_prepare_eventstream(struct eventstream *es)
{
	struct eventblock *b1, *b2;
	int i;

	SIMPLEQ_FOREACH_SAFE(b1, es, entries, b2) {
		/* do not allow this to go on too long so that we could miss
		 * our playback moment */
		if (i++ >= 64)
			return 0;
		free(b1);
	}

	/* eventstream is empty, return true */
	return 1;
}

static int
sequencer_check_midievent(struct midievent me, float *time_as_measures)
{
	if (!sequencer_check_range(me.eventtype, 0, EVENTTYPE_COUNT)) {
		warnx("midievent eventtype is invalid: %d", me.eventtype);
		return 0;
	}

	if (!sequencer_check_range(me.channel, 0, SEQ_CHANNEL_MAX)) {
		warnx("midievent channel is invalid: %d", me.channel);
		return 0;
	}

	if (!sequencer_check_range(me.note, 0, SEQ_NOTE_MAX)) {
		warnx("midievent note is invalid: %s", me.note);
		return 0;
	}

	if (!sequencer_check_range(me.velocity, 0, SEQ_VELOCITY_MAX)) {
		warnx("midievent velocity is invalid: %s", me.velocity);
		return 0;
	}

	if (!isfinite(me.time_as_measures)) {
		warnx("time_as_measures is not a valid (finite) value");
		return 0;
	}

	if (me.time_as_measures < *time_as_measures) {
		warnx("time is decreasing in eventstream");
		return 0;
	}

	*time_as_measures = me.time_as_measures;

	return 1;
}

static int
sequencer_check_range(u_int8_t value, u_int8_t min, u_int8_t max)
{
	return (min <= value && value <= max);
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

static ssize_t
sequencer_read_to_eventstream(struct eventstream *es,
			      struct eventblock **cur_eb,
			      float *time_as_measures,
			      int fd)
{
	ssize_t nr;
	int ret, i;

	assert(fd >= 0);
	assert(es != NULL);

	if (cur_eb == NULL || *cur_eb == NULL
	      || (*cur_eb)->readcount == sizeof((*cur_eb)->events)) {
		*cur_eb = malloc(sizeof(struct eventblock));
		if (*cur_eb == NULL) {
			warn("malloc failure in" \
			       " sequencer_read_to_eventstream");
			return -1;
		}

		(*cur_eb)->readcount = 0;
		SIMPLEQ_INSERT_TAIL(es, *cur_eb, entries);
	}

	nr = read(fd,
		  (char *) (*cur_eb)->events + (*cur_eb)->readcount,
		  sizeof((*cur_eb)->events) - (*cur_eb)->readcount);

	if (nr == -1) {
		warn("error in reading to eventstream");
		return nr;
	}

	if (nr == 0) {
		if ((*cur_eb)->readcount % sizeof(struct midievent) > 0) {
			warnx("received music stream which" \
				" is not complete");
			return -1;
		}

		return nr;
	}

	for (i = (*cur_eb)->readcount / sizeof(struct midievent);
	     i < ((*cur_eb)->readcount + nr) / sizeof(struct midievent);
	     i++) {
		if (!sequencer_check_midievent((*cur_eb)->events[i],
					       time_as_measures))
			return -1;
	}

	(*cur_eb)->readcount += nr;

	return nr;
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
		warn("receiving fd through socket, control message truncated");
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
