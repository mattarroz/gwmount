#ifndef FFTABLE_H
#define FFTABLE_H
#include <ff.h>

#define FFFF_RDONLY 1

struct fftab {
	int fd;
	int index;
	int flags;
	FATFS fs;
};

int fftab_new (int flags);
void fftab_del(int index);
struct fftab *fftab_get(int index);

#endif
