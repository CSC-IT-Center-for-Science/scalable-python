/*  Copyright (C) 2010       CSC - IT Center for Science Ltd.
*/

#ifdef ENABLE_MPI
#include <mpi.h>
#endif
/* In BG/P sys/stat.h does not seem to work properly
 * is there a different define in Python.h? */
#include "Python.h"
// #include <stdio.h>
// #include <sys/stat.h>

#define MASTER 0
#define MAX_FILES 32 // Maximum number of files to open in parallel

static int rank = MASTER;
static int enabled = 0;
static int initialized = 0;

#ifdef ENABLE_MPI
static MPI_Comm io_comm;
static FILE *parallel_fps[MAX_FILES];
static int current_fp = 0;
static FILE *fp_dummy;

// Initialize wrapper stuff
void init_io_wrappers(void)
{
  int i, mpi_initialized;
  MPI_Initialized(&mpi_initialized);
  if (mpi_initialized)
  {
    MPI_Comm_dup(MPI_COMM_WORLD, &io_comm);
    MPI_Comm_rank(io_comm, &rank);
    for (i=0; i < MAX_FILES; i++)
      parallel_fps[i] = (FILE*) -1;
    // fp_dummy = stderr;
    fp_dummy = fopen("/dev/null", "r");
#ifdef DEBUG_MPI
    fprintf(stderr,
        "[%d] (%d%d) I/O wrappers are now INITIALISED and ENABLED.\n",
        rank, initialized, enabled);
#endif
    enabled = 1;
    initialized = 1;
  }
}

// Finalize wrapper stuff
void finalize_io_wrappers(void)
{
  if (initialized)
    {
      MPI_Comm_free(&io_comm);
      fclose(fp_dummy);
#ifdef DEBUG_MPI
    fprintf(stderr,
        "[%d] (%d%d) I/O wrappers are now UNINITIALISED and DISABLED.\n",
        rank, initialized, enabled);
#endif
      initialized = 0;
      enabled = 0;
    }
}
#endif

// switching wrapping on and off
void enable_io_wrappers(void)
{
#ifdef DEBUG_MPI
  fprintf(stderr, "[%d] (%d%d) I/O wrappers are now ENABLED.\n",
      rank, initialized, enabled);
#endif
  enabled = 1;
}

void disable_io_wrappers(void)
{
#ifdef DEBUG_MPI
  fprintf(stderr, "[%d] (%d%d) I/O wrappers are now DISABLED.\n",
        rank, initialized, enabled);
#endif
  enabled = 0;
}

#ifdef ENABLE_MPI
// Utility function to check if the file pointer is "parallel"
int check_fp(FILE *fp)
{
  int i;
  for (i=current_fp-1; i >=0; i--)
    if ( fp == parallel_fps[i] )
      return i+1;

  return 0;
}

// File open, close, lock, etc.
FILE* __wrap_fopen(const char *filename, const char *modes)
{
  FILE *fp;
  int fp_is_null;
#ifdef DEBUG_MPI
  if (rank == MASTER)
      fprintf(stderr, "[%d] (%d%d) __wrap_fopen('%s', '%s') called\n",
          rank, initialized, enabled, filename, modes);
#endif
  if (! initialized)
    init_io_wrappers();
  // Wrap only in read mode
  if (modes[0] == 'r' && enabled) {
    if (rank == MASTER) {
#ifdef DEBUG_MPI
      fprintf(stderr, "[%d] (%d%d) open file + send: %s\n", rank,
          initialized, enabled, filename);
#endif
      fp = fopen(filename, modes);
      // NULL information is needed also in other ranks
      fp_is_null = ((fp == NULL) ? 1 : 0);
      MPI_Bcast(&fp_is_null, 1, MPI_INT, MASTER, io_comm);
    } else {
      MPI_Bcast(&fp_is_null, 1, MPI_INT, MASTER, io_comm);
      if ( fp_is_null)
        fp = NULL;
      else
        fp = fp_dummy;
    }
    // Store the "parallel" file pointer
    if (fp != NULL) {
      parallel_fps[current_fp] = fp;
      current_fp++;
      if (current_fp == MAX_FILES) {
        printf("Too many open files\n");
        MPI_Abort(io_comm, -1);
      }
    }
    MPI_Barrier(io_comm);
  } else {
    // Write mode, all processes can participate
#ifdef DEBUG_MPI
      fprintf(stderr, "[%d] (%d%d) open file: %s\n", rank,
          initialized, enabled, filename);
#endif
    fp = fopen(filename, modes);
  }
  return fp;
}

int  __wrap_fclose(FILE *fp)
{
  int x;
  int i = check_fp(fp);
  if ( i == current_fp && i > 0 )
    current_fp--;
  if ( ! i || (rank == MASTER) )
  {
    x = fclose(fp);
  }

  // TODO: Should one return the true return value of fclose?
  return 0;
}

void  __wrap_setbuf(FILE *fp, char *buf)
{
  if ( ! check_fp(fp) || (rank == MASTER) )
    setbuf(fp, buf);
}

int  __wrap_setvbuf(FILE *fp, char *buf, int type, size_t size)
{
  int x;
  if (check_fp(fp))
  {
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) setvbuf + send\n", rank,
        initialized, enabled);
#endif
      x = setvbuf(fp, buf, type, size);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  }
  else
    x = setvbuf(fp, buf, type, size);
  return x;
}

int  __wrap_flockfile(FILE *fp)
{
  if ( ! check_fp(fp) || (rank == MASTER) )
    flockfile(fp);
  return 0;
}

int  __wrap_funlockfile(FILE *fp)
{
  if ( ! check_fp(fp) || (rank == MASTER) )
    funlockfile(fp);
  return 0;
}

int __wrap_ferror(FILE* fp)
{
  int x;
  if (check_fp(fp))
  {
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) ferror + send\n", rank,
        initialized, enabled);
#endif
      x = ferror(fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  }
  else
    x = ferror(fp);
  return x;
}

int __wrap_feof(FILE* fp)
{
  int x;
  if (check_fp(fp))
  {
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) feof + send\n", rank,
        initialized, enabled);
#endif
      x = feof(fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  }
  else
    x = feof(fp);
  return x;
}

void  __wrap_clearerr(FILE *fp)
{
  if ( ! check_fp(fp) || (rank == MASTER) )
    clearerr(fp);
}

// File positioning etc.
int __wrap_fseek(FILE *fp, long offset, int origin)
{
  int x;
  if (check_fp(fp))
  {
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) fseek + send\n", rank,
        initialized, enabled);
#endif
      x = fseek(fp, offset, origin);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  }
  else
    x = fseek(fp, offset, origin);
  return x;
}

void __wrap_rewind(FILE *fp)
{
  if (! check_fp(fp) || (rank == MASTER))
    rewind(fp);
}


int __wrap_ungetc(int c, FILE* fp)
{
  int x;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) ungetc + send\n", rank,
        initialized, enabled);
#endif
      x =ungetc(c, fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  else
    x =ungetc(c, fp);
  return x;
}

int __wrap_fflush(FILE *fp)
{
  int x;
  if (check_fp(fp))
  {
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) fflush + send\n", rank,
        initialized, enabled);
#endif
      x = fflush(fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  }
  else
    x = fflush(fp);
  return x;
}

int __wrap_fgetpos ( FILE * fp, fpos_t * pos )
{
  int x;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) fgetpos + send\n", rank,
        initialized, enabled);
#endif
      x = fgetpos(fp, pos);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  else
    x = fgetpos(fp, pos);
  return x;
}

int __wrap_fsetpos ( FILE * fp, const fpos_t * pos )
{
  int x;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) fsetpos + send\n", rank,
        initialized, enabled);
#endif
      x = fsetpos(fp, pos);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  else
    x = fsetpos(fp, pos);
  return x;
}

long int __wrap_ftell ( FILE * fp )
{
  long x;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) ftell + send\n", rank,
        initialized, enabled);
#endif
      x = ftell(fp);
      MPI_Bcast(&x, 1, MPI_LONG, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_LONG, MASTER, io_comm);
  else
    x = ftell(fp);
  return x;
}

// Read functions
int __wrap_getc(FILE *fp)
{
  int x;
  if (enabled)
    if (rank == MASTER )
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) getc + send\n", rank,
        initialized, enabled);
#endif
      x = getc(fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
    {
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
  else
    x = getc(fp);
  return x;
}

int __wrap_getc_unlocked(FILE *fp)
{
  int x;
  if (enabled)
    if (rank == MASTER )
    {
#ifdef DEBUG_MPI
    fprintf(stderr, "[%d] (%d%d) getc_unlocked + send\n", rank,
        initialized, enabled);
#endif
      x = getc_unlocked(fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
    {
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
  else
    x = getc_unlocked(fp);
  return x;
}

int __wrap_fread(void *ptr, size_t size, size_t n, FILE* fp)
{
  // Is it OK to use just int for the size of data read?
  int x;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
      fprintf(stderr, "[%d] (%d%d) fread + send\n", rank,
          initialized, enabled);
#endif
      x = fread(ptr, size, n, fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
      MPI_Bcast(ptr, x*size, MPI_BYTE, MASTER, io_comm);
    }
    else
    {
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
      MPI_Bcast(ptr, x*size, MPI_BYTE, MASTER, io_comm);
    }
  else
    x = fread(ptr, size, n, fp);
  return x;
}

char *__wrap_fgets(char *str, int num, FILE* fp)
{
  char* s;
  int s_is_null=0;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
      fprintf(stderr, "[%d] (%d%d) fgets + send\n", rank,
          initialized, enabled);
#endif
      s = fgets(str, num, fp);
      if (s==NULL)
      {
        s_is_null = 1;
        MPI_Bcast(&s_is_null, 1, MPI_INT, MASTER, io_comm);
      }
      else
      {
        MPI_Bcast(&s_is_null, 1, MPI_INT, MASTER, io_comm);
        MPI_Bcast(s, num, MPI_BYTE, MASTER, io_comm);
      }
    }
    else
    {
      MPI_Bcast(&s_is_null, 1, MPI_INT, MASTER, io_comm);
      if (s_is_null == 1)
        s = NULL;
      else
        MPI_Bcast(s, num, MPI_BYTE, MASTER, io_comm);
    }
  else
    s = fgets(str, num, fp);
  return s;
}

int __wrap_fgetc ( FILE * fp )
{
  int x;
  if (enabled)
    if (rank == MASTER )
    {
#ifdef DEBUG_MPI
      fprintf(stderr, "[%d] (%d%d) fgetc + send\n", rank,
          initialized, enabled);
#endif
      x = getc(fp);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
  else
    x = fgetc(fp);
  return x;
}

int __wrap_fstat(int fildes, struct stat *buf)
{
  int size = sizeof(struct stat);
  int x;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
      fprintf(stderr, "[%d] (%d%d) fstat + send\n", rank,
          initialized, enabled);
#endif
      x = fstat(fildes, buf);
      MPI_Bcast(buf, size, MPI_BYTE, MASTER, io_comm);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
    {
      MPI_Bcast(buf, size, MPI_BYTE, MASTER, io_comm);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
  else
    x = fstat(fildes, buf);

  return x;
}

int __wrap_stat(const char *path, struct stat *buf)
{
  int size = sizeof(struct stat);
  int x;
  if (enabled)
    if (rank == MASTER)
    {
#ifdef DEBUG_MPI
      fprintf(stderr, "[%d] (%d%d) stat + send\n", rank,
          initialized, enabled);
#endif
      x = stat(path, buf);
      MPI_Bcast(buf, size, MPI_BYTE, MASTER, io_comm);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
    else
    {
      MPI_Bcast(buf, size, MPI_BYTE, MASTER, io_comm);
      MPI_Bcast(&x, 1, MPI_INT, MASTER, io_comm);
    }
  else
    x = stat(path, buf);

#ifdef DEBUG_MPI
  if (! x && (rank == MASTER))
    fprintf(stderr, "[%d] (%d%d) File found: %s\n", rank,
        initialized, enabled, path);
#endif
  return x;
}
#endif

/* vim: set et tw=80 ts=2 sw=2: */
