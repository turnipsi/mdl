/* $Id: mdl.c,v 1.5 2016/05/13 20:30:00 je Exp $ */

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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interpreter.h"
#include "midi.h"
#include "protocol.h"
#include "sequencer.h"
#include "util.h"

#define MDL_VERSION	"0.0-current"

#ifdef HAVE_SNDIO
#define DEFAULT_MIDIDEV_TYPE MIDIDEV_SNDIO
#else
#define DEFAULT_MIDIDEV_TYPE MIDIDEV_RAW
#endif

#ifdef HAVE_MALLOC_OPTIONS
extern char	*malloc_options;
#endif /* HAVE_MALLOC_OPTIONS */

static int	show_version(void);
static int	start_interpreter(int, int, int);
static void	handle_signal(int);
static int	send_fd_through_socket(int, int);
static int	wait_for_subprocess(const char *, int);
static int	handle_musicfiles(char **, int, const char *, int,
    enum mididev_type, const char *);
static void __dead usage(void);

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdl_shutdown_main = 0;

extern int loglevel;

char *mdl_process_type;

static void __dead
usage(void)
{
	(void) fprintf(stderr, "usage: mdl [-nv] [-d debuglevel] [-f device]"
	    " [-m MIDI-interface] [file ...]\n");
	exit(1);
}

static void
handle_signal(int signo)
{
	assert(signo == SIGINT || signo == SIGTERM);

	if (signo == SIGINT || signo == SIGTERM)
		mdl_shutdown_main = 1;
}

int
main(int argc, char *argv[])
{
	char *devicepath, *server_socketpath;
	char **musicfiles;
	int musicfilecount, ch, nflag;
	size_t ret;
	enum mididev_type mididev_type;

#ifdef HAVE_MALLOC_OPTIONS
	malloc_options = (char *) "AFGJPS";
#endif /* HAVE_MALLOC_OPTIONS */

	mdl_process_type = "main";

	devicepath = NULL;
	nflag = 0;
	mididev_type = DEFAULT_MIDIDEV_TYPE;

	if (pledge("proc recvfd rpath sendfd stdio unix wpath", NULL) == -1)
		err(1, "pledge");

	signal(SIGINT,  handle_signal);
	signal(SIGTERM, handle_signal);

	_mdl_logging_init();

	while ((ch = getopt(argc, argv, "d:f:m:nv")) != -1) {
		switch (ch) {
		case 'd':
			if (_mdl_logging_setopts(optarg) == -1)
				errx(1, "error in setting logging opts");
			break;
		case 'f':
			devicepath = optarg;
			break;
		case 'm':
			if (strcmp(optarg, "raw") == 0) {
				mididev_type = MIDIDEV_RAW;
#ifdef HAVE_SNDIO
			} else if (strcmp(optarg, "sndio") == 0) {
				mididev_type = MIDIDEV_SNDIO;
#endif /* HAVE_SNDIO */
			} else {
				warnx("unsupported midi interface \"%s\"",
				    optarg);
				warnx("run with -v to see possible options");
				exit(1);
			}
			break;
		case 'n':
			nflag = 1;
			break;
		case 'v':
			if (show_version() != 0)
				exit(1);
			exit(0);
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	_mdl_log(MDLLOG_PROCESS, 0, "new main process, pid %d\n", getpid());

	musicfilecount = argc;
	musicfiles = argv;

	if ((server_socketpath = _mdl_get_socketpath()) == NULL)
		err(1, "could not determine socketpath");

	ret = handle_musicfiles(musicfiles, musicfilecount, server_socketpath,
	    nflag, mididev_type, devicepath);
	if (ret != 0 || mdl_shutdown_main == 1)
		return 1;

	_mdl_logging_close();

	return 0;
}

static int
show_version(void)
{
	int ret;

	ret = printf("mdl version %s\n", MDL_VERSION);
	if (ret < 0)
		return 1;

	ret = printf("compiled with midi interface support:\n");
	if (ret < 0)
		return 1;

	ret = printf("  raw%s\n",
	    (DEFAULT_MIDIDEV_TYPE == MIDIDEV_RAW ? " (default)" : ""));
	if (ret < 0)
		return 1;

#ifdef HAVE_SNDIO
	ret = printf("  sndio%s\n",
	    (DEFAULT_MIDIDEV_TYPE == MIDIDEV_SNDIO ? " (default)" : ""));
	if (ret < 0)
		return 1;
#endif

	return 0;
}

static int
handle_musicfiles(char **files, int filecount,
    const char *server_socketpath, int dry_run,
    enum mididev_type mididev_type, const char *devicepath)
{
	int ms_sp[2];	/* main-sequencer socketpair */
	int server_socket, file_fd, ret, retvalue;
	int using_stdin, i;
	pid_t sequencer_pid;
	char *stdinfiles[] = { "-" };
	int sequencer_retvalue;

	retvalue = 0;

	/* XXX We should use server_socketpath to initialize this: */
	server_socket = -1;

	/* Setup socketpair for main <-> sequencer communication. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, ms_sp) == -1) {
		warn("could not setup socketpair for main <-> sequencer");
		return 1;
	}

	if (fflush(NULL) == EOF)
		warn("error flushing streams before sequencer fork");

	/* Fork the midi sequencer process. */
	if ((sequencer_pid = fork()) == -1) {
		warn("could not fork sequencer process");
		if (close(ms_sp[0]) == -1)
			warn("error closing first end of ms_sp");
		if (close(ms_sp[1]) == -1)
			warn("error closing second end of ms_sp");
		return 1;
	}

	if (sequencer_pid == 0) {
		/*
		 * We are in sequencer process, start sequencer loop.
		 */
		_mdl_logging_clear();
		mdl_process_type = "seq";
		_mdl_log(MDLLOG_PROCESS, 0, "new sequencer process, pid %d\n",
		    getpid());
		/*
		 * XXX We should close all file descriptors that sequencer
		 * XXX does not need... does this do that?
		 */
		if (close(ms_sp[0]) == -1)
			warn("error closing first end of ms_sp");
		sequencer_retvalue = _mdl_sequencer_loop(ms_sp[1], dry_run,
		    mididev_type, devicepath);
		if (close(ms_sp[1]) == -1)
			warn("closing main socket");
		if (fflush(NULL) == EOF) {
			warn("error flushing streams in sequencer"
			       " before exit");
		}
		_mdl_logging_close();
		_exit(sequencer_retvalue);
	}

	/* Now that sequencer has been forked, we can drop "wpath" pledge. */
	if (pledge("proc recvfd rpath sendfd stdio unix", NULL) == -1)
		err(1, "pledge");

	if (close(ms_sp[1]) == -1)
		warn("error closing second end of ms_sp");

	if (filecount == 0) {
		filecount = 1;
		files = stdinfiles;
	}

	/* XXX We could also open all files immediately so we could then
	 * XXX drop the rpath pledge? */
	for (i = 0; i < filecount && mdl_shutdown_main == 0; i++) {
		if (strcmp(files[i], "-") == 0) {
			file_fd = fileno(stdin);
			using_stdin = 1;
		} else {
			using_stdin = 0;
			file_fd = open(files[i], O_RDONLY);
			if (file_fd == -1) {
				warn("could not open %s", files[i]);
				continue;
			}
		}

		ret = start_interpreter(file_fd, ms_sp[0], server_socket);
		if (ret != 0) {
			warnx("error in handling %s",
			    (using_stdin ? "stdin" : files[i]));
			if (close(file_fd) == -1)
				warn("error closing %s", files[i]);
			retvalue = 1;
			goto finish;
		}

		if (file_fd != fileno(stdin) && close(file_fd) == -1)
			warn("error closing %s", files[i]);
	}

finish:
	if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		return 1;
	}

	if (close(ms_sp[0]) == -1)
		warn("error closing first end of ms_sp");

	if (server_socket >= 0 && close(server_socket) == -1)
		warn("error closing server socket");

	if (wait_for_subprocess("sequencer", sequencer_pid) != 0)
		return 1;

	return retvalue;
}

static int
start_interpreter(int file_fd, int sequencer_socket, int server_socket)
{
	int mi_sp[2];	/* main-interpreter socketpair */
	int is_pipe[2];	/* interpreter-sequencer pipe */

	int ret;
	pid_t interpreter_pid;

	/* Setup socketpair for main <-> interpreter communication. */
	if (socketpair(AF_UNIX, SOCK_STREAM, 0, mi_sp) == -1) {
		warn("could not setup socketpair for main <-> interpreter");
		return 1;
	}

	/* Setup pipe for interpreter --> sequencer communication. */
	if (pipe(is_pipe) == -1) {
		warn("could not setup pipe for interpreter -> sequencer");
		if (close(mi_sp[0]) == -1)
			warn("error closing first end of mi_sp");
		if (close(mi_sp[1]) == -1)
			warn("error closing second end of mi_sp");
		return 1;
	}


	if (fflush(NULL) == EOF)
		warn("error flushing streams before interpreter fork");

	if ((interpreter_pid = fork()) == -1) {
		warn("could not fork interpreter pid");
		if (close(mi_sp[1]) == -1)
			warn("error closing second end of mi_sp");
		if (close(mi_sp[0]) == -1)
			warn("error closing first end of mi_sp");
		if (close(is_pipe[1]) == -1)
			warn("error closing write end of is_pipe");
		if (close(is_pipe[0]) == -1)
			warn("error closing read end of is_pipe");
		return 1;
	}

	if (interpreter_pid == 0) {
		/*
		 * We are in the interpreter process.
		 */
		_mdl_logging_clear();
		mdl_process_type = "interp";
		_mdl_log(MDLLOG_PROCESS, 0,
		    "new interpreter process, pid %d\n", getpid());

		/*
		 * Be strict here when closing file descriptors so that we
		 * do not leak file descriptors to interpreter process.
		 */
		if (close(sequencer_socket) == -1) {
			warn("error closing sequencer socket");
			ret = 1;
			goto interpreter_out;
		}
		if (close(mi_sp[0]) == -1) {
			warn("error closing first end of mi_sp");
			ret = 1;
			goto interpreter_out;
		}
		if (close(is_pipe[0]) == -1) {
			warn("error closing read end of is_pipe");
			ret = 1;
			goto interpreter_out;
		}

		ret = _mdl_handle_musicfile_and_socket(file_fd, mi_sp[1],
		    is_pipe[1], server_socket);

		if (file_fd != fileno(stdin) && close(file_fd) == -1)
			warn("error closing music file");

		if (close(is_pipe[1]) == -1)
			warn("error closing write end of is_pipe");
		if (close(mi_sp[1]) == -1)
			warn("error closing second end of mi_sp");

interpreter_out:
		if (fflush(NULL) == EOF) {
			warn("error flushing streams in sequencer"
			    " before exit");
		}

		_mdl_logging_close();

		_exit(ret);
	}

	if (close(mi_sp[1]) == -1)
		warn("error closing second end of mi_sp");
	if (close(is_pipe[1]) == -1)
		warn("error closing write end of is_pipe");

	/*
	 * We can communicate to interpreter through mi_sp[0],
	 * but to establish interpreter --> sequencer communication
	 * we must send is_pipe[0] to sequencer.
	 */

	if (send_fd_through_socket(is_pipe[0], sequencer_socket) != 0) {
		/*
		 * XXX What to do in case of error?
		 * XXX What should we clean up?
		 */
	}

	/* XXX mi_sp[0] not yet used for anything. */

	if (close(mi_sp[0]) == -1)
		warn("error closing first end of mi_sp");
	if (close(is_pipe[0]) == -1)
		warn("error closing read end of is_pipe");

	/*
	 * XXX Only one interpreter thread should be running at once,
	 * XXX so we should wait specifically for that one to finish
	 * XXX (we might do something interesting while waiting, though).
	 */
	if (wait_for_subprocess("interpreter", interpreter_pid) != 0)
		return 1;

	return 0;
}

static int
wait_for_subprocess(const char *process_type, int pid)
{
	int status;

	if (waitpid(pid, &status, 0) == -1) {
		warn("error when waiting for %s pid %d", process_type, pid);
		return 1;
	}

	if (WIFSIGNALED(status)) {
		warnx("%s pid %d terminated by signal %d (%s)",
		    process_type, pid, WTERMSIG(status),
		    strsignal(WTERMSIG(status)));
#if MDL_USE_AFL
		/*
		 * When subprocesses have exited abnormally, abort()
		 * execution in the main process so that afl-fuzz will
		 * catch that as an abnormal exit.
		 */
		abort();
#else
		return 1;
#endif
	}

	if (!WIFEXITED(status)) {
		warnx("%s pid %d not terminated normally", process_type, pid);
		return 1;
	}

	_mdl_log(MDLLOG_PROCESS, 0, "%s pid %d exited with status code %d\n",
	    process_type, pid, WEXITSTATUS(status));

	return 0;
}

static int
send_fd_through_socket(int fd, int socket)
{
	struct msghdr	msg;
	struct cmsghdr *cmsg;
	union {
		struct cmsghdr	hdr;
		unsigned char	buf[CMSG_SPACE(sizeof(int))];
	} cmsgbuf;

	memset(&msg, 0, sizeof(msg));
	msg.msg_control = &cmsgbuf.buf;
	msg.msg_controllen = sizeof(cmsgbuf.buf);

	memset(&cmsgbuf, 0, sizeof(cmsgbuf));

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;

	*(int *)CMSG_DATA(cmsg) = fd;

	if (sendmsg(socket, &msg, 0) == -1) {
		warn("sending fd through socket");
		return 1;
	}

	return 0;
}
