/* $Id: sequencer.c,v 1.155 2016/09/28 20:34:57 je Exp $ */

/*
 * Copyright (c) 2015, 2016 Juha Erkkilä <je@turnipsi.no-ip.org>
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

#define EVENTBLOCKCOUNT		256

struct eventblock {
	SIMPLEQ_ENTRY(eventblock) entries;
	struct timed_midievent events[EVENTBLOCKCOUNT];
	size_t readcount;
};
SIMPLEQ_HEAD(eventstream, eventblock);

struct eventpointer {
	struct eventblock      *block;
	int			index;
};

TAILQ_HEAD(tmidiev_queue, tmidiev_item);
struct tmidiev_item {
	struct timed_midievent		tmidiev;
	TAILQ_ENTRY(tmidiev_item)	tq;
};

struct notestate {
	struct tmidiev_item    *wanting_join_expr;
	unsigned int		state    : 1;
	unsigned int		velocity : 7;
};

struct channel_state {
	struct notestate	notestates[MIDI_NOTE_COUNT];
	u_int8_t		instrument;
	u_int8_t		volume;
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
static int	sequencer_add_to_tmidiev_queue(struct tmidiev_queue *,
     struct timed_midievent);
static void	sequencer_calculate_timeout(const struct sequencer *,
    const struct timespec *, struct timespec *);
static int	sequencer_clock_gettime(struct timespec *);
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
static int	sequencer_midievent(const struct sequencer *,
    struct songstate *, struct midievent *, int);
static int	sequencer_play_tmidiev_queue(struct tmidiev_queue *,
    struct songstate *, const struct sequencer *);
static int	sequencer_play_music(struct sequencer *,
    struct songstate *);
static ssize_t	sequencer_read_to_eventstream(struct songstate *, int);
static int	sequencer_reset_songstate(struct sequencer *,
    struct songstate *);
static int	sequencer_start_playing(const struct sequencer *,
    struct songstate *, struct songstate *);
static int	sequencer_switch_songs(struct sequencer *);
static void	sequencer_time_for_next_event(struct songstate *ss,
    struct timespec *);
static const char *ss_label(const struct sequencer *, struct songstate *);

static struct timespec
sequencer_calc_time_since_latest_tempo_change(const struct songstate *,
    float);

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
	struct timespec eventtime, timeout, *timeout_p;
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
			sequencer_time_for_next_event(seq->playback_song,
			    &eventtime);
			sequencer_calculate_timeout(seq, &eventtime, &timeout);
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
	int c, n;
	struct notestate *notestate;

	assert(ps == IDLE || ps == READING);

	SIMPLEQ_INIT(&ss->es);

	_mdl_log(MDLLOG_SEQ, 0,
	    "initializing a new songstate %s to state \"%s\"\n",
	   ss_label(seq, ss), strings[ps]);

	/* Initialize channelstates. */
	for (c = 0; c < MIDI_CHANNEL_COUNT; c++) {
		ss->channelstates[c].instrument = 0;
		ss->channelstates[c].volume = 0;
		for (n = 0; n < MIDI_NOTE_COUNT; n++) {
			notestate = &ss->channelstates[c].notestates[n];
			notestate->state = 0;
			notestate->velocity = 0;
			notestate->wanting_join_expr = NULL;
		}
	}

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
sequencer_calculate_timeout(const struct sequencer *seq,
    const struct timespec *eventtime, struct timespec *timeout)
{
	struct timespec current_time;
	int ret;

	if (seq->dry_run) {
		timeout->tv_sec = 0;
		timeout->tv_nsec = 0;
		return;
	}

	ret = sequencer_clock_gettime(&current_time);
	assert(ret == 0);

	timeout->tv_sec  = eventtime->tv_sec  - current_time.tv_sec;
	timeout->tv_nsec = eventtime->tv_nsec - current_time.tv_nsec;

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

static int
sequencer_clock_gettime(struct timespec *tp)
{
#ifdef HAVE_CLOCK_UPTIME
	return clock_gettime(CLOCK_UPTIME, tp);
#else
	return clock_gettime(CLOCK_MONOTONIC, tp);
#endif
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
		/* XXX FreeBSD 10.3 sometimes gets here (maybe on 1 of 1000
		 * XXX runs), even though client socket has *not* been
		 * XXX shutdown or should not have been.  I do not know
		 * XXX what is going on. */
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
	struct timed_midievent *tmidiev;
	struct midievent *midiev;
	struct timespec eventtime, time_to_play;
	struct tmidiev_queue tmeq;
	int ret, retvalue;

	retvalue = 0;

	ce = &ss->current_event;

	TAILQ_INIT(&tmeq);

	while (ce->block) {
		while (ce->index < EVENTBLOCKCOUNT) {
			tmidiev = &ce->block->events[ ce->index ];
			midiev = &tmidiev->midiev;

			if (midiev->evtype == MIDIEV_SONG_END) {
				ss->playback_state = IDLE;

				if (seq->client_socket >= 0) {
					ret = imsg_compose(&seq->client_ibuf,
					    SEQEVENT_SONG_END, 0, 0, -1, "",
					    0);
					if (ret == -1) {
						warnx("error sending"
						    " SEQEVENT_SONG_END");
						retvalue = 1;
						goto finish;
					}
				}

				goto finish;
			}

			sequencer_time_for_next_event(ss, &eventtime);
			sequencer_calculate_timeout(seq, &eventtime,
			    &time_to_play);

			/*
			 * If timeout has not been gone to zero,
			 * it is not our time to play yet.
			 */
			if (time_to_play.tv_sec > 0 ||
			    (time_to_play.tv_sec == 0 &&
			    time_to_play.tv_nsec > 0))
				goto finish;

			switch (midiev->evtype) {
			case MIDIEV_INSTRUMENT_CHANGE:
			case MIDIEV_NOTEOFF:
			case MIDIEV_NOTEON:
			case MIDIEV_VOLUMECHANGE:
				ret = sequencer_add_to_tmidiev_queue(&tmeq,
				    *tmidiev);
				if (ret != 0) {
					retvalue = 1;
					goto finish;
				}
				break;
			case MIDIEV_MARKER:
				/*
				 * XXX Not doing anything currently.
				 * XXX (Only interesting with textual location
				 * XXX -aware sequencer).
				 */
				break;
			case MIDIEV_SONG_END:
				/* This has been handled above. */
				assert(0);
				break;
			case MIDIEV_TEMPOCHANGE:
				ss->latest_tempo_change_as_time = eventtime;
				ss->latest_tempo_change_as_measures =
				    tmidiev->time_as_measures;
				ss->tempo = midiev->u.bpm;
				_mdl_log(MDLLOG_MIDI, 0,
				    "changing tempo to %.0fbpm\n", ss->tempo);
				break;
			default:
				assert(0);
			}

			ce->index += 1;
		}

		ce->block = SIMPLEQ_NEXT(ce->block, entries);
		ce->index = 0;
	}

finish:
	return sequencer_play_tmidiev_queue(&tmeq, ss, seq);
}

static int
sequencer_add_to_tmidiev_queue(struct tmidiev_queue *tmeq,
     struct timed_midievent tmidiev)
{
	struct tmidiev_item *mi;

	if ((mi = malloc(sizeof(struct tmidiev_item))) == NULL) {
		warn("malloc failure in sequencer_add_to_tmidiev_queue");
		return 1;
	}

	mi->tmidiev = tmidiev;

	TAILQ_INSERT_TAIL(tmeq, mi, tq);

	return 0;
}

static int
sequencer_play_tmidiev_queue(struct tmidiev_queue *tmeq,
    struct songstate *ss, const struct sequencer *seq)
{
	struct tmidiev_item *p, *q, *wanting_join_expr;
	struct midievent *midiev;
	u_int8_t channel, note;
	int ret;

	ret = 0;

	/*
	 * XXX Should keep track of exact times when notes are off,
	 * XXX and then compare those.  This heuristic works correctly only if
	 * XXX sequencer awakes up fast enough so that notes that should not
	 * XXX join are far enough, so they get into different queues.
	 */

	TAILQ_FOREACH_SAFE(p, tmeq, tq, q) {
		midiev = &p->tmidiev.midiev;
		channel = midiev->u.midinote.channel;
		note = midiev->u.midinote.note;

		if (midiev->evtype == MIDIEV_NOTEOFF
		    && midiev->u.midinote.joining) {
			ss->channelstates[channel].notestates[note]
			    .wanting_join_expr = p;
		} else if (midiev->evtype == MIDIEV_NOTEON) {
			wanting_join_expr = ss->channelstates[channel]
			    .notestates[note].wanting_join_expr;
			if (wanting_join_expr &&
			    wanting_join_expr->tmidiev.time_as_measures ==
			    p->tmidiev.time_as_measures) {
				/*
				 * There is a noteoff at the exact same time
				 * that wants a join, so remove the noteoff
				 * note from the play queue, and the noteon
				 * midievent as well, so the note simply
				 * continues on.
				 */
				TAILQ_REMOVE(tmeq, p, tq);
				free(p);
				TAILQ_REMOVE(tmeq, wanting_join_expr, tq);
				free(wanting_join_expr);
				ss->channelstates[channel].notestates[note]
				    .wanting_join_expr = NULL;
			}
		}
	}

	TAILQ_FOREACH_SAFE(p, tmeq, tq, q) {
		midiev = &p->tmidiev.midiev;
		channel = midiev->u.midinote.channel;
		note = midiev->u.midinote.note;
		ss->channelstates[channel].notestates[note]
		    .wanting_join_expr = NULL;

		if (ret == 0)
			ret = sequencer_midievent(seq, ss, midiev, 0);
		TAILQ_REMOVE(tmeq, p, tq);
		free(p);
	}

	return ret;
}

static int
sequencer_midievent(const struct sequencer *seq, struct songstate *ss,
    struct midievent *me, int level)
{
	int ret;
	struct notestate *nstate;

	ret = _mdl_midi_play_midievent(me, level, seq->dry_run);
	if (ret == 0) {
		switch (me->evtype) {
		case MIDIEV_INSTRUMENT_CHANGE:
			ss->channelstates[me->u.instr_change.channel]
			    .instrument = me->u.instr_change.code;
			break;
		case MIDIEV_NOTEOFF:
			nstate = &ss->channelstates[me->u.midinote.channel]
			    .notestates[me->u.midinote.note];
			nstate->state = 0;
			nstate->velocity = 0;
			break;
		case MIDIEV_NOTEON:
			nstate = &ss->channelstates[me->u.midinote.channel]
			    .notestates[me->u.midinote.note];
			nstate->state = 1;
			nstate->velocity = me->u.midinote.velocity;
			break;
		case MIDIEV_SONG_END:
		case MIDIEV_TEMPOCHANGE:
			/* These events should not have come this far. */
			assert(0);
			break;
		case MIDIEV_VOLUMECHANGE:
			ss->channelstates[me->u.volumechange.channel]
			    .volume = me->u.volumechange.volume;
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

	for (i = new_b->readcount / sizeof(struct timed_midievent);
	    i < (new_b->readcount + nr) / sizeof(struct timed_midievent);
	    i++) {
		/* The song end must not come again. */
		if (ss->got_song_end) {
			warnx("received music events after song end");
			nr = -1;
			goto finish;
		}

		if (new_b->events[i].midiev.evtype == MIDIEV_SONG_END)
			ss->got_song_end = 1;

		_mdl_timed_midievent_log(MDLLOG_MIDI, "received",
		    &new_b->events[i], 0);

		if (!_mdl_midi_check_timed_midievent(new_b->events[i],
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
	struct channel_state old_cs, new_cs;
	struct notestate old_ns, new_ns;
	struct eventpointer ce;
	struct timed_midievent *tmidiev;
	struct timed_midievent change_instrument, change_volume, note_off,
	    note_on;
	struct midievent *midiev;
	struct timespec latest_tempo_change_as_time,
	    time_since_latest_tempo_change;
	struct tmidiev_queue tmeq;
	int instr_changed, retrigger_note, volume_changed, c, n, ret;

	TAILQ_INIT(&tmeq);

	/*
	 * Find the event where we should be at at new songstate,
	 * and do a "shadow playback" to determine what our midi state
	 * should be.
	 */
	SIMPLEQ_FOREACH(ce.block, &new_ss->es, entries) {
		new_ss->current_event.block = ce.block;

		for (ce.index = 0; ce.index < EVENTBLOCKCOUNT; ce.index++) {
			new_ss->current_event.index = ce.index;

			tmidiev = &ce.block->events[ ce.index ];
			midiev = &tmidiev->midiev;

			if (tmidiev->time_as_measures >=
			    new_ss->time_as_measures)
				goto current_event_found;
			if (midiev->evtype == MIDIEV_SONG_END)
				goto current_event_found;

			switch (midiev->evtype) {
			case MIDIEV_INSTRUMENT_CHANGE:
				c = midiev->u.instr_change.channel;
				new_ss->channelstates[c].instrument =
				    midiev->u.instr_change.code;
				break;
			case MIDIEV_NOTEON:
				c = midiev->u.midinote.channel;
				n = midiev->u.midinote.note;
				new_ss->channelstates[c].notestates[n].state =
				    1;
				new_ss->channelstates[c].notestates[n]
				    .velocity = midiev->u.midinote.velocity;
				break;
			case MIDIEV_NOTEOFF:
				c = midiev->u.midinote.channel;
				n = midiev->u.midinote.note;
				new_ss->channelstates[c].notestates[n].state =
				    0;
				new_ss->channelstates[c].notestates[n]
				    .velocity = 0;
				break;
			case MIDIEV_SONG_END:
				/* This has been handled above. */
				assert(0);
				break;
			case MIDIEV_TEMPOCHANGE:
				new_ss->latest_tempo_change_as_measures =
				    tmidiev->time_as_measures;
				new_ss->tempo = midiev->u.bpm;
				break;
			default:
				assert(0);
			}
		}
	}

current_event_found:
	/*
	 * Sync playback state
	 *   (start or turn off notes according to new playback song).
	 */
	for (c = 0; c < MIDI_CHANNEL_COUNT; c++) {
		old_cs = old_ss->channelstates[c];
		new_cs = new_ss->channelstates[c];

		instr_changed = (old_cs.instrument != new_cs.instrument);
		volume_changed = (old_cs.volume != new_cs.volume);

		if (instr_changed) {
			change_instrument.time_as_measures =
			    new_ss->time_as_measures;
			change_instrument.midiev.evtype =
			    MIDIEV_INSTRUMENT_CHANGE;
			change_instrument.midiev.u.instr_change.channel = c;
			change_instrument.midiev.u.instr_change.code =
			    new_cs.instrument;
			ret = sequencer_add_to_tmidiev_queue(&tmeq,
			    change_instrument);
			if (ret != 0)
				return ret;
		}

		if (volume_changed) {
			change_volume.time_as_measures =
			    new_ss->time_as_measures;
			change_volume.midiev.evtype = MIDIEV_VOLUMECHANGE;
			change_volume.midiev.u.volumechange.channel = c;
			change_volume.midiev.u.volumechange.volume =
			    new_cs.volume;
			ret = sequencer_add_to_tmidiev_queue(&tmeq,
			    change_volume);
			if (ret != 0)
				return ret;
		}

		for (n = 0; n < MIDI_NOTE_COUNT; n++) {
			old_ns = old_cs.notestates[n];
			new_ns = new_cs.notestates[n];

			note_off.time_as_measures = new_ss->time_as_measures;
			note_off.midiev.evtype = MIDIEV_NOTEOFF;
			note_off.midiev.u.midinote.channel = c;
			note_off.midiev.u.midinote.joining = 0;
			note_off.midiev.u.midinote.note = n;
			note_off.midiev.u.midinote.velocity = 0;

			note_on.time_as_measures = new_ss->time_as_measures;
			note_on.midiev.evtype = MIDIEV_NOTEON;
			note_on.midiev.u.midinote.channel = c;
			note_on.midiev.u.midinote.joining = 0;
			note_on.midiev.u.midinote.note = n;
			note_on.midiev.u.midinote.velocity = new_ns.velocity;

			/*
			 * Retrigger note if:
			 *   1. note is playing on old and new songstate
			 *     AND
			 *   2a. instrument has changed
			 *     OR
			 *   2b. velocity has changed
			 */
			retrigger_note = old_ns.state && new_ns.state &&
			    ((old_ns.velocity != new_ns.velocity) ||
			    instr_changed);

			if (retrigger_note) {
				ret = sequencer_add_to_tmidiev_queue(&tmeq,
				    note_off);
				if (ret != 0)
					return ret;
				ret = sequencer_add_to_tmidiev_queue(&tmeq,
				    note_on);
				if (ret != 0)
					return ret;
			} else if (old_ns.state && !new_ns.state) {
				/* Note is playing, but should no longer be. */
				ret = sequencer_add_to_tmidiev_queue(&tmeq,
				    note_off);
				if (ret != 0)
					return ret;
			} else if (!old_ns.state && new_ns.state) {
				/* Note is not playing, but should be. */
				ret = sequencer_add_to_tmidiev_queue(&tmeq,
				    note_on);
				if (ret != 0)
					return ret;
			}
		}
	}

	if ((ret = sequencer_play_tmidiev_queue(&tmeq, old_ss, seq)) != 0)
		return ret;

	/*
	 * play_midievent_queue() should also have a side effect to make
	 * these assertions true:
	 */
	for (c = 0; c < MIDI_CHANNEL_COUNT; c++) {
		old_cs = old_ss->channelstates[c];
		new_cs = new_ss->channelstates[c];
		for (n = 0; n < MIDI_NOTE_COUNT; n++) {
			old_ns = old_cs.notestates[n];
			new_ns = new_cs.notestates[n];

			assert(old_ns.state == new_ns.state);
			assert(old_ns.velocity == new_ns.velocity);
		}
		assert(old_cs.instrument == new_cs.instrument);
		assert(old_cs.volume == new_cs.volume);
	}

	/*
	 * Update new_ss->latest_tempo_change_in_time to match what it might
	 * have been.
	 */

	assert(new_ss->time_as_measures >=
	    new_ss->latest_tempo_change_as_measures);

	time_since_latest_tempo_change =
	    sequencer_calc_time_since_latest_tempo_change(new_ss,
	    new_ss->time_as_measures);

	ret = sequencer_clock_gettime(&latest_tempo_change_as_time);
	assert(ret == 0);

	latest_tempo_change_as_time.tv_sec -=
	    time_since_latest_tempo_change.tv_sec;
	latest_tempo_change_as_time.tv_nsec -=
	    time_since_latest_tempo_change.tv_nsec;
	if (latest_tempo_change_as_time.tv_nsec < 0) {
		latest_tempo_change_as_time.tv_sec -= 1;
		latest_tempo_change_as_time.tv_nsec += 1000000000;
	}
	new_ss->latest_tempo_change_as_time = latest_tempo_change_as_time;

	new_ss->playback_state = PLAYING;
	old_ss->playback_state = FREEING_EVENTSTREAM;

	return 0;
}

static struct timespec
sequencer_calc_time_since_latest_tempo_change(const struct songstate *ss,
    float time_as_measures)
{
	struct timespec time_since_latest_tempo_change;
	float time_since_latest_tempo_change_in_ns;
	float measures_since_latest_tempo_change;

	assert(ss != NULL);
	assert(ss->tempo > 0);

	measures_since_latest_tempo_change = time_as_measures -
	    ss->latest_tempo_change_as_measures;

	time_since_latest_tempo_change_in_ns =
	    (1000000000.0 * ss->measure_length * (60.0 * 4 / ss->tempo)) *
	    measures_since_latest_tempo_change;

	time_since_latest_tempo_change.tv_nsec =
	    fmodf(time_since_latest_tempo_change_in_ns, 1000000000.0);

	/*
	 * This is tricky to get right... naively dividing
	 * time_since_latest_tempo_change_in_ns / 1000000000.0
	 * does not always work, because of floating point rounding errors.
	 * XXX Should floating point be used for timing information at all?
	 * XXX (There may be issues in timing accuracy with very long music
	 * XXX playback... should calculate how much that could be.)
	 */
	time_since_latest_tempo_change.tv_sec =
	    rintf((time_since_latest_tempo_change_in_ns -
	    time_since_latest_tempo_change.tv_nsec) / 1000000000.0);

	return time_since_latest_tempo_change;
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
sequencer_time_for_next_event(struct songstate *ss, struct timespec *eventtime)
{
	struct timed_midievent next_midievent;
	struct timespec time_since_latest_tempo_change;

	assert(ss != NULL);
	assert(ss->current_event.block != NULL);
	assert(0 <= ss->current_event.index &&
	    ss->current_event.index < EVENTBLOCKCOUNT);
	assert(ss->latest_tempo_change_as_time.tv_sec > 0 ||
	    ss->latest_tempo_change_as_time.tv_nsec > 0);
	assert(ss->playback_state == PLAYING);

	next_midievent =
	    ss->current_event.block->events[ ss->current_event.index ];

	time_since_latest_tempo_change =
	    sequencer_calc_time_since_latest_tempo_change(ss,
	    next_midievent.time_as_measures);

	eventtime->tv_sec = time_since_latest_tempo_change.tv_sec +
	    ss->latest_tempo_change_as_time.tv_sec;
	eventtime->tv_nsec = time_since_latest_tempo_change.tv_nsec +
	    ss->latest_tempo_change_as_time.tv_nsec;

	if (eventtime->tv_nsec >= 1000000000) {
		eventtime->tv_nsec -= 1000000000;
		eventtime->tv_sec += 1;
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

	_mdl_log(MDLLOG_SEQ, 0,
	    "turning off notes that are currently playing\n");

	for (c = 0; c < MIDI_CHANNEL_COUNT; c++)
		for (n = 0; n < MIDI_NOTE_COUNT; n++)
			if (ss->channelstates[c].notestates[n].state) {
				_mdl_log(MDLLOG_SEQ, 1,
				    "channel=%d has note=%d playing,"
				    " turning it off\n", c, n);
				note_off.evtype = MIDIEV_NOTEOFF;
				note_off.u.midinote.channel = c;
				note_off.u.midinote.note = n;
				note_off.u.midinote.velocity = 0;
				ret = sequencer_midievent(seq, ss, &note_off,
				    2);
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
