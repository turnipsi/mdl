/* $Id: sequencer.c,v 1.129 2016/08/13 18:20:59 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkil� <je@turnipsi.no-ip.org>
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
#include <sys/types.h>
#include <sys/uio.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <imsg.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ipc.h"
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

struct channel_state {
	struct notestate notestates[MIDI_NOTE_COUNT];
	u_int8_t instrument;
};

enum playback_state { IDLE, READING, PLAYING, FREEING_EVENTSTREAM, };

struct songstate {
	struct channel_state channelstates[MIDI_CHANNEL_COUNT];
	struct eventstream es;
	struct eventpointer current_event;
	struct timespec latest_tempo_change_as_time;
	float latest_tempo_change_as_measures, tempo, time_as_measures;
	int got_song_end, keep_position_when_switched_to, measure_length;
	enum playback_state playback_state;
};

struct sequencer {
	int			dry_run;
	int			interp_fd;
	int			client_socket;
	int			server_socket;
	struct songstate	song1;
	struct songstate	song2;
	struct songstate       *playback_song;
	struct songstate       *reading_song;
	struct imsgbuf		client_ibuf;
	struct imsgbuf		server_ibuf;
};

extern char *_mdl_process_type;

/* If this is set in signal handler, we should shut down. */
volatile sig_atomic_t	_mdl_shutdown_sequencer = 0;

static int	sequencer_loop(struct sequencer *);
static int	sequencer_accept_client_socket(struct sequencer *, int);
static int	sequencer_accept_interp_fd(struct sequencer *, int);
static void	sequencer_calculate_timeout(const struct sequencer *,
    struct songstate *, struct timespec *);
static void	sequencer_close(struct sequencer *);
static void	sequencer_close_songstate(const struct sequencer *,
    struct songstate *);
static void	sequencer_free_songstate(struct songstate *);
static int	sequencer_handle_client_events(struct sequencer *);
static int	sequencer_handle_server_events(struct sequencer *);
static void	sequencer_handle_signal(int);
static int	sequencer_init(struct sequencer *, int, int, enum mididev_type,
    const char *);
static void	sequencer_init_songstate(const struct sequencer *,
    struct songstate *, enum playback_state);
static int	sequencer_noteevent(const struct sequencer *,
    struct songstate *, struct midievent *);
static int	sequencer_play_music(struct sequencer *,
    struct songstate *);
static ssize_t	sequencer_read_to_eventstream(struct songstate *, int);
static int	sequencer_reset_songstate(struct sequencer *,
    struct songstate *);
static int	sequencer_start_playing(const struct sequencer *,
    struct songstate *, struct songstate *);
static int	sequencer_switch_songs(struct sequencer *);
static void	sequencer_time_for_next_note(struct songstate *ss,
    struct timespec *notetime);
static const char *ss_label(const struct sequencer *, struct songstate *);

static int
sequencer_init(struct sequencer *seq, int dry_run, int server_socket,
    enum mididev_type mididev_type, const char *devicepath)
{
	sigset_t loop_sigmask;

	signal(SIGINT,  sequencer_handle_signal);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGTERM, sequencer_handle_signal);

	if (sigemptyset(&loop_sigmask) == -1 ||
	    sigaddset(&loop_sigmask, SIGINT) == -1 ||
	    sigaddset(&loop_sigmask, SIGTERM) == -1 ||
	    sigprocmask(SIG_BLOCK, &loop_sigmask, NULL) == -1) {
		warn("error setting up sequencer signal handling");
		return 1;
	}

	seq->client_socket = -1;
	seq->dry_run = dry_run;
	seq->interp_fd = -1;
	seq->server_socket = server_socket;

	if (fcntl(seq->server_socket, F_SETFL, O_NONBLOCK) == -1) {
		warn("could not set server_socket non-blocking");
		return 1;
	}

	if (!seq->dry_run) {
		if (_mdl_midi_open_device(mididev_type, devicepath) != 0)
			return 1;
	}

	seq->reading_song = &seq->song1;
	seq->playback_song = &seq->song2;

	sequencer_init_songstate(seq, seq->reading_song, READING);
	sequencer_init_songstate(seq, seq->playback_song, IDLE);

	imsg_init(&seq->server_ibuf, seq->server_socket);

	return 0;
}

static void
sequencer_handle_signal(int signo)
{
	assert(signo == SIGINT || signo == SIGTERM);

	if (signo == SIGINT || signo == SIGTERM)
		_mdl_shutdown_sequencer = 1;
}

int
_mdl_start_sequencer_process(pid_t *sequencer_pid,
    struct sequencer_connection *seq_conn, enum mididev_type mididev_type,
    const char *devicepath, int dry_run)
{
	struct sequencer seq;
	int ss_sp[2];	/* client-sequencer socketpair */
	int sequencer_retvalue, ret;
	pid_t tmp_sequencer_pid;

	/* Setup socketpair for server <-> sequencer communication. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ss_sp) == -1) {
		warn("could not setup socketpair for server <-> sequencer");
		return 1;
	}

	if (fflush(NULL) == EOF)
		warn("error flushing streams before sequencer fork");

	/* Fork the midi sequencer process. */
	if ((tmp_sequencer_pid = fork()) == -1) {
		warn("could not fork sequencer process");
		if (close(ss_sp[0]) == -1)
			warn("error closing first end of ss_sp");
		if (close(ss_sp[1]) == -1)
			warn("error closing second end of ss_sp");
		return 1;
	}

	if (tmp_sequencer_pid == 0) {
		/*
		 * We are in the sequencer process.
		 */

		/*
		 * XXX Receiving fd works even if recvfd is not specified...
		 * XXX is this a bug? (can this be confirmed?)
		 * XXX Should read sio_open(3) section "Use with pledge(2)"...
		 * XXX (even though we use mio_open(3)).
		 */
		if (pledge("rpath recvfd stdio unix wpath", NULL) == -1) {
			warn("pledge");
			_exit(1);
		}

		setproctitle("sequencer");

		_mdl_logging_clear();
		_mdl_process_type = "seq";
		_mdl_log(MDLLOG_PROCESS, 0, "new sequencer process, pid %d\n",
		    getpid());
		/*
		 * XXX We should close all file descriptors that sequencer
		 * XXX does not need... does this do that?
		 */
		if (close(ss_sp[0]) == -1)
			warn("error closing first end of ss_sp");

		ret = sequencer_init(&seq, dry_run, ss_sp[1], mididev_type,
		    devicepath);
		if (ret != 0) {
			warnx("problem initializing sequencer");
			sequencer_retvalue = 1;
			goto finish;
		}

		if (pledge("recvfd stdio", NULL) == -1) {
			warn("pledge");
			_exit(1);
		}

		sequencer_retvalue = sequencer_loop(&seq);
	finish:
		if (pledge("stdio", NULL) == -1) {
			warn("pledge");
			_exit(1);
		}

		if (close(ss_sp[1]) == -1)
			warn("closing server socket");
		if (fflush(NULL) == EOF) {
			warn("error flushing streams in sequencer before"
			       " exit");
		}
		_mdl_logging_close();
		_exit(sequencer_retvalue);
	}

	if (close(ss_sp[1]) == -1)
		warn("error closing second end of ss_sp");

	*sequencer_pid = tmp_sequencer_pid;

	seq_conn->pending_writes = 0;
	seq_conn->socket = ss_sp[0];

	imsg_init(&seq_conn->ibuf, seq_conn->socket);

	return 0;
}

int
_mdl_disconnect_sequencer_connection(struct sequencer_connection *seq_conn)
{
	int retvalue;

	retvalue = 0;

	if (imsg_flush(&seq_conn->ibuf) == -1) {
		warnx("error flushing imsg buffers to sequencer");
		retvalue = 1;
	}

	imsg_clear(&seq_conn->ibuf);
	if (close(seq_conn->socket) == -1)
		warn("error closing sequencer socket connection");

	return retvalue;
}

int
_mdl_disconnect_sequencer_process(pid_t sequencer_pid,
    struct sequencer_connection *seq_conn)
{
	assert(sequencer_pid > 0);
	assert(seq_conn != NULL);

	if (_mdl_disconnect_sequencer_connection(seq_conn) != 0)
		warnx("error in disconnecting sequencer connection");

	if (_mdl_wait_for_subprocess("sequencer", sequencer_pid) != 0) {
		warnx("error when waiting for sequencer subprocess");
		return 1;
	}

	return 0;
}

static int
sequencer_loop(struct sequencer *seq)
{
	fd_set readfds;
	int retvalue, ret, nr;
	struct timespec timeout, *timeout_p;
	sigset_t select_sigmask;

	retvalue = 0;

	_mdl_log(MDLLOG_SEQ, 0, "starting sequencer loop\n");

	(void) sigemptyset(&select_sigmask);

	while (!_mdl_shutdown_sequencer) {
		assert(seq->playback_song->playback_state == IDLE ||
		    seq->playback_song->playback_state == PLAYING);
		assert(seq->reading_song->playback_state == READING ||
		    seq->reading_song->playback_state == FREEING_EVENTSTREAM);

		_mdl_log(MDLLOG_SEQ, 0, "new sequencer loop iteration\n");

		if (seq->client_socket >= 0) {
			if (msgbuf_write(&seq->client_ibuf.w) == -1) {
				if (errno != EAGAIN) {
					warnx("msgbuf_write error");
					goto finish;
				}
				/* XXX set max timeout to 100ms or some such */
			}
		}

		FD_ZERO(&readfds);
		if (seq->client_socket >= 0)
			FD_SET(seq->client_socket, &readfds);
		if (seq->server_socket >= 0)
			FD_SET(seq->server_socket, &readfds);

		ret = sequencer_reset_songstate(seq, seq->reading_song);
		if (ret && seq->interp_fd >= 0)
			FD_SET(seq->interp_fd, &readfds);

		if (seq->playback_song->playback_state == PLAYING) {
			sequencer_calculate_timeout(seq, seq->playback_song,
			    &timeout);
			timeout_p = &timeout;
		} else {
			timeout_p = NULL;
		}

		if (seq->client_socket == -1 && seq->interp_fd == -1 &&
		    seq->server_socket == -1 && timeout_p == NULL) {
			_mdl_log(MDLLOG_SEQ, 0,
			    "nothing more to do, exiting sequencer loop\n");
			retvalue = 0;
			goto finish;
		}

		ret = pselect(FD_SETSIZE, &readfds, NULL, NULL, timeout_p,
		    &select_sigmask);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			warn("error in pselect");
			retvalue = 1;
			goto finish;
		}

		if (seq->playback_song->playback_state == PLAYING) {
			ret = sequencer_play_music(seq, seq->playback_song);
			if (ret != 0) {
				warnx("error when playing music");
				retvalue = 1;
				goto finish;
			}
		}

		if (seq->server_socket >= 0 &&
		    FD_ISSET(seq->server_socket, &readfds)) {
			/*
			 * sequencer_handle_server_events() may close and set
			 * seq->client_socket to a new value.  It may also
			 * set seq->server_socket to -1 (disabling it).
			 */
			if (sequencer_handle_server_events(seq) != 0) {
				retvalue = 1;
				goto finish;
			}
			/*
			 * Restart loop because seq->client_socket might have
			 * changed.
			 */
			continue;
		}

		if (seq->client_socket >= 0 &&
		    FD_ISSET(seq->client_socket, &readfds)) {
			/*
			 * sequencer_handle_client_events() may close
			 * seq->client_socket and/or seq->interp_fd and set
			 * them to a new value.
			 */
			if (sequencer_handle_client_events(seq) != 0) {
				retvalue = 1;
				goto finish;
			}
			/*
			 * Restart loop because seq->client_socket and/or
			 * seq->interp_fd might have changed.
			 */
			continue;
		}

		if (seq->interp_fd >= 0 &&
		    FD_ISSET(seq->interp_fd, &readfds)) {
			_mdl_log(MDLLOG_SEQ, 0,
			    "reading eventstream to songstate %s\n",
			    ss_label(seq, seq->reading_song));

			nr = sequencer_read_to_eventstream(seq->reading_song,
			    seq->interp_fd);
			if (nr == -1) {
				retvalue = 1;
				goto finish;
			}
			if (nr == 0) {
				/*
				 * We have a new playback stream, great!
				 * reading_song becomes the playback song.
				 */
				if (sequencer_switch_songs(seq) != 0) {
					retvalue = 1;
					goto finish;
				}
				if (close(seq->interp_fd) == -1)
					warn("closing interpreter pipe");
				seq->interp_fd = -1;
			}
		}
	}

	if (_mdl_shutdown_sequencer) {
		_mdl_log(MDLLOG_PROCESS, 0,
		    "sequencer received shutdown signal\n");
		retvalue = 1;
	}

finish:
	sequencer_close(seq);

	return retvalue;
}

static void
sequencer_init_songstate(const struct sequencer *seq, struct songstate *ss,
    enum playback_state ps)
{
	static const char *strings[] = {
		"idle",			/* IDLE */
		"reading",		/* READING */
		"playing",		/* PLAYING */
		"freeing eventstream",	/* FREEING_EVENTSTREAM */
	};

	assert(ps == IDLE || ps == READING);

	SIMPLEQ_INIT(&ss->es);

	_mdl_log(MDLLOG_SEQ, 0,
	    "initializing a new songstate %s to state \"%s\"\n",
	   ss_label(seq, ss), strings[ps]);

	/*
	 * This memset() sets the default instrument for all channels to 0
	 * ("acoustic grand"), and all notes are off with velocity 0.
	 */
	memset(ss->channelstates, 0, sizeof(ss->channelstates));

	ss->current_event.block = NULL;
	ss->current_event.index = 0;
	ss->got_song_end = 0;
	ss->keep_position_when_switched_to = 0;
	ss->latest_tempo_change_as_measures = 0;
	ss->latest_tempo_change_as_time.tv_sec = 0;
	ss->latest_tempo_change_as_time.tv_nsec = 0;
	ss->measure_length = 1;
	ss->playback_state = ps;
	ss->tempo = 120;
	ss->time_as_measures = 0.0;
}

static int
sequencer_accept_client_socket(struct sequencer *seq, int new_fd)
{
	if (new_fd == -1) {
		warnx("did not receive a client socket when expecting it");
		return 1;
	}

	assert(seq->client_socket != new_fd);

	_mdl_log(MDLLOG_SEQ, 0, "received new client socket\n");

	/* We have new client socket, make it non-blocking. */
	if (fcntl(new_fd, F_SETFL, O_NONBLOCK) == -1) {
		warn("could not set new client socket non-blocking,"
		    " not accepting it");
		if (close(new_fd) == -1)
			warn("closing new client socket");
		return 1;
	}

	if (seq->client_socket >= 0) {
		/* XXX Note this may block the sequencer process. */
		if (imsg_flush(&seq->client_ibuf) == -1)
			warnx("error when flushing client socket buffers");
		imsg_clear(&seq->client_ibuf);
		if (close(seq->client_socket) == -1)
			warn("closing old client socket");
	}

	seq->client_socket = new_fd;
	imsg_init(&seq->client_ibuf, seq->client_socket);

	return 0;
}

static int
sequencer_accept_interp_fd(struct sequencer *seq, int new_fd)
{
	if (new_fd == -1) {
		warnx("did not receive an interpreter pipe when expecting it");
		return 1;
	}

	assert(seq->interp_fd != new_fd);

	_mdl_log(MDLLOG_SEQ, 0, "received new interpreter pipe\n");

	/* We have new interpreter pipe, make it non-blocking. */
	if (fcntl(new_fd, F_SETFL, O_NONBLOCK) == -1) {
		warn("could not set new interpreter pipe non-blocking,"
		    " not accepting it");
		if (close(new_fd) == -1)
			warn("closing new interpreter pipe");
		return 1;
	}

	if (seq->interp_fd >= 0 && close(seq->interp_fd) == -1)
		warn("closing old interpreter pipe");

	seq->interp_fd = new_fd;

	return 0;
}

static void
sequencer_calculate_timeout(const struct sequencer *seq, struct songstate *ss,
    struct timespec *timeout)
{
	struct timespec current_time, notetime;
	int ret;

	if (seq->dry_run) {
		timeout->tv_sec = 0;
		timeout->tv_nsec = 0;
		return;
	}

	sequencer_time_for_next_note(ss, &notetime);

	ret = clock_gettime(CLOCK_UPTIME, &current_time);
	assert(ret == 0);

	timeout->tv_sec  = notetime.tv_sec  - current_time.tv_sec;
	timeout->tv_nsec = notetime.tv_nsec - current_time.tv_nsec;

	if (timeout->tv_nsec < 0) {
		timeout->tv_nsec += 1000000000;
		timeout->tv_sec -= 1;
	}

	if (timeout->tv_sec < 0) {
		timeout->tv_sec = 0;
		timeout->tv_nsec = 0;
	} else {
		_mdl_log(MDLLOG_SEQ, 0,
		    "next sequencer timeout: %ld.%ld\n", timeout->tv_sec,
		    timeout->tv_nsec);
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
sequencer_handle_client_events(struct sequencer *seq)
{
	struct imsg imsg;
	enum mdl_event event;
	ssize_t nr;
	int ret, retvalue;

	nr = imsg_read(&seq->client_ibuf);
	if (nr == -1) {
		warnx("error in reading event from client / imsg_read");
		return 1;
	}

	if (nr == 0) {
		/* Client process has shutdown/closed the client_socket. */
		_mdl_log(MDLLOG_SEQ, 0,
		    "client socket has been shutdown by the client process\n");
		if (close(seq->client_socket) == -1)
			warnx("closing client socket");
		seq->client_socket = -1;
		return 0;
	}

	retvalue = 0;

	while (retvalue == 0) {
		nr = imsg_get(&seq->client_ibuf, &imsg);
		if (nr == -1) {
			warnx("error in reading event from client / imsg_get");
			retvalue = 1;
			break;
		}
		if (nr == 0)
			break;

		event = imsg.hdr.type;
		switch (event) {
		case CLIENTEVENT_NEW_MUSICFD:
			warnx("sequencer received new music file descriptor,"
			    " this should not happen");
			retvalue = 1;
			break;
		case CLIENTEVENT_NEW_SONG:
			ret = sequencer_accept_interp_fd(seq, imsg.fd);
			if (ret != 0)
				retvalue = 1;
			break;
		case CLIENTEVENT_REPLACE_SONG:
			ret = sequencer_accept_interp_fd(seq, imsg.fd);
			if (ret != 0) {
				retvalue = 1;
				break;
			}
			seq->reading_song->keep_position_when_switched_to = 1;
			break;
		case SEQEVENT_SONG_END:
			warnx("received a sequencer event from client");
			retvalue = 1;
			break;
		case SERVEREVENT_NEW_CLIENT:
		case SERVEREVENT_NEW_INTERPRETER:
			warnx("received a server event from client");
			retvalue = 1;
			break;
		default:
			warnx("unknown event received from client");
			retvalue = 1;
		}

		imsg_free(&imsg);
	}

	return retvalue;
}

static int
sequencer_handle_server_events(struct sequencer *seq)
{
	struct imsg imsg;
	enum mdl_event event;
	ssize_t nr;
	int ret, retvalue;

	nr = imsg_read(&seq->server_ibuf);
	if (nr == -1) {
		warnx("error in reading event from server / imsg_read");
		return 1;
	}

	if (nr == 0) {
		/* Server process has shutdown/closed the server_socket. */
		_mdl_log(MDLLOG_SEQ, 0,
		    "server socket has been shutdown by the server process\n");
		/*
		 * Do not close the server socket, just flush imsg buffers
		 * and mark it disabled.  It is not our responsibility to
		 * close it.
		 * XXX Note this may block the sequencer process.
		 */
		if (imsg_flush(&seq->server_ibuf) == -1)
			warnx("error flushing imsg buffers to server");
		imsg_clear(&seq->server_ibuf);
		seq->server_socket = -1;
		return 0;
	}

	retvalue = 0;

	while (retvalue == 0) {
		nr = imsg_get(&seq->server_ibuf, &imsg);
		if (nr == -1) {
			warnx("error in reading event from server / imsg_get");
			retvalue = 1;
			break;
		}
		if (nr == 0)
			break;

		event = imsg.hdr.type;
		switch (event) {
		case CLIENTEVENT_NEW_MUSICFD:
		case CLIENTEVENT_NEW_SONG:
		case CLIENTEVENT_REPLACE_SONG:
			warnx("received a client event from server");
			retvalue = 1;
			break;
		case SEQEVENT_SONG_END:
			warnx("received a sequencer event from server");
			retvalue = 1;
			break;
		case SERVEREVENT_NEW_CLIENT:
			ret = sequencer_accept_client_socket(seq, imsg.fd);
			if (ret != 0) {
				warnx("error in accepting new client socket");
				retvalue = 1;
			}
			break;
		case SERVEREVENT_NEW_INTERPRETER:
			warnx("sequencer received new interpreter event from"
			    " server, this should not happen");
			retvalue = 1;
			break;
		default:
			warnx("unknown event received from server");
			retvalue = 1;
		}

		imsg_free(&imsg);
	}

	return retvalue;
}

static int
sequencer_play_music(struct sequencer *seq, struct songstate *ss)
{
	struct eventpointer *ce;
	struct midievent *me;
	struct timespec time_to_play;
	int ret;

	ce = &ss->current_event;

	while (ce->block) {
		while (ce->index < EVENTBLOCKCOUNT) {
			me = &ce->block->events[ ce->index ];

			if (me->evtype == MIDIEV_SONG_END) {
				ss->playback_state = IDLE;

				if (seq->client_socket >= 0) {
					ret = imsg_compose(&seq->client_ibuf,
					    SEQEVENT_SONG_END, 0, 0, -1, "",
					    0);
					if (ret == -1) {
						warnx("error sending"
						    " SEQEVENT_SONG_END");
						return 1;
					}
				}

				return 0;
			}

			sequencer_calculate_timeout(seq, ss, &time_to_play);

			/*
			 * If timeout has not been gone to zero,
			 * it is not our time to play yet.
			 */
			if (time_to_play.tv_sec > 0 ||
			    (time_to_play.tv_sec == 0 &&
			    time_to_play.tv_nsec > 0))
				return 0;

			switch(me->evtype) {
			case MIDIEV_INSTRUMENT_CHANGE:
			case MIDIEV_NOTEOFF:
			case MIDIEV_NOTEON:
				ret = sequencer_noteevent(seq, ss, me);
				if (ret != 0)
					return ret;
				break;
			case MIDIEV_SONG_END:
				/* This has been handled above. */
				assert(0);
				break;
			case MIDIEV_TEMPOCHANGE:
				/* XXX */
				assert(0);
				break;
			default:
				assert(0);
			}

			ce->index += 1;
		}

		ce->block = SIMPLEQ_NEXT(ce->block, entries);
	}

	return 0;
}

static int
sequencer_noteevent(const struct sequencer *seq, struct songstate *ss,
    struct midievent *me)
{
	int ret;
	struct notestate *nstate;

	ret = _mdl_midi_send_midievent(me, seq->dry_run);
	if (ret == 0) {
		switch (me->evtype) {
		case MIDIEV_INSTRUMENT_CHANGE:
			ss->channelstates[me->u.instr_change.channel]
			    .instrument = me->u.instr_change.code;
			break;
		case MIDIEV_NOTEOFF:
			nstate = &ss->channelstates[me->u.note.channel]
			    .notestates[me->u.note.note];
			nstate->state = 0;
			nstate->velocity = 0;
			break;
		case MIDIEV_NOTEON:
			nstate = &ss->channelstates[me->u.note.channel]
			    .notestates[me->u.note.note];
			nstate->state = 1;
			nstate->velocity = me->u.note.velocity;
			break;
		case MIDIEV_SONG_END:
		case MIDIEV_TEMPOCHANGE:
			/* These events should not have come this far. */
			assert(0);
			break;
		default:
			assert(0);
		}

	}

	return ret;
}

static ssize_t
sequencer_read_to_eventstream(struct songstate *ss, int fd)
{
	struct eventblock *cur_b, *new_b;
	ssize_t nr;
	size_t i;

	assert(fd >= 0);
	assert(ss != NULL);

	new_b = cur_b = ss->current_event.block;

	if (cur_b == NULL || cur_b->readcount == sizeof(cur_b->events)) {
		if ((new_b = malloc(sizeof(struct eventblock))) == NULL) {
			warn("malloc failure in"
			    " sequencer_read_to_eventstream");
			return -1;
		}

		new_b->readcount = 0;
	}

	nr = read(fd, ((char *) new_b->events + new_b->readcount),
	    (sizeof(new_b->events) - new_b->readcount));

	if (nr == -1) {
		warn("error in reading to eventstream");
		goto finish;
	}

	if (nr == 0) {
		/* The last event must be SONG_END. */
		if (!ss->got_song_end) {
			warnx("received music stream which is not complete"
			    " (last event not SONG_END)");
			nr = -1;
			goto finish;
		}

		goto finish;
	}

	for (i = new_b->readcount / sizeof(struct midievent);
	    i < (new_b->readcount + nr) / sizeof(struct midievent);
	    i++) {
		/* The song end must not come again. */
		if (ss->got_song_end) {
			warnx("received music events after song end");
			nr = -1;
			goto finish;
		}

		if (new_b->events[i].evtype == MIDIEV_SONG_END)
			ss->got_song_end = 1;

		_mdl_midievent_log(MDLLOG_MIDI, "received", &new_b->events[i],
		    0);

		if (!_mdl_midi_check_midievent(new_b->events[i],
		    ss->time_as_measures)) {
			nr = -1;
			goto finish;
		}

		ss->current_event.index = i;
		ss->time_as_measures = new_b->events[i].time_as_measures;
	}

	if (nr > 0)
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

	return nr;
}

static int
sequencer_reset_songstate(struct sequencer *seq, struct songstate *ss)
{
	struct eventblock *eb;
	int i;

	assert(ss->playback_state == FREEING_EVENTSTREAM ||
	    ss->playback_state == READING);

	if (ss->playback_state != FREEING_EVENTSTREAM)
		return 1;


	_mdl_log(MDLLOG_SEQ, 0,
	    "freeing the old eventstream for songstate %s\n",
	    ss_label(seq, ss));

	/*
	 * Do not allow this to go on too long so that
	 * we do not miss our playback moment.
	 */
	i = 0;
	while (!SIMPLEQ_EMPTY(&ss->es) && i++ < 64) {
		eb = SIMPLEQ_FIRST(&ss->es);
		SIMPLEQ_REMOVE_HEAD(&ss->es, entries);
		free(eb);
	}

	/* If eventstream is empty, we can re-init and return true. */
	if (SIMPLEQ_EMPTY(&ss->es)) {
		_mdl_log(MDLLOG_SEQ, 0,
		    "old eventstream freed for songstate %s\n",
		    ss_label(seq, ss));
		sequencer_init_songstate(seq, ss, READING);
		return 1;
	} else {
		_mdl_log(MDLLOG_SEQ, 0,
		    "songstate %s still has eventstream to be freed\n",
		     ss_label(seq, ss));
	}

	return 0;
}

static int
sequencer_start_playing(const struct sequencer *seq, struct songstate *new_ss,
    struct songstate *old_ss)
{
	struct notestate old, new;
	struct eventpointer ce;
	struct midievent change_instrument, note_off, note_on, *me;
	int instr_changed, retrigger_note, c, n, ret;

	/*
	 * Find the event where we should be at at new songstate,
	 * and do a "shadow playback" to determine what our midi state
	 * should be.
	 */
	ce = new_ss->current_event;
	SIMPLEQ_FOREACH(ce.block, &new_ss->es, entries) {
		for (ce.index = 0; ce.index < EVENTBLOCKCOUNT; ce.index++) {
			me = &ce.block->events[ ce.index ];
			if (me->evtype == MIDIEV_SONG_END)
				break;
			if (me->time_as_measures >= new_ss->time_as_measures)
				break;

			switch (me->evtype) {
			case MIDIEV_INSTRUMENT_CHANGE:
				new_ss->channelstates
				    [me->u.instr_change.channel]
				    .instrument = me->u.instr_change.code;
				break;
			case MIDIEV_NOTEON:
				new_ss->channelstates[me->u.note.channel]
				    .notestates[me->u.note.note].state = 1;
				new_ss->channelstates[me->u.note.channel]
				    .notestates[me->u.note.note].velocity =
				    me->u.note.velocity;
				break;
			case MIDIEV_NOTEOFF:
				new_ss->channelstates[me->u.note.channel]
				    .notestates[me->u.note.note].state = 0;
				new_ss->channelstates[me->u.note.channel]
				    .notestates[me->u.note.note].velocity = 0;
				break;
			case MIDIEV_SONG_END:
				/* This has been handled above. */
				assert(0);
				break;
			case MIDIEV_TEMPOCHANGE:
				/* XXX */
				assert(0);
				break;
			default:
				assert(0);
			}
		}
		new_ss->current_event.block = ce.block;
		new_ss->current_event.index = ce.index;
	}

	/*
	 * Sync playback state
	 *   (start or turn off notes according to new playback song).
	 */
	for (c = 0; c < MIDI_CHANNEL_COUNT; c++) {
		instr_changed = (old_ss->channelstates[c].instrument !=
		    new_ss->channelstates[c].instrument);

		if (instr_changed) {
			change_instrument.evtype = MIDIEV_INSTRUMENT_CHANGE;
			change_instrument.u.instr_change.channel = c;
			change_instrument.u.instr_change.code =
			    new_ss->channelstates[c].instrument;

			ret = sequencer_noteevent(seq, old_ss,
			    &change_instrument);
			if (ret != 0)
				return ret;
		}

		for (n = 0; n < MIDI_NOTE_COUNT; n++) {
			old = old_ss->channelstates[c].notestates[n];
			new = new_ss->channelstates[c].notestates[n];

			note_off.evtype = MIDIEV_NOTEOFF;
			note_off.u.note.channel = c;
			note_off.u.note.note = n;
			note_off.u.note.velocity = 0;

			note_on.evtype = MIDIEV_NOTEON;
			note_on.u.note.channel = c;
			note_on.u.note.note = n;
			note_on.u.note.velocity = new.velocity;

			/*
			 * Retrigger note if:
			 *   1. note is playing on old and new songstate
			 *     AND
			 *   2a. instrument has changed
			 *     OR
			 *   2b. velocity has changed
			 */
			retrigger_note = old.state && new.state &&
			    ((old.velocity != new.velocity) || instr_changed);

			if (retrigger_note) {
				ret = sequencer_noteevent(seq, old_ss,
				    &note_off);
				if (ret != 0)
					return ret;
				ret = sequencer_noteevent(seq, old_ss,
				    &note_on);
				if (ret != 0)
					return ret;
			} else if (old.state && !new.state) {
				/* Note is playing, but should no longer be. */
				ret = sequencer_noteevent(seq, old_ss,
				    &note_off);
				if (ret != 0)
					return ret;
			} else if (!old.state && new.state) {
				/* Note is not playing, but should be. */
				ret = sequencer_noteevent(seq, old_ss,
				    &note_on);
				if (ret != 0)
					return ret;
			}

			/*
			 * sequencer_noteevent() should also have
			 * a side effect to make these true:
			 */
			assert(old_ss->channelstates[c].notestates[n].state ==
			    new_ss->channelstates[c].notestates[n].state &&
			    old_ss->channelstates[c].notestates[n].velocity ==
			    new_ss->channelstates[c].notestates[n].velocity
			);
		}

		assert(old_ss->channelstates[c].instrument ==
		       new_ss->channelstates[c].instrument);
	}

	ret = clock_gettime(CLOCK_UPTIME,
	    &new_ss->latest_tempo_change_as_time);
	assert(ret == 0);

	new_ss->playback_state = PLAYING;
	old_ss->playback_state = FREEING_EVENTSTREAM;

	return 0;
}

static int
sequencer_switch_songs(struct sequencer *seq)
{
	struct songstate *old_ss;
	int ret;

	old_ss             = seq->playback_song;
	seq->playback_song = seq->reading_song;
	seq->reading_song  = old_ss;

	_mdl_log(MDLLOG_SEQ, 0,
	    "received a new playback stream, playback songstate is now %s\n",
	    ss_label(seq, seq->playback_song));

	seq->playback_song->time_as_measures =
	    seq->playback_song->keep_position_when_switched_to
		? old_ss->time_as_measures
		: 0.0;

	ret = sequencer_start_playing(seq, seq->playback_song, old_ss);
	if (ret != 0)
		return 1;

	return 0;
}
static void
sequencer_time_for_next_note(struct songstate *ss, struct timespec *notetime)
{
	struct midievent next_midievent;
	struct timespec time_for_note_since_latest_tempo_change;
	float measures_for_note_since_latest_tempo_change;
	float time_for_note_since_latest_tempo_change_in_ns;

	assert(ss != NULL);
	assert(ss->current_event.block != NULL);
	assert(0 <= ss->current_event.index &&
	    ss->current_event.index < EVENTBLOCKCOUNT);
	assert(ss->latest_tempo_change_as_time.tv_sec > 0 ||
	    ss->latest_tempo_change_as_time.tv_nsec > 0);
	assert(ss->playback_state == PLAYING);
	assert(ss->tempo > 0);

	next_midievent =
	    ss->current_event.block->events[ ss->current_event.index ];

	measures_for_note_since_latest_tempo_change =
	    next_midievent.time_as_measures -
	    ss->latest_tempo_change_as_measures;

	time_for_note_since_latest_tempo_change_in_ns =
	    (1000000000.0 * ss->measure_length * (60.0 * 4 / ss->tempo)) *
	    measures_for_note_since_latest_tempo_change;

	time_for_note_since_latest_tempo_change.tv_nsec =
	    fmodf(time_for_note_since_latest_tempo_change_in_ns, 1000000000.0);

	/*
	 * This is tricky to get right... naively dividing
	 * time_for_note_since_latest_tempo_change_in_ns / 1000000000.0
	 * does not always work, because of floating point rounding errors.
	 * XXX Should floating point be used for timing information at all?
	 * XXX (There may be issues in timing accuracy with very long music
	 * XXX playback... should calculate how much that could be.)
	 */
	time_for_note_since_latest_tempo_change.tv_sec =
	    rintf((time_for_note_since_latest_tempo_change_in_ns -
	    time_for_note_since_latest_tempo_change.tv_nsec) / 1000000000.0);

	notetime->tv_sec = time_for_note_since_latest_tempo_change.tv_sec +
	    ss->latest_tempo_change_as_time.tv_sec;
	notetime->tv_nsec = time_for_note_since_latest_tempo_change.tv_nsec +
	    ss->latest_tempo_change_as_time.tv_nsec;

	if (notetime->tv_nsec >= 1000000000) {
		notetime->tv_nsec -= 1000000000;
		notetime->tv_sec += 1;
	}
}

static void
sequencer_close(struct sequencer *seq)
{
	if (seq->interp_fd >= 0 && close(seq->interp_fd) == -1)
		warn("closing interpreter pipe");

	sequencer_close_songstate(seq, seq->playback_song);

	sequencer_free_songstate(seq->playback_song);
	sequencer_free_songstate(seq->reading_song);

	if (seq->client_socket >= 0) {
		if (imsg_flush(&seq->client_ibuf) == -1)
			warnx("error flushing imsg buffers to client");
		imsg_clear(&seq->client_ibuf);
		if (close(seq->client_socket) == -1)
			warn("closing client socket");
		seq->client_socket = -1;
	}

	if (imsg_flush(&seq->server_ibuf) == -1)
		warnx("error flushing imsg buffers to server");
	imsg_clear(&seq->server_ibuf);

	if (seq->dry_run)
		return;

	_mdl_midi_close_device();
}

static void
sequencer_close_songstate(const struct sequencer *seq, struct songstate *ss)
{
	struct midievent note_off;
	int c, n, ret;

	for (c = 0; c < MIDI_CHANNEL_COUNT; c++)
		for (n = 0; n < MIDI_CHANNEL_COUNT; n++)
			if (ss->channelstates[c].notestates[n].state) {
				note_off.evtype = MIDIEV_NOTEOFF;
				note_off.u.note.channel = c;
				note_off.u.note.note = n;
				note_off.u.note.velocity = 0;
				ret = sequencer_noteevent(seq, ss, &note_off);
				if (ret != 0)
					warnx("error in turning off note"
					    " %d on channel %d", n, c);
			}
}

static const char *
ss_label(const struct sequencer *seq, struct songstate *ss)
{
	return (ss == &seq->song1 ? "A" : "B");
}
