/*  RetroArch - A frontend for libretro.
 *  Copyright (C) 2011-2017 - Daniel De Matteis
 *
 *  RetroArch is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  RetroArch is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with RetroArch.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#ifdef _XBOX1
#include <xtl.h>
#endif

#ifdef __MACH__
#include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR
#include <stdio.h>
#else
#include <asl.h>
#endif
#endif

#include <stdio.h>
#include <stdarg.h>

#ifdef ANDROID
#include <android/log.h>
#endif

#include <string/stdstring.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef RARCH_INTERNAL
#include "frontend/frontend_driver.h"
#endif

#include "file_path_special.h"
#include "verbosity.h"

/* If this is non-NULL. RARCH_LOG and friends
 * will write to this file. */
static FILE *log_file            = NULL;
static bool main_verbosity       = false;
static bool log_file_initialized = false;

void verbosity_enable(void)
{
   main_verbosity = true;
#ifdef RARCH_INTERNAL
   if (!log_file_initialized)
      frontend_driver_attach_console();
#endif
}

void verbosity_disable(void)
{
   main_verbosity = false;
#ifdef RARCH_INTERNAL
   if (!log_file_initialized)
      frontend_driver_detach_console();
#endif
}

bool verbosity_is_enabled(void)
{
   return main_verbosity;
}

bool *verbosity_get_ptr(void)
{
   return &main_verbosity;
}

void *retro_main_log_file(void)
{
   return log_file;
}

void retro_main_log_file_init(const char *path)
{
   if (log_file_initialized)
      return;

   log_file             = stderr;
   if (path == NULL)
      return;

   log_file             = fopen(path, "wb");
   log_file_initialized = true;
}

void retro_main_log_file_deinit(void)
{
   
}

#if !defined(HAVE_LOGGER)
void RARCH_LOG_V(const char *tag, const char *fmt, va_list ap)
{
   FILE *fp = (FILE*)retro_main_log_file();

   if (!verbosity_is_enabled())
      return;
   fprintf(fp, "%s ",
         tag ? tag : file_path_str(FILE_PATH_LOG_INFO));
   vfprintf(fp, fmt, ap);
   fflush(fp);
}

void RARCH_LOG(const char *fmt, ...)
{
   va_list ap;

   if (!verbosity_is_enabled())
      return;

   va_start(ap, fmt);
   RARCH_LOG_V(file_path_str(FILE_PATH_LOG_INFO), fmt, ap);
   va_end(ap);
}

void RARCH_LOG_OUTPUT_V(const char *tag, const char *msg, va_list ap)
{
   RARCH_LOG_V(tag, msg, ap);
}

void RARCH_LOG_OUTPUT(const char *msg, ...)
{
   va_list ap;
   va_start(ap, msg);
   RARCH_LOG_OUTPUT_V(file_path_str(FILE_PATH_LOG_INFO), msg, ap);
   va_end(ap);
}

void RARCH_WARN_V(const char *tag, const char *fmt, va_list ap)
{
   RARCH_LOG_V(tag, fmt, ap);
}

void RARCH_WARN(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   RARCH_WARN_V(file_path_str(FILE_PATH_LOG_WARN), fmt, ap);
   va_end(ap);
}

void RARCH_ERR_V(const char *tag, const char *fmt, va_list ap)
{
   RARCH_LOG_V(tag, fmt, ap);
}

void RARCH_ERR(const char *fmt, ...)
{
   va_list ap;
   va_start(ap, fmt);
   RARCH_ERR_V(file_path_str(FILE_PATH_LOG_ERROR), fmt, ap);
   va_end(ap);
}
#endif

