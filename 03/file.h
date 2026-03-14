#pragma once

int
write_file_at(int dirfd, const char *filename, const char *id);

int
open_at(const char *dirname, const char *name, int flags);
