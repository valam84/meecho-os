/*	$NetBSD: histedit.c,v 1.47 2014/06/18 18:17:30 christos Exp $	*/

/*-
 * Copyright (c) 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Kenneth Almquist.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)histedit.c	8.2 (Berkeley) 5/4/95";
#else
__RCSID("$NetBSD: histedit.c,v 1.47 2014/06/18 18:17:30 christos Exp $");
#endif
#endif /* not lint */

#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <paths.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
/*
 * Editline and history functions (and glue).
 */
#include "shell.h"
#include "parser.h"
#include "var.h"
#include "options.h"
#include "builtins.h"
#include "main.h"
#include "output.h"
#include "mystring.h"
#include "myhistedit.h"
#include "error.h"
#include "alias.h"
#ifndef SMALL
#include "eval.h"
#include "memalloc.h"

#define MAXHISTLOOPS	4	/* max recursions through fc */
#define DEFEDITOR	"ed"	/* default editor *should* be $EDITOR */

History *hist;	/* history cookie */
EditLine *el;	/* editline cookie */
int displayhist;
static FILE *el_in, *el_out;
unsigned char _el_fn_complete(EditLine *, int);

STATIC const char *fc_replace(const char *, char *, char *);

#ifdef DEBUG
extern FILE *tracefile;
#endif

/*
 * <tab> completion.
 *
 * libedit completes file names and nothing else, and that is all NetBSD's
 * sh ever bound <tab> to.  Here the first word of a command is completed
 * from the builtins and from PATH instead: on a machine whose only user
 * interface is this shell, the names of its programs are the first thing
 * one needs to be told.  Every other word, and a first word with a slash in
 * it, still goes to libedit.  Several matches insert what they share; when
 * there is nothing left to share, <tab> lists them.
 */

/* What libedit takes for the end of a word (filecomplete.c). */
static const char word_break[] = " \t\n\"\\'`@$><=;|&{(";

/* More candidates than this and the list asks before scrolling. */
#define QUERY_ITEMS	100

struct cmdlist {
	char **name;
	size_t n, cap;
};

static void
cmdlist_add(struct cmdlist *l, const char *name)
{
	char **v;
	size_t cap;

	if (l->n + 1 >= l->cap) {
		cap = l->cap ? l->cap * 2 : 64;
		if ((v = realloc(l->name, cap * sizeof(*v))) == NULL)
			return;
		l->name = v;
		l->cap = cap;
	}
	if ((l->name[l->n] = strdup(name)) != NULL)
		l->n++;
}

static void
cmdlist_free(struct cmdlist *l)
{
	size_t i;

	for (i = 0; i < l->n; i++)
		free(l->name[i]);
	free(l->name);
}

static int
cmdlist_cmp(const void *a, const void *b)
{
	return strcmp(*(char *const *)a, *(char *const *)b);
}

/*
 * Every command whose name begins with text: the builtins, then each
 * directory of PATH in order.  Sorted and without repeats: the same name in
 * two directories is one command as far as the list is concerned.
 */
static void
command_matches(const char *text, struct cmdlist *l)
{
	const struct builtincmd *bp;
	const char *path, *end;
	char dir[PATH_MAX], full[PATH_MAX];
	size_t len, dlen, i, j;
	DIR *dp;
	struct dirent *de;
	struct stat st;

	len = strlen(text);
	for (bp = builtincmd; bp->name != NULL; bp++)
		if (strncmp(bp->name, text, len) == 0)
			cmdlist_add(l, bp->name);

	for (path = pathval(); path != NULL; path = end ? end + 1 : NULL) {
		end = strchr(path, ':');
		dlen = end ? (size_t)(end - path) : strlen(path);
		if (dlen == 0)
			strcpy(dir, ".");
		else
			snprintf(dir, sizeof(dir), "%.*s", (int)dlen, path);
		if ((dp = opendir(dir)) == NULL)
			continue;
		while ((de = readdir(dp)) != NULL) {
			/* An empty word does not show hidden names. */
			if (de->d_name[0] == '.' && len == 0)
				continue;
			if (strncmp(de->d_name, text, len) != 0)
				continue;
			snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
			if (stat(full, &st) == 0 && S_ISREG(st.st_mode) &&
			    (st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH)))
				cmdlist_add(l, de->d_name);
		}
		closedir(dp);
	}

	qsort(l->name, l->n, sizeof(*l->name), cmdlist_cmp);
	for (i = j = 0; i < l->n; i++) {
		if (j > 0 && strcmp(l->name[j - 1], l->name[i]) == 0) {
			free(l->name[i]);
			continue;
		}
		l->name[j++] = l->name[i];
	}
	l->n = j;
}

/*
 * Print the candidates in columns under the command line.  The caller
 * returns CC_REDISPLAY so that libedit draws the prompt and the line again
 * below them.
 */
static void
list_matches(struct cmdlist *l)
{
	struct winsize ws;
	size_t maxlen, width, cols, rows, r, c, i;
	int screen = 80;
	char ch;

	if (l->n > QUERY_ITEMS) {
		fprintf(el_out, "\nDisplay all %zu possibilities? (y or n) ",
		    l->n);
		fflush(el_out);
		if (read(fileno(el_in), &ch, 1) != 1 || ch != 'y') {
			fputc('\n', el_out);
			fflush(el_out);
			return;
		}
	}

	if (ioctl(fileno(el_out), TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
		screen = ws.ws_col;
	for (maxlen = 0, i = 0; i < l->n; i++)
		if (strlen(l->name[i]) > maxlen)
			maxlen = strlen(l->name[i]);
	width = maxlen + 2;
	cols = (size_t)screen / width;
	if (cols == 0)
		cols = 1;
	rows = (l->n + cols - 1) / cols;

	fputc('\n', el_out);
	for (r = 0; r < rows; r++) {
		for (c = 0; c < cols; c++) {
			i = c * rows + r;
			if (i >= l->n)
				break;
			if (i + rows < l->n)
				fprintf(el_out, "%-*s", (int)width, l->name[i]);
			else
				fputs(l->name[i], el_out);
		}
		fputc('\n', el_out);
	}
	fflush(el_out);
}

/*
 * Does the word starting at word begin a command?  At the start of the
 * line, or after something that ends a command and starts another.
 */
static int
command_position(const char *buf, const char *word)
{
	while (word > buf && (word[-1] == ' ' || word[-1] == '\t'))
		word--;
	return word == buf || strchr(";|&(`{", word[-1]) != NULL;
}

static unsigned char
sh_complete(EditLine *e, int ch)
{
	const LineInfo *li;
	const char *word;
	char *text, saved;
	struct cmdlist l;
	size_t len, common, k;
	unsigned char ret;

	li = el_line(e);
	word = li->cursor;
	while (word > li->buffer && strchr(word_break, word[-1]) == NULL)
		word--;
	len = (size_t)(li->cursor - word);
	if (memchr(word, '/', len) != NULL ||
	    !command_position(li->buffer, word))
		return _el_fn_complete(e, ch);

	/* li points into libedit's scratch space; copy before editing. */
	if ((text = malloc(len + 1)) == NULL)
		return CC_ERROR;
	memcpy(text, word, len);
	text[len] = '\0';
	memset(&l, 0, sizeof(l));
	command_matches(text, &l);
	free(text);

	if (l.n == 0) {
		el_beep(e);
		cmdlist_free(&l);
		return CC_NORM;
	}

	/* What every match shares; the list is sorted, so ends suffice. */
	common = strlen(l.name[0]);
	for (k = 0; k < common && l.name[0][k] == l.name[l.n - 1][k]; k++)
		continue;
	common = k;

	if (l.n == 1) {
		el_insertstr(e, l.name[0] + len);
		el_insertstr(e, " ");
		ret = CC_REFRESH;
	} else if (common > len) {
		saved = l.name[0][common];
		l.name[0][common] = '\0';
		el_insertstr(e, l.name[0] + len);
		l.name[0][common] = saved;
		ret = CC_REFRESH;
	} else {
		list_matches(&l);
		ret = CC_REDISPLAY;
	}
	cmdlist_free(&l);
	return ret;
}

/*
 * Set history and editing status.  Called whenever the status may
 * have changed (figures out what to do).
 */
void
histedit(void)
{
	FILE *el_err;

#define editing (Eflag || Vflag)

	if (iflag == 1) {
		if (!hist) {
			/*
			 * turn history on
			 */
			INTOFF;
			hist = history_init();
			INTON;

			if (hist != NULL)
				sethistsize(histsizeval());
			else
				out2str("sh: can't initialize history\n");
		}
		if (editing && !el && isatty(0)) { /* && isatty(2) ??? */
			/*
			 * turn editing on
			 */
			char *term, *shname;

			INTOFF;
			if (el_in == NULL)
				el_in = fdopen(0, "r");
			if (el_out == NULL)
				el_out = fdopen(2, "w");
			if (el_in == NULL || el_out == NULL)
				goto bad;
			el_err = el_out;
#if DEBUG
			if (tracefile)
				el_err = tracefile;
#endif
			term = lookupvar("TERM");
			if (term)
				setenv("TERM", term, 1);
			else
				unsetenv("TERM");
			shname = arg0;
			if (shname[0] == '-')
				shname++;
			el = el_init(shname, el_in, el_out, el_err);
			if (el != NULL) {
				if (hist)
					el_set(el, EL_HIST, history, hist);
				el_set(el, EL_PROMPT, getprompt);
				el_set(el, EL_SIGNAL, 1);
				el_set(el, EL_ALIAS_TEXT, alias_text, NULL);
				el_set(el, EL_ADDFN, "rl-complete",
				    "ReadLine compatible completion function",
				    _el_fn_complete);
				el_set(el, EL_ADDFN, "sh-complete",
				    "Command or file name completion",
				    sh_complete);
			} else {
bad:
				out2str("sh: can't initialize editing\n");
			}
			INTON;
		} else if (!editing && el) {
			INTOFF;
			el_end(el);
			el = NULL;
			INTON;
		}
		if (el) {
			el_source(el, NULL);
			if (Vflag)
				el_set(el, EL_EDITOR, "vi");
			else if (Eflag)
				el_set(el, EL_EDITOR, "emacs");
			el_set(el, EL_BIND, "^I",
			    tabcomplete ? "sh-complete" : "ed-insert", NULL);
		}
	} else {
		INTOFF;
		if (el) {	/* no editing if not interactive */
			el_end(el);
			el = NULL;
		}
		if (hist) {
			history_end(hist);
			hist = NULL;
		}
		INTON;
	}
}


void
sethistsize(const char *hs)
{
	int histsize;
	HistEvent he;

	if (hist != NULL) {
		if (hs == NULL || *hs == '\0' ||
		   (histsize = atoi(hs)) < 0)
			histsize = 100;
		history(hist, &he, H_SETSIZE, histsize);
		history(hist, &he, H_SETUNIQUE, 1);
	}
}

void
setterm(const char *term)
{
	if (el != NULL && term != NULL)
		if (el_set(el, EL_TERMINAL, term) != 0) {
			outfmt(out2, "sh: Can't set terminal type %s\n", term);
			outfmt(out2, "sh: Using dumb terminal settings.\n");
		}
}

int
inputrc(int argc, char **argv)
{
	if (argc != 2) {
		out2str("usage: inputrc file\n");
		return 1;
	}
	if (el != NULL) {
		if (el_source(el, argv[1])) {
			out2str("inputrc: failed\n");
			return 1;
		} else
			return 0;
	} else {
		out2str("sh: inputrc ignored, not editing\n");
		return 1;
	}
}

/*
 *  This command is provided since POSIX decided to standardize
 *  the Korn shell fc command.  Oh well...
 */
int
histcmd(volatile int argc, char ** volatile argv)
{
	int ch;
	const char * volatile editor = NULL;
	HistEvent he;
	/*
	 * Everything live across the setjmp() below has to be volatile, or
	 * the compiler may keep it in a register that longjmp() restores to
	 * its value at setjmp() time.  Some of these were already marked;
	 * the rest are the ones the compiler has since learned to see.
	 */
	volatile int lflg = 0;
	volatile int nflg = 0, rflg = 0, sflg = 0;
	int i, retval;
	const char *firststr, *laststr;
	int first, last, direction;
	char * volatile pat = NULL;	/* ksh "fc old=new" crap */
	char *repl;
	static int active = 0;
	struct jmploc jmploc;
	struct jmploc *volatile savehandler;
	char editfile[MAXPATHLEN + 1];
	FILE * volatile efp;
#ifdef __GNUC__
	repl = NULL;	/* XXX gcc4 */
	efp = NULL;	/* XXX gcc4 */
#endif

	if (hist == NULL)
		error("history not active");

	if (argc == 1)
		error("missing history argument");

	optreset = 1; optind = 1; /* initialize getopt */
	while (not_fcnumber(argv[optind]) &&
	      (ch = getopt(argc, argv, ":e:lnrs")) != -1)
		switch ((char)ch) {
		case 'e':
			editor = optionarg;
			break;
		case 'l':
			lflg = 1;
			break;
		case 'n':
			nflg = 1;
			break;
		case 'r':
			rflg = 1;
			break;
		case 's':
			sflg = 1;
			break;
		case ':':
			error("option -%c expects argument", optopt);
			/* NOTREACHED */
		case '?':
		default:
			error("unknown option: -%c", optopt);
			/* NOTREACHED */
		}
	argc -= optind, argv += optind;

	/*
	 * If executing...
	 */
	if (lflg == 0 || editor || sflg) {
		lflg = 0;	/* ignore */
		editfile[0] = '\0';
		/*
		 * Catch interrupts to reset active counter and
		 * cleanup temp files.
		 */
		savehandler = handler;
		if (setjmp(jmploc.loc)) {
			active = 0;
			if (*editfile)
				unlink(editfile);
			handler = savehandler;
			longjmp(handler->loc, 1);
		}
		handler = &jmploc;
		if (++active > MAXHISTLOOPS) {
			active = 0;
			displayhist = 0;
			error("called recursively too many times");
		}
		/*
		 * Set editor.
		 */
		if (sflg == 0) {
			if (editor == NULL &&
			    (editor = bltinlookup("FCEDIT", 1)) == NULL &&
			    (editor = bltinlookup("EDITOR", 1)) == NULL)
				editor = DEFEDITOR;
			if (editor[0] == '-' && editor[1] == '\0') {
				sflg = 1;	/* no edit */
				editor = NULL;
			}
		}
	}

	/*
	 * If executing, parse [old=new] now
	 */
	if (lflg == 0 && argc > 0 &&
	     ((repl = strchr(argv[0], '=')) != NULL)) {
		pat = argv[0];
		*repl++ = '\0';
		argc--, argv++;
	}

	/*
	 * If -s is specified, accept only one operand
	 */
	if (sflg && argc >= 2)
		error("too many args");

	/*
	 * determine [first] and [last]
	 */
	switch (argc) {
	case 0:
		firststr = lflg ? "-16" : "-1";
		laststr = "-1";
		break;
	case 1:
		firststr = argv[0];
		laststr = lflg ? "-1" : argv[0];
		break;
	case 2:
		firststr = argv[0];
		laststr = argv[1];
		break;
	default:
		error("too many args");
		/* NOTREACHED */
	}
	/*
	 * Turn into event numbers.
	 */
	first = str_to_event(firststr, 0);
	last = str_to_event(laststr, 1);

	if (rflg) {
		i = last;
		last = first;
		first = i;
	}
	/*
	 * XXX - this should not depend on the event numbers
	 * always increasing.  Add sequence numbers or offset
	 * to the history element in next (diskbased) release.
	 */
	direction = first < last ? H_PREV : H_NEXT;

	/*
	 * If editing, grab a temp file.
	 */
	if (editor) {
		int fd;
		INTOFF;		/* easier */
		snprintf(editfile, sizeof(editfile), "%s_shXXXXXX", _PATH_TMP);
		if ((fd = mkstemp(editfile)) < 0)
			error("can't create temporary file %s", editfile);
		if ((efp = fdopen(fd, "w")) == NULL) {
			close(fd);
			error("can't allocate stdio buffer for temp");
		}
	}

	/*
	 * Loop through selected history events.  If listing or executing,
	 * do it now.  Otherwise, put into temp file and call the editor
	 * after.
	 *
	 * The history interface needs rethinking, as the following
	 * convolutions will demonstrate.
	 */
	history(hist, &he, H_FIRST);
	retval = history(hist, &he, H_NEXT_EVENT, first);
	for (;retval != -1; retval = history(hist, &he, direction)) {
		if (lflg) {
			if (!nflg)
				out1fmt("%5d ", he.num);
			out1str(he.str);
		} else {
			const char *s = pat ?
			   fc_replace(he.str, pat, repl) : he.str;

			if (sflg) {
				if (displayhist) {
					out2str(s);
				}

				evalstring(strcpy(stalloc(strlen(s) + 1), s), 0);
				if (displayhist && hist) {
					/*
					 *  XXX what about recursive and
					 *  relative histnums.
					 */
					history(hist, &he, H_ENTER, s);
				}

				break;
			} else
				fputs(s, efp);
		}
		/*
		 * At end?  (if we were to lose last, we'd sure be
		 * messed up).
		 */
		if (he.num == last)
			break;
	}
	if (editor) {
		char *editcmd;
		size_t cmdlen;

		fclose(efp);
		cmdlen = strlen(editor) + strlen(editfile) + 2;
		editcmd = stalloc(cmdlen);
		snprintf(editcmd, cmdlen, "%s %s", editor, editfile);
		evalstring(editcmd, 0);	/* XXX - should use no JC command */
		INTON;
		readcmdfile(editfile);	/* XXX - should read back - quick tst */
		unlink(editfile);
	}

	if (lflg == 0 && active > 0)
		--active;
	if (displayhist)
		displayhist = 0;
	return 0;
}

STATIC const char *
fc_replace(const char *s, char *p, char *r)
{
	char *dest;
	int plen = strlen(p);

	STARTSTACKSTR(dest);
	while (*s) {
		if (*s == *p && strncmp(s, p, plen) == 0) {
			while (*r)
				STPUTC(*r++, dest);
			s += plen;
			*p = '\0';	/* so no more matches */
		} else
			STPUTC(*s++, dest);
	}
	STACKSTRNUL(dest);
	dest = grabstackstr(dest);

	return (dest);
}

int
not_fcnumber(char *s)
{
	if (s == NULL)
		return 0;
        if (*s == '-')
                s++;
	return (!is_number(s));
}

int
str_to_event(const char *str, int last)
{
	HistEvent he;
	const char *s = str;
	int relative = 0;
	int i, retval;

	retval = history(hist, &he, H_FIRST);
	switch (*s) {
	case '-':
		relative = 1;
		/*FALLTHROUGH*/
	case '+':
		s++;
	}
	if (is_number(s)) {
		i = atoi(s);
		if (relative) {
			while (retval != -1 && i--) {
				retval = history(hist, &he, H_NEXT);
			}
			if (retval == -1)
				retval = history(hist, &he, H_LAST);
		} else {
			retval = history(hist, &he, H_NEXT_EVENT, i);
			if (retval == -1) {
				/*
				 * the notion of first and last is
				 * backwards to that of the history package
				 */
				retval = history(hist, &he,
						last ? H_FIRST : H_LAST);
			}
		}
		if (retval == -1)
			error("history number %s not found (internal error)",
			       str);
	} else {
		/*
		 * pattern
		 */
		retval = history(hist, &he, H_PREV_STR, str);
		if (retval == -1)
			error("history pattern not found: %s", str);
	}
	return (he.num);
}
#else
int
histcmd(int argc, char **argv)
{
	error("not compiled with history support");
	/* NOTREACHED */
}
int
inputrc(int argc, char **argv)
{
	error("not compiled with history support");
	/* NOTREACHED */
}
#endif
