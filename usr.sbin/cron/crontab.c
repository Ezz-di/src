/*	$OpenBSD: crontab.c,v 1.83 2015/11/06 23:47:42 millert Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include <bitstring.h>		/* for structs.h */
#include <err.h>
#include <errno.h>
#include <locale.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "pathnames.h"
#include "macros.h"
#include "structs.h"
#include "funcs.h"
#include "globals.h"

#define NHEADER_LINES 3

enum opt_t	{ opt_unknown, opt_list, opt_delete, opt_edit, opt_replace };

static	pid_t		Pid;
static	gid_t		crontab_gid;
static	gid_t		user_gid;
static	char		User[MAX_UNAME], RealUser[MAX_UNAME];
static	char		Filename[MAX_FNAME], TempFilename[MAX_FNAME];
static	FILE		*NewCrontab;
static	int		CheckErrorCount;
static	enum opt_t	Option;
static	struct passwd	*pw;
int			editit(const char *);
static	void		list_cmd(void),
			delete_cmd(void),
			edit_cmd(void),
			check_error(const char *),
			parse_args(int c, char *v[]),
			copy_crontab(FILE *, FILE *),
			die(int);
static	int		replace_cmd(void);

static void
usage(const char *msg)
{
	fprintf(stderr, "%s: usage error: %s\n", __progname, msg);
	fprintf(stderr, "usage: %s [-u user] file\n", __progname);
	fprintf(stderr, "       %s [-e | -l | -r] [-u user]\n", __progname);
	fprintf(stderr,
	    "\t\t(default operation is replace, per 1003.2)\n"
	    "\t-e\t(edit user's crontab)\n"
	    "\t-l\t(list user's crontab)\n"
	    "\t-r\t(delete user's crontab)\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	int exitstatus;

	Pid = getpid();
	user_gid = getgid();
	crontab_gid = getegid();

	if (pledge("stdio rpath wpath cpath fattr getpw unix flock id proc exec",
	    NULL) == -1) {
		perror("pledge");
		exit(EXIT_FAILURE);
	}

	setlocale(LC_ALL, "");

	setvbuf(stderr, NULL, _IOLBF, 0);
	parse_args(argc, argv);		/* sets many globals, opens a file */
	set_cron_cwd();
	if (!allowed(RealUser, CRON_ALLOW, CRON_DENY)) {
		fprintf(stderr,
			"You (%s) are not allowed to use this program (%s)\n",
			User, __progname);
		fprintf(stderr, "See crontab(1) for more information\n");
		log_it(RealUser, Pid, "AUTH", "crontab command not allowed");
		exit(EXIT_FAILURE);
	}
	exitstatus = EXIT_SUCCESS;
	switch (Option) {
	case opt_list:
		list_cmd();
		break;
	case opt_delete:
		delete_cmd();
		break;
	case opt_edit:
		edit_cmd();
		break;
	case opt_replace:
		if (replace_cmd() < 0)
			exitstatus = EXIT_FAILURE;
		break;
	default:
		exitstatus = EXIT_FAILURE;
		break;
	}
	exit(exitstatus);
	/*NOTREACHED*/
}

static void
parse_args(int argc, char *argv[])
{
	int argch;

	if (!(pw = getpwuid(getuid()))) {
		fprintf(stderr, "%s: your UID isn't in the passwd file.\n",
			__progname);
		fprintf(stderr, "bailing out.\n");
		exit(EXIT_FAILURE);
	}
	if (strlen(pw->pw_name) >= sizeof User) {
		fprintf(stderr, "username too long\n");
		exit(EXIT_FAILURE);
	}
	strlcpy(User, pw->pw_name, sizeof(User));
	strlcpy(RealUser, User, sizeof(RealUser));
	Filename[0] = '\0';
	Option = opt_unknown;
	while ((argch = getopt(argc, argv, "u:ler")) != -1) {
		switch (argch) {
		case 'u':
			if (getuid() != 0) {
				fprintf(stderr,
					"must be privileged to use -u\n");
				exit(EXIT_FAILURE);
			}
			if (!(pw = getpwnam(optarg))) {
				fprintf(stderr, "%s:  user `%s' unknown\n",
					__progname, optarg);
				exit(EXIT_FAILURE);
			}
			if (strlcpy(User, optarg, sizeof User) >= sizeof User)
				usage("username too long");
			break;
		case 'l':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_list;
			break;
		case 'r':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_delete;
			break;
		case 'e':
			if (Option != opt_unknown)
				usage("only one operation permitted");
			Option = opt_edit;
			break;
		default:
			usage("unrecognized option");
		}
	}

	endpwent();

	if (Option != opt_unknown) {
		if (argv[optind] != NULL)
			usage("no arguments permitted after this option");
	} else {
		if (argv[optind] != NULL) {
			Option = opt_replace;
			if (strlcpy(Filename, argv[optind], sizeof Filename)
			    >= sizeof Filename)
				usage("filename too long");
		} else
			usage("file name must be specified for replace");
	}

	if (Option == opt_replace) {
		/* we have to open the file here because we're going to
		 * chdir(2) into /var/cron before we get around to
		 * reading the file.
		 */
		if (!strcmp(Filename, "-"))
			NewCrontab = stdin;
		else {
			/* relinquish the setgid status of the binary during
			 * the open, lest nonroot users read files they should
			 * not be able to read.  we can't use access() here
			 * since there's a race condition.  thanks go out to
			 * Arnt Gulbrandsen <agulbra@pvv.unit.no> for spotting
			 * the race.
			 */

			if (setegid(user_gid) < 0) {
				perror("setegid(user_gid)");
				exit(EXIT_FAILURE);
			}
			if (!(NewCrontab = fopen(Filename, "r"))) {
				perror(Filename);
				exit(EXIT_FAILURE);
			}
			if (setegid(crontab_gid) < 0) {
				perror("setegid(crontab_gid)");
				exit(EXIT_FAILURE);
			}
		}
	}
}

static void
list_cmd(void)
{
	char n[MAX_FNAME];
	FILE *f;

	log_it(RealUser, Pid, "LIST", User);
	if (snprintf(n, sizeof n, "%s/%s", SPOOL_DIR, User) >= sizeof(n)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}
	if (!(f = fopen(n, "r"))) {
		if (errno == ENOENT)
			fprintf(stderr, "no crontab for %s\n", User);
		else
			perror(n);
		exit(EXIT_FAILURE);
	}

	/* file is open. copy to stdout, close.
	 */
	Set_LineNum(1)

	copy_crontab(f, stdout);
	fclose(f);
}

static void
delete_cmd(void)
{
	char n[MAX_FNAME];

	log_it(RealUser, Pid, "DELETE", User);
	if (snprintf(n, sizeof n, "%s/%s", SPOOL_DIR, User) >= sizeof(n)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}
	if (unlink(n) != 0) {
		if (errno == ENOENT)
			fprintf(stderr, "no crontab for %s\n", User);
		else
			perror(n);
		exit(EXIT_FAILURE);
	}
	poke_daemon(SPOOL_DIR, RELOAD_CRON);
}

static void
check_error(const char *msg)
{
	CheckErrorCount++;
	fprintf(stderr, "\"%s\":%d: %s\n", Filename, LineNumber-1, msg);
}

static void
edit_cmd(void)
{
	char n[MAX_FNAME], q[MAX_TEMPSTR];
	FILE *f;
	int t;
	struct stat statbuf, xstatbuf;
	struct timespec ts[2];

	log_it(RealUser, Pid, "BEGIN EDIT", User);
	if (snprintf(n, sizeof n, "%s/%s", SPOOL_DIR, User) >= sizeof(n)) {
		fprintf(stderr, "path too long\n");
		exit(EXIT_FAILURE);
	}
	if (!(f = fopen(n, "r"))) {
		if (errno != ENOENT) {
			perror(n);
			exit(EXIT_FAILURE);
		}
		fprintf(stderr, "no crontab for %s - using an empty one\n",
			User);
		if (!(f = fopen(_PATH_DEVNULL, "r"))) {
			perror(_PATH_DEVNULL);
			exit(EXIT_FAILURE);
		}
	}

	if (fstat(fileno(f), &statbuf) < 0) {
		perror("fstat");
		goto fatal;
	}
	ts[0] = statbuf.st_atim;
	ts[1] = statbuf.st_mtim;

	/* Turn off signals. */
	(void)signal(SIGHUP, SIG_IGN);
	(void)signal(SIGINT, SIG_IGN);
	(void)signal(SIGQUIT, SIG_IGN);

	if (snprintf(Filename, sizeof Filename, "%scrontab.XXXXXXXXXX",
	    _PATH_TMP) >= sizeof(Filename)) {
		fprintf(stderr, "path too long\n");
		goto fatal;
	}
	t = mkstemp(Filename);
	if (t == -1) {
		perror(Filename);
		goto fatal;
	}
	if (!(NewCrontab = fdopen(t, "r+"))) {
		perror("fdopen");
		goto fatal;
	}

	Set_LineNum(1)

	copy_crontab(f, NewCrontab);
	fclose(f);
	if (fflush(NewCrontab) < 0) {
		perror(Filename);
		exit(EXIT_FAILURE);
	}
	(void)futimens(t, ts);
 again:
	rewind(NewCrontab);
	if (ferror(NewCrontab)) {
		fprintf(stderr, "%s: error while writing new crontab to %s\n",
			__progname, Filename);
 fatal:
		unlink(Filename);
		exit(EXIT_FAILURE);
	}

	/* we still have the file open.  editors will generally rewrite the
	 * original file rather than renaming/unlinking it and starting a
	 * new one; even backup files are supposed to be made by copying
	 * rather than by renaming.  if some editor does not support this,
	 * then don't use it.  the security problems are more severe if we
	 * close and reopen the file around the edit.
	 */
	if (editit(Filename) == -1) {
		warn("error starting editor");
		goto fatal;
	}

	if (fstat(t, &statbuf) < 0) {
		perror("fstat");
		goto fatal;
	}
	if (timespeccmp(&ts[1], &statbuf.st_mtim, ==)) {
		if (lstat(Filename, &xstatbuf) == 0 &&
		    statbuf.st_ino != xstatbuf.st_ino) {
			fprintf(stderr, "%s: crontab temp file moved, editor "
			   "may create backup files improperly\n", __progname);
		}
		fprintf(stderr, "%s: no changes made to crontab\n",
			__progname);
		goto remove;
	}
	fprintf(stderr, "%s: installing new crontab\n", __progname);
	switch (replace_cmd()) {
	case 0:
		break;
	case -1:
		for (;;) {
			printf("Do you want to retry the same edit? ");
			fflush(stdout);
			q[0] = '\0';
			if (fgets(q, sizeof q, stdin) == NULL) {
				putchar('\n');
				goto abandon;
			}
			switch (q[0]) {
			case 'y':
			case 'Y':
				goto again;
			case 'n':
			case 'N':
				goto abandon;
			default:
				fprintf(stderr, "Enter Y or N\n");
			}
		}
		/*NOTREACHED*/
	case -2:
	abandon:
		fprintf(stderr, "%s: edits left in %s\n",
			__progname, Filename);
		goto done;
	default:
		fprintf(stderr, "%s: panic: bad switch() in replace_cmd()\n",
			__progname);
		goto fatal;
	}
 remove:
	unlink(Filename);
 done:
	log_it(RealUser, Pid, "END EDIT", User);
}

/* returns	0	on success
 *		-1	on syntax error
 *		-2	on install error
 */
static int
replace_cmd(void)
{
	char n[MAX_FNAME], envstr[MAX_ENVSTR];
	FILE *tmp;
	int ch, eof, fd;
	int error = 0;
	entry *e;
	time_t now = time(NULL);
	char **envp = env_init();

	if (envp == NULL) {
		fprintf(stderr, "%s: Cannot allocate memory.\n", __progname);
		return (-2);
	}
	if (snprintf(TempFilename, sizeof TempFilename, "%s/tmp.XXXXXXXXX",
	    SPOOL_DIR) >= sizeof(TempFilename)) {
		TempFilename[0] = '\0';
		fprintf(stderr, "path too long\n");
		return (-2);
	}
	if ((fd = mkstemp(TempFilename)) == -1 || !(tmp = fdopen(fd, "w+"))) {
		perror(TempFilename);
		if (fd != -1) {
			close(fd);
			unlink(TempFilename);
		}
		TempFilename[0] = '\0';
		return (-2);
	}

	(void) signal(SIGHUP, die);
	(void) signal(SIGINT, die);
	(void) signal(SIGQUIT, die);

	/* write a signature at the top of the file.
	 *
	 * VERY IMPORTANT: make sure NHEADER_LINES agrees with this code.
	 */
	fprintf(tmp, "# DO NOT EDIT THIS FILE - edit the master and reinstall.\n");
	fprintf(tmp, "# (%s installed on %-24.24s)\n", Filename, ctime(&now));
	fprintf(tmp, "# (Cron version %s)\n", CRON_VERSION);

	/* copy the crontab to the tmp
	 */
	rewind(NewCrontab);
	Set_LineNum(1)
	while (EOF != (ch = get_char(NewCrontab)))
		putc(ch, tmp);
	ftruncate(fileno(tmp), ftello(tmp));	/* XXX redundant with "w+"? */
	fflush(tmp);  rewind(tmp);

	if (ferror(tmp)) {
		fprintf(stderr, "%s: error while writing new crontab to %s\n",
			__progname, TempFilename);
		fclose(tmp);
		error = -2;
		goto done;
	}

	/* check the syntax of the file being installed.
	 */

	/* BUG: was reporting errors after the EOF if there were any errors
	 * in the file proper -- kludged it by stopping after first error.
	 *		vix 31mar87
	 */
	Set_LineNum(1 - NHEADER_LINES)
	CheckErrorCount = 0;  eof = FALSE;
	while (!CheckErrorCount && !eof) {
		switch (load_env(envstr, tmp)) {
		case -1:
			/* check for data before the EOF */
			if (envstr[0] != '\0') {
				Set_LineNum(LineNumber + 1);
				check_error("premature EOF");
			}
			eof = TRUE;
			break;
		case FALSE:
			e = load_entry(tmp, check_error, pw, envp);
			if (e)
				free_entry(e);
			break;
		case TRUE:
			break;
		}
	}

	if (CheckErrorCount != 0) {
		fprintf(stderr, "errors in crontab file, can't install.\n");
		fclose(tmp);
		error = -1;
		goto done;
	}

	if (fchown(fileno(tmp), pw->pw_uid, -1) < 0) {
		perror("fchown");
		fclose(tmp);
		error = -2;
		goto done;
	}

	if (fclose(tmp) == EOF) {
		perror("fclose");
		error = -2;
		goto done;
	}

	if (snprintf(n, sizeof n, "%s/%s", SPOOL_DIR, User) >= sizeof(n)) {
		fprintf(stderr, "path too long\n");
		error = -2;
		goto done;
	}
	if (rename(TempFilename, n)) {
		fprintf(stderr, "%s: error renaming %s to %s\n",
			__progname, TempFilename, n);
		perror("rename");
		error = -2;
		goto done;
	}
	TempFilename[0] = '\0';
	log_it(RealUser, Pid, "REPLACE", User);

	poke_daemon(SPOOL_DIR, RELOAD_CRON);

done:
	(void) signal(SIGHUP, SIG_DFL);
	(void) signal(SIGINT, SIG_DFL);
	(void) signal(SIGQUIT, SIG_DFL);
	if (TempFilename[0]) {
		(void) unlink(TempFilename);
		TempFilename[0] = '\0';
	}
	return (error);
}

/*
 * Execute an editor on the specified pathname, which is interpreted
 * from the shell.  This means flags may be included.
 *
 * Returns -1 on error, or the exit value on success.
 */
int
editit(const char *pathname)
{
	char *argp[] = {"sh", "-c", NULL, NULL}, *ed, *p;
	sig_t sighup, sigint, sigquit, sigchld;
	pid_t pid;
	int saved_errno, st, ret = -1;

	ed = getenv("VISUAL");
	if (ed == NULL || ed[0] == '\0')
		ed = getenv("EDITOR");
	if (ed == NULL || ed[0] == '\0')
		ed = _PATH_VI;
	if (asprintf(&p, "%s %s", ed, pathname) == -1)
		return (-1);
	argp[2] = p;

	sighup = signal(SIGHUP, SIG_IGN);
	sigint = signal(SIGINT, SIG_IGN);
	sigquit = signal(SIGQUIT, SIG_IGN);
	sigchld = signal(SIGCHLD, SIG_DFL);
	if ((pid = fork()) == -1)
		goto fail;
	if (pid == 0) {
		setgid(getgid());
		setuid(getuid());
		execv(_PATH_BSHELL, argp);
		_exit(127);
	}
	while (waitpid(pid, &st, 0) == -1)
		if (errno != EINTR)
			goto fail;
	if (!WIFEXITED(st))
		errno = EINTR;
	else
		ret = WEXITSTATUS(st);

 fail:
	saved_errno = errno;
	(void)signal(SIGHUP, sighup);
	(void)signal(SIGINT, sigint);
	(void)signal(SIGQUIT, sigquit);
	(void)signal(SIGCHLD, sigchld);
	free(p);
	errno = saved_errno;
	return (ret);
}

static void
die(int x)
{
	if (TempFilename[0])
		(void) unlink(TempFilename);
	_exit(EXIT_FAILURE);
}

static void
copy_crontab(FILE *f, FILE *out)
{
	int ch, x;

	/* ignore the top few comments since we probably put them there.
	 */
	x = 0;
	while (EOF != (ch = get_char(f))) {
		if ('#' != ch) {
			putc(ch, out);
			break;
		}
		while (EOF != (ch = get_char(f)))
			if (ch == '\n')
				break;
		if (++x >= NHEADER_LINES)
			break;
	}

	/* copy out the rest of the crontab (if any)
	 */
	if (EOF != ch)
		while (EOF != (ch = get_char(f)))
			putc(ch, out);
}
