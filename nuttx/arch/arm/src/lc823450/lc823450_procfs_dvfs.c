/****************************************************************************
 * arch/arm/src/lc823450/lc823450_procfs_dvfs.c
 *
 *   Copyright (C) 2018 Sony Corporation. All rights reserved.
 *   Author: Masayuki Ishikawa <Masayuki.Ishikawa@jp.sony.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <sys/statfs.h>
#include <sys/stat.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/sched.h>
#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fs/procfs.h>
#include <nuttx/fs/dirent.h>

#include <arch/irq.h>

#include "lc823450_dvfs2.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DVFS_LINELEN  64

#ifndef MIN
#  define MIN(a,b) (a < b ? a : b)
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct dvfs_file_s
{
  struct procfs_file_s  base;    /* Base open file structure */
  unsigned int linesize;         /* Number of valid characters in line[] */
  char line[DVFS_LINELEN];       /* Pre-allocated buffer for formatted lines */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     dvfs_open(FAR struct file *filep, FAR const char *relpath,
                         int oflags, mode_t mode);
static int     dvfs_close(FAR struct file *filep);
static ssize_t dvfs_read(FAR struct file *filep, FAR char *buffer,
                         size_t buflen);
static ssize_t dvfs_write(FAR struct file *filep, FAR const char *buffer,
                          size_t buflen);
static int     dvfs_dup(FAR const struct file *oldp,
                        FAR struct file *newp);
static int     dvfs_stat(FAR const char *relpath, FAR struct stat *buf);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct procfs_operations dvfs_procfsoperations =
{
  dvfs_open,      /* open */
  dvfs_close,     /* close */
  dvfs_read,      /* read */
  dvfs_write,     /* write */
  dvfs_dup,       /* dup */
  NULL,           /* opendir */
  NULL,           /* closedir */
  NULL,           /* readdir */
  NULL,           /* rewinddir */
  dvfs_stat       /* stat */
};

static const struct procfs_entry_s g_procfs_dvfs =
{
  "dvfs",
  &dvfs_procfsoperations
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern int8_t   g_dvfs_enabled;
extern uint16_t g_dvfs_cur_freq;


/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dvfs_open
 ****************************************************************************/

static int dvfs_open(FAR struct file *filep, FAR const char *relpath,
                    int oflags, mode_t mode)
{
  FAR struct dvfs_file_s *priv;

  finfo("Open '%s'\n", relpath);

  /* "dvfs" is the only acceptable value for the relpath */

  if (strcmp(relpath, "dvfs") != 0)
    {
      ferr("ERROR: relpath is '%s'\n", relpath);
      return -ENOENT;
    }

  /* Allocate a container to hold the task and attribute selection */

  priv = (FAR struct dvfs_file_s *)kmm_zalloc(sizeof(struct dvfs_file_s));
  if (!priv)
    {
      ferr("ERROR: Failed to allocate file attributes\n");
      return -ENOMEM;
    }

  /* Save the index as the open-specific state in filep->f_priv */

  filep->f_priv = (FAR void *)priv;
  return OK;
}

/****************************************************************************
 * Name: dvfs_close
 ****************************************************************************/

static int dvfs_close(FAR struct file *filep)
{
  FAR struct dvfs_file_s *priv;

  /* Recover our private data from the struct file instance */

  priv = (FAR struct dvfs_file_s *)filep->f_priv;
  DEBUGASSERT(priv);

  /* Release the file attributes structure */

  kmm_free(priv);
  filep->f_priv = NULL;
  return OK;
}

/****************************************************************************
 * Name: dvfs_read
 ****************************************************************************/

static ssize_t dvfs_read(FAR struct file *filep, FAR char *buffer,
                         size_t buflen)
{
  FAR struct dvfs_file_s *priv;
  size_t linesize;
  size_t copysize;
  size_t remaining;
  size_t totalsize;
  off_t offset = filep->f_pos;

  finfo("buffer=%p buflen=%d\n", buffer, (int)buflen);

  priv = (FAR struct dvfs_file_s *)filep->f_priv;
  DEBUGASSERT(priv);

  remaining = buflen;
  totalsize = 0;

  linesize = snprintf(priv->line,
                      DVFS_LINELEN,
                      "cur_freq %d \n", g_dvfs_cur_freq);
  copysize = procfs_memcpy(priv->line, linesize, buffer, remaining, &offset);
  totalsize += copysize;
  buffer    += copysize;
  remaining -= copysize;

  if (totalsize >= buflen)
    {
      return totalsize;
    }

  linesize = snprintf(priv->line,
                      DVFS_LINELEN,
                      "enable %d \n", g_dvfs_enabled);
  copysize = procfs_memcpy(priv->line, linesize, buffer, remaining, &offset);
  totalsize += copysize;

  /* Update the file offset */

  if (totalsize > 0)
    {
      filep->f_pos += totalsize;
    }

  return totalsize;
}

/****************************************************************************
 * Name: procfs_write
 ****************************************************************************/

static ssize_t dvfs_write(FAR struct file *filep, FAR const char *buffer,
                          size_t buflen)
{
  char line[DVFS_LINELEN];
  char cmd[16];
  int  n;
  int  freq;
  int  enable;

  n = MIN(buflen, DVFS_LINELEN - 1);
  strncpy(line, buffer, n);
  line[n] = '\0';

  n = MIN(strcspn(line, " "), sizeof(cmd) - 1);
  strncpy(cmd, line, n);
  cmd[n] = '\0';

  if (0 == strcmp(cmd, "cur_freq"))
    {
      freq = atoi(line + (n + 1));
      (void)lc823450_dvfs_set_freq(freq);
    }
  else if (0 == strcmp(cmd, "enable"))
    {
      enable = atoi(line + (n + 1));
      g_dvfs_enabled = enable;
    }
  else
    {
      printf("%s not supported.\n", cmd);
    }

  return buflen;
}

/****************************************************************************
 * Name: dvfs_dup
 ****************************************************************************/

static int dvfs_dup(FAR const struct file *oldp, FAR struct file *newp)
{
  FAR struct dvfs_file_s *oldpriv;
  FAR struct dvfs_file_s *newpriv;

  finfo("Dup %p->%p\n", oldp, newp);

  /* Recover our private data from the old struct file instance */

  oldpriv = (FAR struct dvfs_file_s *)oldp->f_priv;
  DEBUGASSERT(oldpriv);

  /* Allocate a new container to hold the task and attribute selection */

  newpriv = (FAR struct dvfs_file_s *)kmm_zalloc(sizeof(struct dvfs_file_s));
  if (!newpriv)
    {
      ferr("ERROR: Failed to allocate file attributes\n");
      return -ENOMEM;
    }

  /* The copy the file attributes from the old attributes to the new */

  memcpy(newpriv, oldpriv, sizeof(struct dvfs_file_s));

  /* Save the new attributes in the new file structure */

  newp->f_priv = (FAR void *)newpriv;
  return OK;
}

/****************************************************************************
 * Name: dvfs_stat
 ****************************************************************************/

static int dvfs_stat(const char *relpath, struct stat *buf)
{
  if (strcmp(relpath, "dvfs") != 0)
    {
      ferr("ERROR: relpath is '%s'\n", relpath);
      return -ENOENT;
    }

  buf->st_mode    =
    S_IFREG |
    S_IROTH | S_IWOTH |
    S_IRGRP | S_IWGRP |
    S_IRUSR | S_IWUSR;

  buf->st_size    = 0;
  buf->st_blksize = 0;
  buf->st_blocks  = 0;

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dvfs_procfs_register
 ****************************************************************************/

int dvfs_procfs_register(void)
{
  return procfs_register(&g_procfs_dvfs);
}
