/* $Id: mdl.c,v 1.53 2016/04/11 19:25:37 je Exp $ */

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

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>

#include <assert.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "interpreter.h"
#include "sequencer.h"
#include "util.h"

#define SOCKETPATH_LEN 104

#ifndef NDEBUG
extern char	*malloc_options;
#endif /* !NDEBUG */

static int	get_default_mdldir(char *);
static int	get_default_socketpath(char *, const char *);
static int	start_interpreter(int, int, int, int);
static void	handle_signal(int);
static int	send_fd_through_socket(int, int);
static int	wait_for_subprocess(const char *, int);
static int	setup_sequencer_for_sources(char **, int, const char *, int,
    int);
static int	setup_server_socket(const char *);
static void __dead usage(void);

/* If set in signal handler, we should shut down. */
volatile sig_atomic_t mdl_shutdown_main = 0;

extern int loglevel;

char *mdl_process_type;

static void __dead
usage(void)
{
	(void) fprintf(stderr, "usage: mdl [-cs] [-D mdldir] [file ...]\n");
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
	char mdldir[PATH_MAX], server_socketpath[SOCKETPATH_LEN];
	char **musicfiles;
	int musicfilecount, ch, cflag, nflag, sflag, fileflags, lockfd;
	size_t ret;

#ifndef NDEBUG
	malloc_options = (char *) "AFGJPS";
#endif /* !NDEBUG */

	mdl_process_type = "main";

	cflag = nflag = sflag = 0;
	lockfd = -1;

	if (pledge("cpath flock proc recvfd rpath sendfd stdio unix wpath",
	    NULL) == -1)
		err(1, "pledge");

	signal(SIGINT,  handle_signal);
	signal(SIGTERM, handle_signal);

	mdl_logging_init();

	if (get_default_mdldir(mdldir) != 0)
		errx(1, "could not get default mdl directory");

	while ((ch = getopt(argc, argv, "cd:D:ns")) != -1) {
		switch (ch) {
		case 'c':
			cflag = 1;
			break;
		case 'd':
			if (mdl_logging_setopts(optarg) == -1)
				errx(1, "error in setting logging opts");
			break;
		case 'D':
			if (strlcpy(mdldir, optarg, PATH_MAX) >= PATH_MAX)
				errx(1, "mdldir too long");
			break;
		case 'n':
			nflag = 1;
			break;
		case 's':
			sflag = 1;
			break;
		default:
			usage();
			/* NOTREACHED */
		}
	}
	argc -= optind;
	argv += optind;

	mdl_log(MDLLOG_PROCESS, 0, "new main process, pid %d\n", getpid());

	if ((mkdir(mdldir, 0755) == -1) && errno != EEXIST)
		err(1, "error creating %s", mdldir);

	if (sflag) {
		/*
		 * When opening server socket, open mdldir for exclusive
		 * lock, to get exclusive access to socket path, not needed
		 * for anything else.
		 */
		fileflags = O_RDONLY|O_NONBLOCK|O_EXLOCK|O_DIRECTORY;
		if ((lockfd = open(mdldir, fileflags)) == -1) {
			warn("could not open %s for exclusive lock", mdldir);
			errx(1, "do you have another instance of" \
			    " mdl running?");
		}
	}

	if (pledge("cpath proc recvfd rpath sendfd stdio unix wpath", NULL)
	    == -1)
		err(1, "pledge");

	if (get_default_socketpath(server_socketpath, mdldir) != 0)
		errx(1, "could not get default socketpath");

	musicfilecount = argc;
	musicfiles = argv;

	if (cflag && sflag) {
		warnx("-c and -s options are mutually exclusive");
		usage();
		/* NOTREACHED */
	}

	if (cflag && musicfilecount > 1)
		warnx("sending only the first musicfile (%s)", musicfiles[0]);

	ret = setup_sequencer_for_sources(musicfiles, musicfilecount,
	    (sflag ? server_socketpath : NULL), lockfd, nflag);
	if (ret != 0 || mdl_shutdown_main == 1)
		return 1;

	if (lockfd >= 0 && close(lockfd) == -1)
		warn("error closing lockfd");

	mdl_logging_close();

	return 0;
}

static int
get_default_mdldir(char *mdldir)
{
	int ret;
	char *home;

	if ((home = getenv("HOME")) == NULL) {
		warnx("could not determine user home directory");
		return 1;
	}

	ret = snprintf(mdldir, PATH_MAX, "%s/.mdl", home);
	if (ret == -1 || ret >= PATH_MAX) {
		warnx("mdl home directory too long, check HOME");
		return 1;
	}

	return 0;
}

static int
get_default_socketpath(char *socketpath, const char *mdldir)
{
	int ret;

	ret = snprintf(socketpath, SOCKETPATH_LEN, "%s/socket", mdldir);
	if (ret == -1 || ret >= SOCKETPATH_LEN) {
		warnx("default server socketpath too long, mdldir is %s",
		      mdldir);
		return 1;
	}

	return 0;
}

static int
setup_sequencer_for_sources(char **files, int filecount,
    const char *socketpath, int lockfd, int dry_run)
{
	int ms_sp[2];	/* main-sequencer socketpair */
	int server_socket, file_fd, ret, retvalue;
	int using_stdin, i;
	pid_t sequencer_pid;
	char *stdinfiles[] = { "-" };
	int sequencer_retvalue;

	server_socket = -1;
	retvalue = 0;

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
			warn("error closing first endpoint of ms_sp");
		if (close(ms_sp[1]) == -1)
			warn("error closing second endpoint of ms_sp");
		return 1;
	}

	if (sequencer_pid == 0) {
		/*
		 * We are in sequencer process, start sequencer loop.
		 */
		mdl_logging_clear();
		mdl_process_type = "seq";
		mdl_log(MDLLOG_PROCESS, 0, "new sequencer process, pid %d\n",
		    getpid());
		/*
		 * XXX We should close all file descriptors that sequencer
		 * XXX does not need... does this do that?
		 */
		if (lockfd >= 0 && close(lockfd) == -1)
			warn("error closing lockfd");
		if (close(ms_sp[0]) == -1)
			warn("error closing first endpoint of ms_sp");
		sequencer_retvalue = sequencer_loop(ms_sp[1], dry_run);
		if (close(ms_sp[1]) == -1)
			warn("closing main socket");
		if (fflush(NULL) == EOF) {
			warn("error flushing streams in sequencer"
			       " before exit");
		}
		mdl_logging_close();
		_exit(sequencer_retvalue);
	}

	if (close(ms_sp[1]) == -1)
		warn("error closing second endpoint of ms_sp");

	if (pledge("cpath proc rpath sendfd stdio unix", NULL) == -1) {
		warn("pledge");
		retvalue = 1;
		goto finish;
	}

	if (socketpath) {
		if ((server_socket = setup_server_socket(socketpath)) == -1) {
			retvalue = 1;
			goto finish;
		}
	}

	if (pledge("cpath proc rpath sendfd stdio", NULL) == -1) {
		warn("pledge");
		retvalue = 1;
		goto finish;
	}

	if (filecount == 0) {
		filecount = 1;
		files = stdinfiles;
	}

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

		ret = start_interpreter(file_fd, ms_sp[0], server_socket,
		    lockfd);
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
	if (pledge("cpath stdio", NULL) == -1) {
		warn("pledge");
		return 1;
	}

	if (close(ms_sp[0]) == -1)
		warn("error closing first endpoint of ms_sp");

	if (server_socket >= 0 && close(server_socket) == -1)
		warn("error closing server socket");

	if (socketpath != NULL && unlink(socketpath) && errno != ENOENT)
		warn("could not delete %s", socketpath);

	if (pledge("stdio", NULL) == -1) {
		warn("pledge");
		return 1;
	}

	if (wait_for_subprocess("sequencer", sequencer_pid) != 0)
		return 1;

	return retvalue;
}

static int
start_interpreter(int file_fd, int sequencer_socket, int server_socket,
    int lockfd)
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

	/* Setup pipe for interpreter -> sequencer communication. */
	if (pipe(is_pipe) == -1) {
		warn("could not setup pipe for interpreter -> sequencer");
		if (close(mi_sp[0]) == -1)
			warn("error closing first endpoint of mi_sp");
		if (close(mi_sp[1]) == -1)
			warn("error closing second endpoint of mi_sp");
		return 1;
	}


	if (fflush(NULL) == EOF)
		warn("error flushing streams before interpreter fork");

	if ((interpreter_pid = fork()) == -1) {
		warn("could not fork interpreter pid");
		if (close(mi_sp[0]) == -1)
			warn("error closing first endpoint of mi_sp");
		if (close(mi_sp[1]) == -1)
			warn("error closing second endpoint of mi_sp");
		if (close(is_pipe[0]) == -1)
			warn("error closing first endpoint of is_pipe");
		if (close(is_pipe[1]) == -1)
			warn("error closing second endpoint of is_pipe");
		return 1;
	}

	if (interpreter_pid == 0) {
		/*
		 * We are in the interpreter process.
		 */
		mdl_logging_clear();
		mdl_process_type = "interp";
		mdl_log(MDLLOG_PROCESS, 0,
		    "new interpreter process, pid %d\n", getpid());

		/*
		 * Be strict here when closing file descriptors so that we
		 * do not leak file descriptors to interpreter process.
		 */
		if (lockfd >= 0 && close(lockfd) == -1) {
			warn("error closing lock file descriptor");
			ret = 1;
			goto interpreter_out;
		}
		if (close(sequencer_socket) == -1) {
			warn("error closing sequencer socket");
			ret = 1;
			goto interpreter_out;
		}
		if (close(mi_sp[0]) == -1) {
			warn("error closing first endpoint of mi_sp");
			ret = 1;
			goto interpreter_out;
		}
		if (close(is_pipe[1]) == -1) {
			warn("error closing second endpoint of is_pipe");
			ret = 1;
			goto interpreter_out;
		}

		ret = handle_musicfile_and_socket(file_fd, mi_sp[1],
		    is_pipe[0], server_socket);

		if (file_fd != fileno(stdin) && close(file_fd) == -1)
			warn("error closing music file");

		if (close(is_pipe[0]) == -1)
			warn("error closing first endpoint of is_pipe");
		if (close(mi_sp[1]) == -1)
			warn("error closing second endpoint of mi_sp");

interpreter_out:
		if (fflush(NULL) == EOF) {
			warn("error flushing streams in sequencer"
			    " before exit");
		}

		mdl_logging_close();

		_exit(ret);
	}

	if (close(mi_sp[1]) == -1)
		warn("error closing second endpoint of mi_sp");
	if (close(is_pipe[0]) == -1)
		warn("error closing first endpoint of is_pipe");

	/*
	 * We can communicate to interpreter through mi_sp[0],
	 * but to establish interpreter <-> sequencer communication
	 * we must send is_pipe[1] to sequencer.
	 */

	if (send_fd_through_socket(is_pipe[1], sequencer_socket) != 0) {
		/*
		 * XXX What to do in case of error?
		 * XXX What should we clean up?
		 */
	}

	/* XXX mi_sp[0] not yet used for anything. */

	if (close(mi_sp[0]) == -1)
		warn("error closing first endpoint of mi_sp");
	if (close(is_pipe[1]) == -1)
		warn("error closing second endpoint of is_pipe");

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

	mdl_log(MDLLOG_PROCESS, 0, "%s pid %d exited with status code %d\n",
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

static int
setup_server_socket(const char *socketpath)
{
	struct sockaddr_un sun;
	int ret, server_socket;

	memset(&sun, 0, sizeof(struct sockaddr_un));
	sun.sun_family = AF_UNIX;
	ret = strlcpy(sun.sun_path, socketpath, SOCKETPATH_LEN);
	assert(ret < SOCKETPATH_LEN);

	if ((server_socket = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		warn("could not open socket %s", socketpath);
		return -1;
	}

	/*
	 * Exclusive flock() for mdldir should mean that
	 * no other mdl process is using the socketpath.
	 */
	if (unlink(socketpath) == -1 && errno != ENOENT) {
		warn("could not remove %s", socketpath);
		goto fail;
	}

	ret = bind(server_socket, (struct sockaddr *)&sun, SUN_LEN(&sun));
	if (ret == -1) {
		warn("could not bind socket %s", socketpath);
		goto fail;
	}

	if (listen(server_socket, 1) == -1) {
		warn("could not listen on socket %s", socketpath);
		goto fail;
	}

	return server_socket;

fail:
	if (close(server_socket) == -1)
		warn("error closing server socket");

	return -1;
}
