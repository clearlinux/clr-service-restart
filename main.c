/***
 Copyright © 2018 Intel Corporation

 Author: Auke-jan H. Kok <auke-jan.h.kok@intel.com>

 This file is part of clr-service-restart

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published
 by the Free Software Foundation, either version 3 of the License,
 or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program. If not, see <https://www.gnu.org/licenses/>.

***/

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#include <systemd/sd-bus.h>

#define SLICE_DIR "/sys/fs/cgroup/systemd/system.slice"

enum needs_restart {
	NO_RESTART_NEEDED = 0,
	EXECUTABLE,
	LIBRARY
};

enum mode {
	ALLOW = 0,
	DISALLOW,
	DEFAULT
};

void usage(const char* name)
{
	fprintf(stderr,
		"Usage: %s [ <options> | allow | disallow | default \"service1\" [ \"service2\" ] ...\n"
		"Valid options:\n"
		"   -a    Consider all services, not just allowed services\n"
		"   -n    Don't actually restart services, just show what happens\n"
		, name);
	exit(EXIT_FAILURE);
}

int main(int argc, char **argv)
{
	bool noop = false;
	bool all = false;

	/* parse args */
	if (argc > 1) {
		if (argv[1][0] == '-') {
			/* runtime option handling */
			for (int i = 1; i < argc; i++) {
				if (strcmp(argv[i], "-n") == 0) {
					noop = true;
				} else if (strcmp(argv[i], "-a") == 0) {
					all = true;
				} else {
					usage(argv[0]);
				}
			}
		} else {
			/* manipulate settings */
			if (argc < 3)
				usage(argv[0]);

			enum mode m;
			if (strcmp(argv[1], "allow") == 0) {
				m = ALLOW;
			} else if (strcmp(argv[1], "disallow") == 0) {
				m = DISALLOW;
			} else if (strcmp(argv[1], "default") == 0) {
				m = DEFAULT;
			} else {
				usage(argv[0]);
			}

			for (int i = 2; i < argc; i++) {
				char *cmd;
				switch(m) {
				case ALLOW:
					if (asprintf(&cmd, "/bin/ln -sf %s /etc/clr-service-restart/%s",
							argv[i], argv[i]) < 0) {
						perror("asprintf");
						exit(EXIT_FAILURE);
					}
					break;
				case DISALLOW:
					if (asprintf(&cmd, "/bin/ln -sf /dev/null /etc/clr-service-restart/%s",
							argv[i]) < 0) {
						perror("asprintf");
						exit(EXIT_FAILURE);
					}
					break;
				case DEFAULT:
					if (asprintf(&cmd, "/bin/rm -f /etc/clr-service-restart/%s",
							argv[i]) < 0) {
						perror("asprintf");
						exit(EXIT_FAILURE);
					}
					break;
				}

				fprintf(stderr, "%s\n", cmd);
				if (system(cmd) != 0)
					exit(EXIT_FAILURE);
			}

			exit(EXIT_SUCCESS);
		}
	}

	/* do restarts */
	DIR *d = opendir(SLICE_DIR);
	if (!d) {
		perror("opendir()");
		exit(EXIT_FAILURE);
	}
	/* Loop over all the units in the system slice */
	for (;;) {
		char *n;

		struct dirent *e = readdir(d);
		if (!e)
			break;

		/* filter out stuff we need to skip */
		if (e->d_name[0] == '.')
			continue;

		/* must end in '.service' */
		size_t l = strlen(e->d_name);
		if (l <= 8)
			continue;
		if (strncmp(e->d_name + l - 8, ".service", 8))
			continue;

		if (all)
			goto nofilter;

		/* check /etc/clr-service-restart to see if it's disallowed or allowed */
		char *af;
		char buf[PATH_MAX];
		if (asprintf(&af, "/etc/clr-service-restart/%s", e->d_name) < 1) {
			perror("asprintf()");
			exit(EXIT_FAILURE);
		}
		ssize_t al = readlink(af, buf, sizeof(buf));
		if (al <= 0) {
			if (errno != ENOENT) {
				perror("readlink");
				exit(EXIT_FAILURE);
			} else {
				/* check /usr/share/clr-service-restart to see if it's allowed */
				if (asprintf(&af, "/usr/share/clr-service-restart/%s", e->d_name) < 1) {
					perror("asprintf()");
					exit(EXIT_FAILURE);
				}
				ssize_t al = readlink(af, buf, sizeof(buf));
				if (al <= 0) {
					if (errno != ENOENT) {
						perror("readlink");
						exit(EXIT_FAILURE);
					} else {
						/* we're not allowed to restart this unit */
						continue;
					}
				} else {
					buf[al] = '\0';
					if (strcmp(buf, "/dev/null") == 0) {
						/* not allowed */
						continue;
					}
				}
			}
		} else {
			buf[al] = '\0';
			if (strcmp(buf, "/dev/null") == 0) {
				/* not allowed to restart this unit */
				continue;
			}
		}

nofilter:
		/* open the tasks file */
		if (asprintf(&n, SLICE_DIR "/%s/tasks", e->d_name) < 1) {
			perror("asprintf()");
			exit(EXIT_FAILURE);
		}
		FILE *f = fopen(n, "r");
		if (!f) {
			/* if `tasks` file disappears, we don't care */
			if (errno == ENOENT) {
				free(n);
				continue;
			}
			perror(n);
			exit(EXIT_FAILURE);
		}
		free(n);

		enum needs_restart r = NO_RESTART_NEEDED;
		/* Get all the task IDs for this unit */
		for (;;) {
			int task;
			if (fscanf(f, "%d\n", &task) != 1)
				break;

			/* inspect task */
			char *exe;
			if (asprintf(&exe, "/proc/%d/exe", task) < 0) {
				perror("asprintf");
				exit(EXIT_FAILURE);
			}
			ssize_t rl = readlink(exe, buf, sizeof(buf));
			free(exe);
			if (rl <= 0) {
				perror("readlink");
				exit(EXIT_FAILURE);
			}
			buf[rl] = '\0';

			if (rl > 9) {
				if (strncmp(buf + rl - 9, "(deleted)", 9) == 0) {
					r = EXECUTABLE;
					break;
				}
			}

			/* check maps */
			char *maps;
			if (asprintf(&maps, "/proc/%d/maps", task) < 0) {
				perror("asprintf");
				exit(EXIT_FAILURE);
			}
			FILE *f = fopen(maps, "r");
			if (!f) {
				perror(maps);
				exit(EXIT_FAILURE);
			}
			free(maps);

			for (;;) {
				if (!fgets(buf, sizeof(buf), f)) 
					break;
				size_t bl = strlen(buf);
				/* minimum needed length to fit /usr/ and (deleted) */
				if (bl < 74 + 5 + 9)
					continue;
				/* ignore stuff outside /usr */
				if (strncmp(buf + 73, "/usr/", 5))
					continue;
				if (strncmp(buf + bl - 9 - 1, "(deleted)", 9))
					continue;

				/* looks like a hit */
				r = LIBRARY;
				break;
			}
			fclose(f);
			if (r)
				break;
		}

		if (r) {
			if (r == LIBRARY) {
				fprintf(stderr, "%s: needs a restart (a library dependency was updated)\n", e->d_name);
			} else {
				fprintf(stderr, "%s: needs a restart (the binary was updated)\n", e->d_name);
			}

			/* notify */
			/* do the restart */
			char *cmd;
			if (asprintf(&cmd, "/usr/bin/systemctl --no-ask-password try-restart %s", e->d_name) < 0) {
				perror("asprintf");
				exit(EXIT_FAILURE);
			}
			if (noop) {
				fprintf(stderr, "%s\n", cmd);
			} else {
				int r = system(cmd);
				if (r != 0) {
					fprintf(stderr, "Failed to restart: %s (systemctl returned error code: %d)\n",
						e->d_name, r);
					/* insert a telemetry event here */
				}
			}
		}
		fclose(f);

	}
	closedir(d);
}
