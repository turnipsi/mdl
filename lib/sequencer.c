/* $Id: sequencer.c,v 1.45 2015/10/28 19:57:06 je Exp $ */

/*
 * Copyright (c) 2015 Juha Erkkil� <je@turnipsi.no-ip.org>
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
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "midi.h"
#include "sequencer.h"

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
	struct notestate notestates[MIDI_CHANNEL_MAX+1][MIDI_NOTE_MAX+1];
	struct eventstream es;
	struct eventpointer current_event;
	struct timeval latest_tempo_change_as_time;
	float latest_tempo_change_as_measures, tempo, time_as_measures;
	int got_song_end, measure_length;
	enum playback_state_t playback_state;
};

static struct mio_hdl  *mio = NULL;
static int		signal_pipe[2];


static void	sequencer_calculate_timeout(struct songstate *,
					    struct timeval *);
static void	sequencer_close(void);
static void	sequencer_close_songstate(struct songstate *);
static void	sequencer_free_songstate(struct songstate *);
static void	sequencer_handle_signal(int);
static int	sequencer_init(void);
static void	sequencer_init_songstate(struct songstate *,
					 enum playback_state_t);
static int	sequencer_noteevent(struct songstate *, struct midievent *);
static int	sequencer_play_music(struct songstate *);
static ssize_t	sequencer_read_to_eventstream(struct songstate *, int);
static int	sequencer_reset_songstate(struct songstate *);
static int	sequencer_start_playing(struct songstate *,
					struct songstate *);
static void	sequencer_time_for_next_note(struct songstate *ss,
					     struct timeval *notetime);

static int	receive_fd_through_socket(int *, int);

static int
sequencer_init(void)
{
	return midi_open_device();
}

static void
sequencer_handle_signal(int signo)
{
	int save_errno;

	save_errno = errno;
	(void) write(signal_pipe[0], "O", 1);
	errno = save_errno;
}

int
sequencer_loop(int main_socket)
{
	struct songstate song1, song2;
	struct songstate *playback_song, *reading_song, *tmp_song;
	fd_set readfds;
	int retvalue, interp_fd, old_interp_fd, ret, nr;
	struct timeval timeout, *timeout_p;

	/* XXX receiving fd works even if recvfd is not specified...
	 * XXX is this a bug? */
	if (pledge("rpath recvfd stdio unix wpath", NULL) != 0) {
		warn("pledge");
		return 1;
	}

	retvalue = 0;

	interp_fd = -1;
	playback_song = &song1;
	reading_song = &song2;

	if (sequencer_init() != 0) {
		warnx("problem initializing sequencer, exiting");
		return 1;
	}

	if (pledge("recvfd stdio", NULL) != 0) {
		warn("pledge");
		sequencer_close();
		return 1;
	}

	if (pipe(signal_pipe) == -1) {
		warn("opening signal-pipe for select");
		sequencer_close();
		return 1;
	}

	signal(SIGINT,  sequencer_handle_signal);
	signal(SIGTERM, sequencer_handle_signal);

	sequencer_init_songstate(playback_song, IDLE);
	sequencer_init_songstate(reading_song,  READING);

	for (;;) {
		assert(playback_song->playback_state == IDLE
			 || playback_song->playback_state == PLAYING);
		assert(reading_song->playback_state == READING
			 || reading_song->playback_state
			      == FREEING_EVENTSTREAM);

		FD_ZERO(&readfds);
		FD_SET(signal_pipe[1], &readfds);

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

		if (main_socket == -1
		      && interp_fd == -1
		      && timeout_p == NULL) {
			/* nothing more to do! */
			retvalue = 0;
			goto finish;
		}

		ret = select(FD_SETSIZE, &readfds, NULL, NULL, timeout_p);
		if (ret == -1 && errno != EINTR) {
			warn("error in select");
			retvalue = 1;
			goto finish;
		}

		if (FD_ISSET(signal_pipe[1], &readfds)) {
			retvalue = 1;
			goto finish;
		}

		if (playback_song->playback_state == PLAYING) {
			if (sequencer_play_music(playback_song) != 0) {
				warnx("error when playing music");
				retvalue = 1;
				goto finish;
			}
		}

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
	ss->got_song_end = 0;
	ss->latest_tempo_change_as_measures = 0;
	ss->latest_tempo_change_as_time.tv_sec = 0;
	ss->latest_tempo_change_as_time.tv_usec = 0;
	ss->measure_length = 1;
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

	timersub(&notetime, &current_time, timeout);

	if (timeout->tv_sec < 0) {
		timeout->tv_sec  = 0;
		timeout->tv_usec = 0;
	}
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
	struct eventpointer *ce;
	struct midievent *me;
	struct timeval time_to_play;
	int ret;

	ce = &ss->current_event;

	while (ce->block) {
		while (ce->index < EVENTBLOCKCOUNT) {
			me = &ce->block->events[ ce->index ];

			if (me->eventtype == SONG_END) {
				ss->playback_state = IDLE;
				return 0;
			}

			sequencer_calculate_timeout(ss, &time_to_play);

			/* if timeout has not been gone to zero, 
			 * it is not our time to play yet */
			if (time_to_play.tv_sec > 0
			      || (time_to_play.tv_sec == 0
				   && time_to_play.tv_usec > 0))
				return 0;

			switch(me->eventtype) {
			case NOTEOFF:
			case NOTEON:
				if ((ret = sequencer_noteevent(ss, me)) != 0)
					return ret;
				break;
			default:
				assert(0);
			}

			ce->index++;
		}

		ce->block = SIMPLEQ_NEXT(ce->block, entries);
	}

	return 0;
}

static int
sequencer_noteevent(struct songstate *ss, struct midievent *me)
{
	int ret;

	ret = midi_send_midievent(me);
	if (ret == 0) {
		ss->notestates[me->channel][me->note].state
		  = (me->eventtype == NOTEON) ? 1 : 0;
		ss->notestates[me->channel][me->note].velocity
		  = (me->eventtype == NOTEON) ? me->velocity : 0;
	}

	return ret;
}

static ssize_t
sequencer_read_to_eventstream(struct songstate *ss, int fd)
{
	struct eventblock *cur_b, *new_b;
	ssize_t nr;
	int retvalue, i;

	assert(fd >= 0);
	assert(ss != NULL);

	new_b = cur_b = ss->current_event.block;

	if (cur_b == NULL || cur_b->readcount == sizeof(cur_b->events)) {
		if ((new_b = malloc(sizeof(struct eventblock))) == NULL) {
			warn("malloc failure in" \
			       " sequencer_read_to_eventstream");
			return -1;
		}

		new_b->readcount = 0;
	}

	nr = read(fd,
		  (char *) new_b->events + new_b->readcount,
		  sizeof(new_b->events) - new_b->readcount);

	if (nr == -1) {
		warn("error in reading to eventstream");
		retvalue = -1;
		goto finish;
	}

	if (nr == 0) {
		/* the last event must be SONG_END */
		if (!ss->got_song_end) {
			warnx("received music stream which" \
				" is not complete (last event not SONG_END)");
			retvalue = -1;
			goto finish;
		}

		retvalue = 0;
		goto finish;
	}

	for (i = new_b->readcount / sizeof(struct midievent);
	     i < (new_b->readcount + nr) / sizeof(struct midievent);
	     i++) {
		/* song end must not come again */
		if (ss->got_song_end) {
			warnx("received music events after song end");
			retvalue = -1;
			goto finish;
		}

		if (new_b->events[i].eventtype == SONG_END)
			ss->got_song_end = 1;

		if (!midi_check_midievent(new_b->events[i],
					  ss->time_as_measures)) {
			retvalue = -1;
			goto finish;
		}

		ss->current_event.index = i;
		ss->time_as_measures = new_b->events[i].time_as_measures;
	}

	new_b->readcount += nr;

finish:
	if (new_b != cur_b) {
		if (new_b->readcount == 0) {
			free(new_b);
		} else {
			SIMPLEQ_INSERT_TAIL(&ss->es, new_b, entries);
			ss->current_event.block = new_b;
		}
	}

	return retvalue;
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
	struct eventpointer ce;
	struct midievent note_off, note_on, *me;
	int c, n, ret;

	/* find the event where we should be at at new songstate,
	 * and do a "shadow playback" to determine what our midi state
	 * should be */
	ce = ss->current_event;
	SIMPLEQ_FOREACH(ce.block, &ss->es, entries) {
		for (ce.index = 0; ce.index < EVENTBLOCKCOUNT; ce.index++) {
			me = &ce.block->events[ ce.index ];
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
				break;
			case NOTEOFF:
				ss->notestates[me->channel][me->note]
				    .state = 0;
				ss->notestates[me->channel][me->note]
				    .velocity = 0;
				break;
			default:
				assert(0);
			}
		}
		ss->current_event.block = ce.block;
		ss->current_event.index = ce.index;
	}

	/* sync playback state
	 *   (start or turn off notes according to new playback song) */
	for (c = 0; c < MIDI_CHANNEL_MAX; c++) {
		for (n = 0; n < MIDI_NOTE_MAX; n++) {
			old = old_ss->notestates[c][n];
 			new =     ss->notestates[c][n];

			note_off.eventtype = NOTEOFF;
			note_off.channel   = c;
			note_off.note      = n;
			note_off.velocity  = 0;

			note_on.eventtype = NOTEON;
			note_on.channel   = c;
			note_on.note      = n;
			note_on.velocity  = new.velocity;

			if (old.state && new.state
			     && old.velocity != new.velocity) {
				/* note playing in different velocity,
				 * so play note again */
				ret = sequencer_noteevent(ss, &note_off);
				if (ret != 0)
					return ret;
				ret = sequencer_noteevent(ss, &note_on);
				if (ret != 0)
					return ret;
			} else if (old.state && !new.state) {
				/* note playing, but should no longer be */
				ret = sequencer_noteevent(ss, &note_off);
				if (ret != 0)
					return ret;
			} else if (!old.state && new.state) {
				/* note not playing, but should be */
				ret = sequencer_noteevent(ss, &note_on);
				if (ret != 0)
					return ret;
			}

			/* sequencer_noteevent() should also have
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
	midi_close_device();
}

static void
sequencer_close_songstate(struct songstate *ss)
{
	struct midievent note_off;
	int c, n;

	for (c = 0; c < MIDI_CHANNEL_MAX; c++)
		for (n = 0; n < MIDI_NOTE_MAX; n++)
			if (ss->notestates[c][n].state) {
				note_off.eventtype = NOTEOFF;
				note_off.channel   = c;
				note_off.note      = n;
				note_off.velocity  = 0;
				if (sequencer_noteevent(ss, &note_off) != 0)
					warnx("error in turning off note"
						" %d on channel %d", n, c);
			}
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
