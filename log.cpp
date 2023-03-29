#include <allegro.h>
#include <stdio.h>
#include <stdarg.h>
#include "log.h"

FILE* errorlog;
FILE *logfile;

int open_log(const char *filename)
{
 if ((logfile = fopen(filename, "w")) != NULL) return 0; /* Opened succesfully. */
 
 return -1; /* Failed. */
}

int wrlog(const char *format, ...)
{
 va_list ptr; /* get an arg pointer */
 int status = -1;
 
 if (logfile != NULL)
 {
  /* initialize ptr to point to the first argument after the format string */
  va_start(ptr, format);
 
  /* Write to logfile. */
  status = vfprintf(logfile, format, ptr); // Write passed text.
  fprintf(logfile, "\n"); // New line..
 
  va_end(ptr);
 
  /* Flush file. */
  fflush(logfile);
 }

 return status;
}

int close_log(void)
{
 int status = -1;
 
 wrlog("Closing log, ending program.");
 if (logfile != NULL)
 {
  fflush(logfile);
  fclose(logfile);
 }
 
 return status;
}