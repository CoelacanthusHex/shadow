/*
 * Copyright (c) 2000       , International Business Machines
 *                            George Kraft IV, gk4@us.ibm.com, 03/23/2000
 * Copyright (c) 2000 - 2006, Tomasz Kłoczko
 * Copyright (c) 2007 - 2008, Nicolas François
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the copyright holders or contributors may not be used to
 *    endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT
 * HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <config.h>

#include <fcntl.h>
#include <getopt.h>
#include <grp.h>
#include <stdio.h>
#include <sys/types.h>
#ifdef USE_PAM
#include "pam_defs.h"
#endif				/* USE_PAM */
#include <pwd.h>
#include "defines.h"
#include "groupio.h"

/* Exit Status Values */

#define EXIT_SUCCESS		0	/* success */
#define EXIT_USAGE		1	/* invalid command syntax */
#define EXIT_GROUP_FILE		2	/* group file access problems */
#define EXIT_NOT_ROOT		3	/* not superuser  */
#define EXIT_NOT_EROOT		4	/* not effective superuser  */
#define EXIT_NOT_PRIMARY	5	/* not primary owner of group  */
#define EXIT_NOT_MEMBER		6	/* member of group does not exist */
#define EXIT_MEMBER_EXISTS	7	/* member of group already exists */
#define EXIT_INVALID_USER	8	/* specified user does not exist */
#define EXIT_INVALID_GROUP	9	/* specified group does not exist */

#define TRUE 1
#define FALSE 0

/*
 * Global variables
 */
static char *adduser = NULL;
static char *deluser = NULL;
static char *thisgroup = NULL;
static int purge = FALSE;
static int list = FALSE;
static int exclusive = 0;
static char *Prog;

static int isroot (void)
{
	return getuid ()? FALSE : TRUE;
}

static int isgroup (void)
{
	gid_t g = getgid ();
	struct group *grp = getgrgid (g); /* local, no need for xgetgrgid */

	return TRUE;
}

static char *whoami (void)
{
	/* local, no need for xgetgrgid */
	struct group *grp = getgrgid (getgid ());
	/* local, no need for xgetpwuid */
	struct passwd *usr = getpwuid (getuid ());

	if (0 == strcmp (usr->pw_name, grp->gr_name)) {
		return (char *) strdup (usr->pw_name);
	} else {
		return NULL;
	}
}

static void addtogroup (char *user, char **members)
{
	int i;

	for (i = 0; NULL != members[i]; i++) {
		if (0 == strcmp (user, members[i])) {
			fputs (_("Member already exists\n"), stderr);
			exit (EXIT_MEMBER_EXISTS);
		}
	}

	members = (char **) realloc (members, sizeof (char *) * (i+2));
	members[i] = user;
	members[i + 1] = NULL;
}

static void rmfromgroup (char *user, char **members)
{
	int i;
	int found = FALSE;

	i = 0;
	while (!found && NULL != members[i]) {
		if (0 == strcmp (user, members[i])) {
			found = TRUE;
		} else {
			i++;
		}
	}

	while (found && NULL != members[i]) {
		members[i] = members[i+1];
		i++;
	}

	if (!found) {
		fputs (_("Member to remove could not be found\n"), stderr);
		exit (EXIT_NOT_MEMBER);
	}
}

static void nomembers (char **members)
{
	int i;

	for (i = 0; NULL != members[i]; i++) {
		members[i] = NULL;
	}
}

static void members (char **members)
{
	int i;

	for (i = 0; NULL != members[i]; i++) {
		printf ("%s ", members[i]);

		if (NULL == members[i + 1]) {
			printf ("\n");
		} else {
			printf (" ");
		}
	}
}

static void usage (void)
{
	fputs (_("Usage: groupmems -a username | -d username | -D | -l [-g groupname]\n"), stderr);
	exit (EXIT_USAGE);
}

int main (int argc, char **argv) 
{
	int arg;
	char *name;
	struct group *grp;

#ifdef USE_PAM
	pam_handle_t *pamh = NULL;
	int retval;
#endif

	int option_index = 0;
	static struct option long_options[] = {
		{"add", required_argument, NULL, 'a'},
		{"delete", required_argument, NULL, 'd'},
		{"group", required_argument, NULL, 'g'},
		{"list", no_argument, NULL, 'l'},
		{"purge", no_argument, NULL, 'p'},
		{NULL, 0, NULL, '\0'}
	};

	/*
	 * Get my name so that I can use it to report errors.
	 */
	Prog = Basename (argv[0]);

	(void) setlocale (LC_ALL, "");
	(void) bindtextdomain (PACKAGE, LOCALEDIR);
	(void) textdomain (PACKAGE);

	while ((arg =
		getopt_long (argc, argv, "a:d:g:lp", long_options,
			     &option_index)) != EOF) {
		switch (arg) {
		case 'a':
			adduser = strdup (optarg);
			++exclusive;
			break;
		case 'd':
			deluser = strdup (optarg);
			++exclusive;
			break;
		case 'p':
			purge = TRUE;
			++exclusive;
			break;
		case 'g':
			thisgroup = strdup (optarg);
			break;
		case 'l':
			list = TRUE;
			++exclusive;
			break;
		default:
			usage ();
		}
	}

	if (exclusive > 1 || optind < argc) {
		usage ();
	}

	if (getpwnam(adduser) == NULL) {
		fprintf (stderr, _("%s: user `%s' does not exist\n")
		         Prog, adduser);
		exit (EXIT_INVALID_USERNAME);
	}

	if (!isroot () && NULL != thisgroup) {
		fputs (_("Only root can add members to different groups\n"),
		       stderr);
		exit (EXIT_NOT_ROOT);
	} else if (isroot () && NULL != thisgroup) {
		name = thisgroup;
	} else if (!isgroup ()) {
		fputs (_("Group access is required\n"), stderr);
		exit (EXIT_NOT_EROOT);
	} else if (NULL == (name = whoami ())) {
		fputs (_("Not primary owner of current group\n"), stderr);
		exit (EXIT_NOT_PRIMARY);
	}
#ifdef USE_PAM
	retval = PAM_SUCCESS;

	{
		struct passwd *pampw;
		pampw = getpwuid (getuid ()); /* local, no need for xgetpwuid */
		if (pampw == NULL) {
			retval = PAM_USER_UNKNOWN;
		}

		if (retval == PAM_SUCCESS) {
			retval = pam_start ("groupmod", pampw->pw_name,
					    &conv, &pamh);
		}
	}

	if (retval == PAM_SUCCESS) {
		retval = pam_authenticate (pamh, 0);
		if (retval != PAM_SUCCESS) {
			(void) pam_end (pamh, retval);
		}
	}

	if (retval == PAM_SUCCESS) {
		retval = pam_acct_mgmt (pamh, 0);
		if (retval != PAM_SUCCESS) {
			(void) pam_end (pamh, retval);
		}
	}

	if (retval != PAM_SUCCESS) {
		fputs (_("PAM authentication failed for\n"), stderr);
		exit (1);
	}
#endif

	if (!gr_lock ()) {
		fputs (_("Unable to lock group file\n"), stderr);
		exit (EXIT_GROUP_FILE);
	}

	if (!gr_open (O_RDWR)) {
		fputs (_("Unable to open group file\n"), stderr);
		exit (EXIT_GROUP_FILE);
	}

	grp = (struct group *) gr_locate (name);

	if (grp == NULL) {
		fprintf (stderr, _("%s: `%s' not found in /etc/group\n"),
		         Prog, name);
		exit (EXIT_READ_GROUP);
	}

	if (NULL != adduser) {
		addtogroup (adduser, grp->gr_mem);
		gr_update (grp);
	} else if (NULL != deluser) {
		rmfromgroup (deluser, grp->gr_mem);
		gr_update (grp);
	} else if (purge) {
		nomembers (grp->gr_mem);
		gr_update (grp);
	} else if (list) {
		members (grp->gr_mem);
	}

	if (!gr_close ()) {
		fputs (_("Cannot close group file\n"), stderr);
		exit (EXIT_GROUP_FILE);
	}

	gr_unlock ();

#ifdef USE_PAM
	if (retval == PAM_SUCCESS) {
		(void) pam_end (pamh, PAM_SUCCESS);
	}
#endif				/* USE_PAM */
	exit (EXIT_SUCCESS);
}
