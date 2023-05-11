/*
 * Copyright (c) 2023 Tobias Heider <me@tobhe.de>
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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "apparmor.h"

static const char *armor_proc_path_tmpl = "/proc/%d/attr/apparmor/%s";

int
armor_proc_open(void)
{
	char	*path;
	pid_t	 tid = gettid();
	int	 fd;
	int	 ret = -1;

	ret = asprintf(&path, armor_proc_path_tmpl, tid, "current");
	if (ret <= 0)
		return (-1);

	fd = open(path, O_WRONLY);
	free(path);

	return (fd);
}

int
armor_change_profile(int fd, const char *profile)
{
	char *cmd = NULL;
	int len;
	int ret = -1;

	len = asprintf(&cmd, "changeprofile %s", profile);
	if (len < 0)
		goto done;

	ret = write(fd, cmd, len);
	if (ret == -1)
		goto done;

	ret = 0;
 done:
	free(cmd);
	close(fd);
	return (ret);
}
