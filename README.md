
## clr-service-restart

A program that restarts systemd services that are using software
components that have been updated.


## How it works

The Linux kernel maintains information about running processes in the
`procfs` filesystem. This information can be used to identify processes
that are using resources that are no longer physically present on
the file system, such as when a binary or system library is updated.

Any software update causes the kernel to see that a reference to a
binary or a reference to a library is no longer corresponding with the
actual current version of the library or binary on the filesystem. If
this happens, the kernel will mark the `procfs` entries for those
references as `(deleted)`. Specifically the `exe` symlink will get
marked in this way if the binary is updated, and the `maps` file will
get updated in this way if a library component is updated.

Using these references, we now know which programs are currently being
executed that are holding references to outdated libraries or binaries,
and therefore should be restarted.

However, we do not want to restart programs that are not part of a
system unit, or in some other way are not part of the system domain. In
order to avoid considering all processes running for the purpose of
restarting them, we restrict consideration to only those processes
that are listed as active `tasks` in the systemd `system.slice`
hierarchy maintained under `/sys/fs/cgroup/systemd/`. This hierarchy
only contains units (and therefore tasks) in the system domain and
excludes user units or user programs.


## Configuration

It's extremely risky to restart all units unconditionally since some
units may require specific ordering, interaction or other factors
to be considered. By default, this program will not restart any
service automatically, and will only consider units that have been
explicitly marked as `allowed` by either the local administrator
or the distribution. This is done through maintaining symlinks in
`/usr/share/clr-service-restart` and `/etc/clr-service-restart`. If a
valid symlink is found in these folders, the unit will be considered
for restarting. If a symlink is found in `/etc/clr-service-restart`
with the name of a unit pointing to `/dev/null` it will be
omitted for consideration and not be restarted. Links in the
`/etc/clr-service-restart` location take precedence over those in
`/usr/share/clr-service-restart`. Links to `/dev/null` in the
`/usr/share/clr-service-restart` location are invalid.


## Commands

These symlinks can be manipulated by the local administrator with the
`clr-service-restart` program, which understands the following 3
commands:

 * `allow "foo.service"`:

   Mark "foo.service" as restartable.

 * `disallow "bar.service"`:

   Mark "bar.service" as not restartable.

 * `default "baz.service"`:

   Remove local preferences and revert to the distribution default
   for "baz.service".

If `clr-service-restart` is executed without any commands, it will
restart all services that are marked `allow` for restart.


## Options

The following options are understood. These options may not be used
together with the `allow`, `disallow` and `default` commands.

 * `-n`:

   Don't actually perform any restarts, just show what would happen
   if the program would execute normally. This makes the program
   more verbose.

 * `-a`:

   Consider all system units. With this option, all system units that
   are running will be considered for restarting. This should normally
   only be done interactively, and never in an automated fashion.


## Bugs

Please report bugs to: `dev@lists.clearlinux.org`


## License

Copyright © 2018 Intel Corporation

Author: Auke-jan H. Kok `<auke-jan.h.kok@intel.com>`

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
