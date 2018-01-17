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

/*
 * read a symlink, putting the result in dst.
 * - returns '-1' if the link did not exist, '0' if it did
 * - on error cancels termination of the program
 * - caller needs to free the ptr after use if '0' returned
 */
static int do_readlink(const char* src, char **dst)
{
	struct stat st;
	if (lstat(src, &st) == -1) {
		/* not an error, but report this back */
		if (errno == ENOENT)
			return -1;

		perror(src);
		exit(EXIT_FAILURE);
	}

	*dst = calloc(1, st.st_size + 1);
	ssize_t ret = readlink(src, *dst, st.st_size + 1);
	if (ret < 0) {
		perror("src");
		exit(EXIT_FAILURE);
	}
	if (ret > st.st_size) {
		perror("st.st_size");
		exit(EXIT_FAILURE);
	}

	char *end = *dst + ret;
	*end = '\0';

	return 0;
}

static void usage(const char* name)
{
	fprintf(stderr,
		"Usage: %s [ <options> | allow | disallow | default \"service1\" [ \"service2\" ] ...\n"
		"Valid options:\n"
		"   -a    Consider all services, not just allowed services\n"
		"   -n    Don't actually restart services, just show what happens\n"
		, name);
	exit(EXIT_FAILURE);
}

/*
 * Find the unit's location by asking systemctl.
 * - Returns 0 on success. Terminatest the program on failure
 * - `path` needs to be freed by the caller.
 */
static void do_getpath(char *unit, char **path)
{
	char buf[256];
	char *cmd;
	if (asprintf(&cmd, "/usr/bin/systemctl show %s --value --property FragmentPath", unit) < 0) {
		perror("asprintf");
		exit(EXIT_FAILURE);
	}
	FILE *p = popen(cmd, "r");
	if (!p) {
		perror(cmd);
		exit(EXIT_FAILURE);
	}
	free(cmd);
	if (fscanf(p, "%255s", buf) != 1) {
		fprintf(stderr, "Unable to find unit file for: %s\n", unit);
		exit(EXIT_FAILURE);
	}
	*path = strndup(buf, 256);
	if (!*path) {
		perror("strndup");
		exit(EXIT_FAILURE);
	}
	pclose(p);
}

static void do_telemetry(char *unit)
{
	FILE *p = popen("/usr/bin/telem-record-gen"
			" --class org.clearlinux/clr-service-restart/try-restart-fail"
			" --severity 4", "w");
	/* ignore errors here, the system could not have telemetry */
	if (p) {
		fprintf(p,
			"PACKAGE_NAME=%s\n"
			"PACKAGE_VERSION=%s\n"
			"unit=%s\n",
			PACKAGE_NAME, PACKAGE_VERSION, unit);
		pclose(p);
	}
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
				char *unitpath;
				char *unit;
				do_getpath(argv[i], &unitpath);
				unit = basename(unitpath);
				char *sl;
				int ret;
				switch(m) {
				case ALLOW:
					if (asprintf(&sl, "/etc/clr-service-restart/%s",
							unit) < 0) {
						perror("asprintf");
						exit(EXIT_FAILURE);
					}
					fprintf(stderr, "ln -sf %s %s\n", unitpath, sl);
					mkdir("/etc/clr-service-restart",
						S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
					unlink(sl);
					ret = symlink(unitpath, sl);
					if ((ret != 0) && (errno != ENOENT)) {
						perror(sl);
						exit(EXIT_FAILURE);
					}
					free(sl);
					break;
				case DISALLOW:
					if (asprintf(&sl, "/etc/clr-service-restart/%s",
							unit) < 0) {
						perror("asprintf");
						exit(EXIT_FAILURE);
					}
					fprintf(stderr, "ln -sf /dev/null %s\n", sl);
					mkdir("/etc/clr-service-restart",
						S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
					unlink(sl);
					ret = symlink("/dev/null", sl);
					if ((ret != 0) && (errno != ENOENT)) {
						perror(sl);
						exit(EXIT_FAILURE);
					}
					free(sl);
					break;
				case DEFAULT:
					if (asprintf(&sl, "/etc/clr-service-restart/%s",
							unit) < 0) {
						perror("asprintf");
						exit(EXIT_FAILURE);
					}
					fprintf(stderr, "rm -f %s\n", sl);
					ret = unlink(sl);
					if ((ret != 0) && (errno != ENOENT)) {
						perror(sl);
						exit(EXIT_FAILURE);
					}
					free(sl);
					break;
				}
				free(unitpath);
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

		char *link;
		int ret = do_readlink(af, &link);
		if (ret == -1) {
			/* check /usr/share/clr-service-restart to see if it's allowed */
			if (asprintf(&af, "/usr/share/clr-service-restart/%s", e->d_name) < 1) {
				perror("asprintf()");
				exit(EXIT_FAILURE);
			}
			ret = do_readlink(af, &link);
			if (ret == -1) {
				/* we're not allowed to restart this unit */
				continue;
			} else {
				if (strcmp(link, "/dev/null") == 0) {
					free(link);
					/* not allowed */
					continue;
				}
				free(link);
			}
		} else {
			if (strcmp(link, "/dev/null") == 0) {
				free(link);
				/* not allowed to restart this unit */
				continue;
			}
			free(link);
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
					do_telemetry(e->d_name);
				}
				char *vrf;
				if (asprintf(&vrf, "/usr/bin/systemctl --quiet is-failed %s", e->d_name) < 0) {
					perror("asprintf");
					exit(EXIT_FAILURE);
				}
				int r2 = system(vrf);
				if (r2 == 0) {
					fprintf(stderr, "Failed to restart: %s (systemctl reports the unit failed: %d)\n",
						e->d_name, r);
					/* insert a telemetry event here */
					do_telemetry(e->d_name);
				}
				free(cmd);
				free(vrf);
			}
		}
		fclose(f);

	}
	closedir(d);
}
