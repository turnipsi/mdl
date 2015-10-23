/* $Id: sequencer.c,v 1.33 2015/10/23 19:23:24 je Exp $ */

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

#define EVENTBLOCKCOUNT		1024

struct eventblock {
	SIMPLEQ_ENTRY(eventblock) entries;
	struct midievent events[EVENTBLOCKCOUNT];
	size_t readcount;
};
SIMPLEQ_HEAD(eventstream, eventblock);

struct eventpointer {
	struct eventblock      *block;
	int			index;
};

struct notestate {
	unsigned int state    : 1;
	unsigned int velocity : 7;
};

enum playback_state_t { IDLE, READING, PLAYING, FREEING_EVENTSTREAM, };

struct songstate {
	struct notestate notestates[SEQ_CHANNEL_MAX+1][SEQ_NOTE_MAX+1];
	struct eventstream es;
	struct eventpointer current_event;
	struct timeval latest_tempo_change_as_time;
	float latest_tempo_change_as_measures, tempo, time_as_measures;
	int measure_length;
	enum playback_state_t playback_state;
};

static struct mio_hdl  *mio = NULL;

static void	sequencer_calculate_timeout(struct songstate *,
					    struct timeval *);
static int	sequencer_check_midievent(struct midievent, float);
static int	sequencer_check_range(u_int8_t, u_int8_t, u_int8_t);
static void	sequencer_close(void);
static void	sequencer_close_songstate(struct songstate *);
static void	sequencer_free_songstate(struct songstate *);

static int	sequencer_init(void);
static void	sequencer_init_songstate(struct songstate *,
					 enum playback_state_t);
static int	sequencer_noteevent(struct songstate *,
				    int,
				    u_int8_t,
				    u_int8_t,
				    u_int8_t);
static int	sequencer_noteoff(struct songstate *, u_int8_t, u_int8_t);
static int	sequencer_noteon(struct songstate *,
				 u_int8_t,
				 u_int8_t,
				 u_int8_t);
static int	sequencer_play_music(struct songstate *);
static ssize_t	sequencer_read_to_eventstream(struct songstate *, int);
static int	sequencer_reset_songstate(struct songstate *);
static int	sequencer_send_midievent(const u_int8_t *);
static int	sequencer_start_playing(struct songstate *,
					struct songstate *);
static void	sequencer_time_for_next_note(struct songstate *ss,
					     struct timeval *notetime);

static int	receive_fd_through_socket(int *, int);

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
	struct songstate song1, song2;
	struct songstate *playback_song, *reading_song, *tmp_song;
	fd_set readfds;
	int retvalue, interp_fd, old_interp_fd, ret, nr;
	struct timeval timeout, *timeout_p;

	retvalue = 0;

	interp_fd = -1;
	playback_song = &song1;
	reading_song = &song2;

	if (sequencer_init() != 0) {
		warnx("problem initializing sequencer, exiting");
		return 1;
	}

	sequencer_init_songstate(playback_song, IDLE);
	sequencer_init_songstate(reading_song,  READING);

	for (;;) {
		if (main_socket == -1
		      && interp_fd == -1
		      && playback_song->playback_state != PLAYING) {
			retvalue = 0;
			goto finish;
		}

		FD_ZERO(&readfds);
		if (main_socket >= 0
		      && playback_song->playback_state != PLAYING)
			FD_SET(main_socket, &readfds);

		if (sequencer_reset_songstate(reading_song) && interp_fd >= 0)
			FD_SET(interp_fd, &readfds);

		if (playback_song->playback_state == PLAYING) {
			sequencer_calculate_timeout(playback_song, &timeout);
			timeout_p = &timeout;
		} else {
			timeout_p = NULL;
		}

		/* XXX do signal handling
		 * XXX and use self-pipe trick or pselect() */

		ret = select(FD_SETSIZE, &readfds, NULL, NULL, timeout_p);
		if (ret == -1) {
			warn("error in select");
			retvalue = 1;
			goto finish;
		}

		if (playback_song->playback_state == PLAYING)
			sequencer_play_music(playback_song);

		if (main_socket >= 0 && FD_ISSET(main_socket, &readfds)) {
			old_interp_fd = interp_fd;
			ret = receive_fd_through_socket(&interp_fd,
							main_socket);
			if (ret == -1) {
				warnx("error receiving pipe from interpreter" \
				        " to sequencer");
				retvalue = 1;
				goto finish;
			}
			if (ret == 0) {
				if (close(main_socket) == -1)
					warn("closing main socket");
				main_socket = -1;
			} else {
				assert(old_interp_fd != interp_fd);
				if (old_interp_fd >= 0
				      && close(old_interp_fd) == -1)
					warn("closing old interpreter fd");
				/* we have new interp_fd, back to select() */
				continue;
			}

		}

		if (interp_fd >= 0 && FD_ISSET(interp_fd, &readfds)) {
			nr = sequencer_read_to_eventstream(reading_song,
							   interp_fd);
			if (nr == -1) {
				retvalue = 1;
				goto finish;
			}
			if (nr == 0) {
				/* we have a new playback stream, great!
				 * reading_song becomes the playback song */
				tmp_song      = reading_song;
				reading_song  = playback_song;
				playback_song = tmp_song;

				ret = sequencer_start_playing(playback_song,
							      reading_song);
				if (ret != 0) {
					retvalue = 1;
					goto finish;
				}

				if (close(interp_fd) == -1)
					warn("closing interpreter fd");
				interp_fd = -1;
			}
		}
	}

finish:
	if (interp_fd >= 0 && close(interp_fd) == -1)
		warn("closing interpreter fd");

	sequencer_close_songstate(playback_song);

	sequencer_free_songstate(playback_song);
	sequencer_free_songstate(reading_song);

	sequencer_close();

	return retvalue;
}

static void
sequencer_init_songstate(struct songstate *ss, enum playback_state_t ps)
{
	SIMPLEQ_INIT(&ss->es);

	bzero(ss->notestates, sizeof(ss->notestates));

	ss->current_event.block = NULL;
	ss->current_event.index = 0;
	ss->latest_tempo_change_as_measures = 0;
	ss->latest_tempo_change_as_time.tv_sec = 0;
	ss->latest_tempo_change_as_time.tv_usec = 0;
	ss->measure_length = 4;
	ss->playback_state = ps;
	ss->tempo = 120;
	ss->time_as_measures = 0;
}

static void
sequencer_calculate_timeout(struct songstate *ss, struct timeval *timeout)
{
	struct timeval current_time, notetime;
	int ret;

	sequencer_time_for_next_note(ss, &notetime);

	ret = gettimeofday(&current_time, NULL);
	assert(ret == 0);

	timersub(&current_time, &notetime, timeout);
}

static void
sequencer_free_songstate(struct songstate *ss)
{
	struct eventblock *eb;

	while (!SIMPLEQ_EMPTY(&ss->es)) {
		eb = SIMPLEQ_FIRST(&ss->es);
		SIMPLEQ_REMOVE_HEAD(&ss->es, entries);
		free(eb);
	}
}

static int
sequencer_play_music(struct songstate *ss)
{
#if 0
	struct timeval current_time;
	struct eventblock *eb;
	struct midievent *ev;
	int eventindex;

	/* XXX ss->time_as_measures should be updated from current time?
	 * XXX (as well as ss->current_event.block and
	 * XXX ss->current_event.index) */

	SIMPLEQ_FOREACH(eb, &ss->es, entries) {
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
#endif

	return 0;
}

static int
sequencer_check_midievent(struct midievent me, float time_as_measures)
{
	if (!sequencer_check_range(me.eventtype, 0, EVENTTYPE_COUNT)) {
		warnx("midievent eventtype is invalid: %d", me.eventtype);
		return 0;
	}

	if (me.eventtype == SONG_END)
		return 1;

	if (!sequencer_check_range(me.channel, 0, SEQ_CHANNEL_MAX)) {
		warnx("midievent channel is invalid: %d", me.channel);
		return 0;
	}

	if (!sequencer_check_range(me.note, 0, SEQ_NOTE_MAX)) {
		warnx("midievent note is invalid: %d", me.note);
		return 0;
	}

	if (!sequencer_check_range(me.velocity, 0, SEQ_VELOCITY_MAX)) {
		warnx("midievent velocity is invalid: %d", me.velocity);
		return 0;
	}

	if (!isfinite(me.time_as_measures)) {
		warnx("time_as_measures is not a valid (finite) value");
		return 0;
	}

	if (me.time_as_measures < time_as_measures) {
		warnx("time is decreasing in eventstream");
		return 0;
	}

	return 1;
}

static int
sequencer_check_range(u_int8_t value, u_int8_t min, u_int8_t max)
{
	return (min <= value && value <= max);
}

static int
sequencer_noteevent(struct songstate *ss,
		    int note_on,
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
		ss->notestates[channel][note].state = note_on ? 1 : 0;
		ss->notestates[channel][note].velocity
		  = note_on ? velocity : 0;
	}

	return ret;
}

static int
sequencer_noteon(struct songstate *ss,
		 u_int8_t channel,
		 u_int8_t note,
		 u_int8_t velocity)
{
	return sequencer_noteevent(ss, 1, channel, note, velocity);
}

static int
sequencer_noteoff(struct songstate *ss, u_int8_t channel, u_int8_t note)
{
	return sequencer_noteevent(ss, 0, channel, note, 0);
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
sequencer_read_to_eventstream(struct songstate *ss, int fd)
{
	struct eventblock *eb;
	ssize_t nr;
	int i;

	assert(fd >= 0);
	assert(ss != NULL);

	eb = ss->current_event.block;

	if (eb == NULL || eb->readcount == sizeof(eb->events)) {
		eb = malloc(sizeof(struct eventblock));
		if (eb == NULL) {
			warn("malloc failure in" \
			       " sequencer_read_to_eventstream");
			return -1;
		}

		eb->readcount = 0;
		SIMPLEQ_INSERT_TAIL(&ss->es, eb, entries);
		ss->current_event.block = eb;
	}

	nr = read(fd,
		  (char *) eb->events + eb->readcount,
		  sizeof(eb->events) - eb->readcount);

	if (nr == -1) {
		warn("error in reading to eventstream");
		return nr;
	}

	if (nr == 0) {
		if (eb->readcount % sizeof(struct midievent) > 0) {
			warnx("received music stream which" \
				" is not complete (truncated event)");
			return -1;
		}

		i = eb->readcount / sizeof(struct midievent);
	        if (eb->events[i].eventtype != SONG_END) {
			warnx("received music stream which" \
				" is not complete (last event not SONG_END)");
			return -1;
		}

		return nr;
	}

	for (i = eb->readcount / sizeof(struct midievent);
	     i < (eb->readcount + nr) / sizeof(struct midievent);
	     i++) {
		if (!sequencer_check_midievent(eb->events[i],
					       ss->time_as_measures))
			return -1;
		ss->current_event.index = i;
		ss->time_as_measures = eb->events[i].time_as_measures;
	}

	eb->readcount += nr;

	return nr;
}

static int
sequencer_reset_songstate(struct songstate *ss)
{
	struct eventblock *eb;
	int i;

	assert(ss->playback_state == FREEING_EVENTSTREAM
		 || ss->playback_state == READING);

	if (ss->playback_state != FREEING_EVENTSTREAM)
		return 1;

	/* do not allow this to go on too long so that
	 * we do not miss our playback moment */
	i = 0;
	while (!SIMPLEQ_EMPTY(&ss->es) && i++ < 64) {
		eb = SIMPLEQ_FIRST(&ss->es);
		SIMPLEQ_REMOVE_HEAD(&ss->es, entries);
		free(eb);
	}

	/* if eventstream is empty, we can re-init and return true */
	if (SIMPLEQ_EMPTY(&ss->es)) {
		sequencer_init_songstate(ss, READING);
		return 1;
	}

	return 0;
}

static int
sequencer_start_playing(struct songstate *ss, struct songstate *old_ss)
{
	struct notestate old, new;
	struct eventpointer *ce;
	struct midievent *me;
	int c, n, ret;

	/* find the event where we should be at at new songstate,
	 * and do a "shadow playback" to determine what our midi state
	 * should be */
	ce = &ss->current_event;
	SIMPLEQ_FOREACH(ce->block, &ss->es, entries) {
		for (ce->index = 0; ce->index < EVENTBLOCKCOUNT; ce->index++) {
			me = &ce->block->events[ ce->index ];
			if (me->eventtype == SONG_END)
				break;
			if (me->time_as_measures >= old_ss->time_as_measures)
				break;

			switch (me->eventtype) {
			case NOTEON:
				ss->notestates[me->channel][me->note]
				    .state = 1;
				ss->notestates[me->channel][me->note]
				    .velocity = me->velocity;
			case NOTEOFF:
				ss->notestates[me->channel][me->note]
				    .state = 0;
				ss->notestates[me->channel][me->note]
				    .velocity = 0;
			default:
				assert(0);
			}
		}
	}

	/* sync playback state
	 *   (start or turn off notes according to new playback song) */
	for (c = 0; c < SEQ_CHANNEL_MAX; c++) {
		for (n = 0; n < SEQ_NOTE_MAX; n++) {
			old = old_ss->notestates[c][n];
 			new =     ss->notestates[c][n];

			if (old.state && new.state
			     && old.velocity != new.velocity) {
				/* note playing in different velocity,
				 * so play note again */
				ret = sequencer_noteoff(ss, c, n);
				if (ret != 0)
					return ret;
				ret = sequencer_noteon(ss, c, n,
						       new.velocity);
				if (ret != 0)
					return ret;
			} else if (old.state && !new.state) {
				/* note playing, but should no longer be */
				ret = sequencer_noteoff(ss, c, n);
				if (ret != 0)
					return ret;
			} else if (!old.state && new.state) {
				/* note not playing, but should be */
				ret = sequencer_noteon(ss, c, n,
						       new.velocity);
				if (ret != 0)
					return ret;
			}

			/* sequencer_{noteoff,noteon} should also have
			 * a side effect to make these true: */
			assert(
			  old_ss->notestates[c][n].state
			      == ss->notestates[c][n].state
                            && old_ss->notestates[c][n].velocity
			         == ss->notestates[c][n].velocity
			);
		}
	}

	ret = gettimeofday(&ss->latest_tempo_change_as_time, NULL);
	assert(ret == 0);

	    ss->playback_state = PLAYING;
	old_ss->playback_state = FREEING_EVENTSTREAM;

	return 0;
}

static void
sequencer_time_for_next_note(struct songstate *ss,
			     struct timeval *notetime)
{
	struct midievent next_midievent;
	struct timeval time_for_note_since_latest_tempo_change;
	float measures_for_note_since_latest_tempo_change,
	      time_for_note_since_latest_tempo_change_in_us;

	assert(ss != NULL);
	assert(ss->current_event.block != NULL);
	assert(0 <= ss->current_event.index
		 && ss->current_event.index < EVENTBLOCKCOUNT);
	assert(ss->latest_tempo_change_as_time.tv_sec > 0
		 || ss->latest_tempo_change_as_time.tv_usec > 0);
	assert(ss->playback_state == PLAYING);
	assert(ss->tempo > 0);

	next_midievent = ss->current_event.block->events[
			   ss->current_event.index
			 ];

	measures_for_note_since_latest_tempo_change
	    = next_midievent.time_as_measures
		- ss->latest_tempo_change_as_measures;

	time_for_note_since_latest_tempo_change_in_us
	    = (1000000.0 * ss->measure_length * (60.0 * 4 / ss->tempo))
                * measures_for_note_since_latest_tempo_change;

	time_for_note_since_latest_tempo_change.tv_sec
	    = time_for_note_since_latest_tempo_change_in_us / 1000000;
	time_for_note_since_latest_tempo_change.tv_usec
	    = fmodf(time_for_note_since_latest_tempo_change_in_us, 1000000);

	timeradd(&time_for_note_since_latest_tempo_change,
		 &ss->latest_tempo_change_as_time,
		 notetime);
}

static void
sequencer_close(void)
{
	if (mio) {
		mio_close(mio);
		mio = NULL;
	}
}

static void
sequencer_close_songstate(struct songstate *ss)
{
	int c, n;

	for (c = 0; c < SEQ_CHANNEL_MAX; c++)
		for (n = 0; n < SEQ_NOTE_MAX; n++)
			if (ss->notestates[c][n].state)
				if (sequencer_noteoff(ss, c, n) != 0)
					warnx("error in turning off note"
						" %d on channel %d", n, c);
}

static int
receive_fd_through_socket(int *received_fd, int socket)
{
	struct msghdr	msg;
	struct cmsghdr *cmsg;
	union {                           
		struct cmsghdr	hdr;
		unsigned char	buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

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

	if (cmsg == NULL)
		return 0;

	if (   cmsg->cmsg_len   == CMSG_LEN(sizeof(int))
            && cmsg->cmsg_level == SOL_SOCKET
            && cmsg->cmsg_type  == SCM_RIGHTS) {
		*received_fd = *(int *)CMSG_DATA(cmsg);
		return 1;
	}

	warnx("did not receive fd while expecting for it");
	return -1;
}
