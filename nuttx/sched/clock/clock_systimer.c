/****************************************************************************
 * sched/clock/clock_systimer.c
 *
 *   Copyright (C) 2011, 2014-2016, 2018 Gregory Nutt. All rights reserved.
 *   Author: Gregory Nutt <gnutt@nuttx.org>
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

#include <stdint.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/clock.h>

#include "clock/clock.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* See nuttx/clock.h */

#undef clock_systimer

/* 32-bit mask for 64-bit timer values */

#define TIMER_MASK32 0x00000000ffffffff

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: clock_systimer
 *
 * Description:
 *   Return the current value of the 32/64-bit system timer counter
 *
 * Parameters:
 *   None
 *
 * Returned Value:
 *   The current value of the system timer counter
 *
 * Assumptions:
 *
 ****************************************************************************/

systime_t clock_systimer(void)
{
#ifdef CONFIG_SCHED_TICKLESS
# ifdef CONFIG_SYSTEM_TIME64

  struct timespec ts;

  /* Get the time from the platform specific hardware */

#ifndef CONFIG_CLOCK_TIMEKEEPING
  (void)up_timer_gettime(&ts);
#else
  (void)clock_timekeeping_get_monotonic_time(&ts);
#endif

  /* Convert to a 64-bit value in microseconds, then in clock tick units */

  return USEC2TICK(1000000 * (uint64_t)ts.tv_sec + (uint64_t)ts.tv_nsec / 1000);

# else /* CONFIG_SYSTEM_TIME64 */

  struct timespec ts;
  uint64_t tmp;

  /* Get the time from the platform specific hardware */

#ifndef CONFIG_CLOCK_TIMEKEEPING
  (void)up_timer_gettime(&ts);
#else
  (void)clock_timekeeping_get_monotonic_time(&ts);
#endif

  /* Convert to a 64- then a 32-bit value */

  tmp = USEC2TICK(1000000 * (uint64_t)ts.tv_sec + (uint64_t)ts.tv_nsec / 1000);
  return (systime_t)(tmp & TIMER_MASK32);

# endif /* CONFIG_SYSTEM_TIME64 */
#else /* CONFIG_SCHED_TICKLESS */
# ifdef CONFIG_SYSTEM_TIME64

  systime_t sample;
  systime_t verify;

  /* 64-bit accesses are not atomic on most architectures.  The following
   * loop samples the 64-bit timer twice and loops in the rare event that
   * there was 32-bit rollover between samples.
   *
   * If there is no 32-bit rollover, then:
   *
   *  - The MS 32-bits of each sample will be the same, and
   *  - The LS 32-bits of the second sample will be greater than or equal to
   *    the LS 32-bits for the first sample.
   */

  do
    {
      verify = g_system_timer;
      sample = g_system_timer;
    }
  while ((sample &  TIMER_MASK32)  < (verify &  TIMER_MASK32) ||
         (sample & ~TIMER_MASK32) != (verify & ~TIMER_MASK32));

  return sample;

# else /* CONFIG_SYSTEM_TIME64 */

  /* Return the current system time */

  return g_system_timer;

# endif /* CONFIG_SYSTEM_TIME64 */
#endif /* CONFIG_SCHED_TICKLESS */
}
