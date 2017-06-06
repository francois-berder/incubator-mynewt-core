/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <assert.h>
#include <os/os.h>
#include <hal/hal_os_tick.h>
#include <stdbool.h>
#include <xc.h>

#define TIMER1_PRESCALER_COUNT      (4)
static const uint32_t timer1_prescalers[TIMER1_PRESCALER_COUNT] =
{1, 8, 64, 256};
static volatile bool timer1_expired;


static inline uint32_t
get_peripheral_base_clock(void)
{
    return MYNEWT_VAL(CLOCK_FREQ) / ((PB3DIV & _PB3DIV_PBDIV_MASK) + 1);
}

static compute_ticks(uint32_t prescaler, uint32_t pr) {
    uint64_t ticks = prescaler * pr;
    ticks *= OS_TICKS_PER_SEC;
    return ticks / get_peripheral_base_clock();
}

static uint32_t
compute_max_ticks_per_interrupt(void)
{
    return compute_ticks(timer1_prescalers[TIMER1_PRESCALER_COUNT - 1],
                         UINT16_MAX);
}

void
__attribute__((interrupt(IPL7AUTO), vector(_TIMER_1_VECTOR)))
timer1_isr(void)
{
    timer1_expired = true;
    IFS0CLR = _IFS0_T1IF_MASK;
}

static void
configure_timer1(os_time_t ticks)
{
    int i;
    uint64_t pr;

    /*
     *             ticks
     *  t =  ------------------
     *        OS_TICKS_PER_SEC
     *
     *
     *          prescaler * PR1
     *  t =    -----------------
     *              PBCLK3
     */

    /* Find minimum prescaler that can be used */
    for (i = 0; i < TIMER1_PRESCALER_COUNT; ++i) {
        if (compute_ticks(timer1_prescalers[i], UINT16_MAX) >= ticks) {
            break;
        }
    }
    assert(i < TIMER1_PRESCALER_COUNT);

    T1CON = (i << _T1CON_TCKPS_POSITION) & _T1CON_TCKPS_MASK;

    pr = get_peripheral_base_clock();
    pr *= ticks;
    pr /= timer1_prescalers[i] * OS_TICKS_PER_SEC;
    PR1 = pr;

    char buffer[64];
    sprintf(buffer, "prescaler=%u\n", timer1_prescalers[i]);
    console_write(buffer, strlen(buffer));
    sprintf(buffer, "pr=%u\n", PR1);
    console_write(buffer, strlen(buffer));

    /* Enable interrupt for timer 1 */
    IPC1CLR = _IPC1_T1IS_MASK | _IPC1_T1IP_MASK;
    IPC1SET = 7 << _IPC1_T1IP_POSITION | 7 << _IPC1_T1IS_POSITION;
    IFS0CLR = _IFS0_T1IF_MASK;
    IEC0SET = _IEC0_T1IE_MASK;

    TMR1 = 0;
    T1CONSET = _T1CON_TON_MASK;
}

void
os_tick_idle(os_time_t ticks)
{
    const uint32_t max_ticks = compute_max_ticks_per_interrupt();

    /*
     * Configure timer1 to generate an interrupt after n ticks happened.
     * The CPU is configured in idle mode, instead of sleep mode, otherwise
     * timer 1 module would not run.
     * Core Timer cannot be used since it stops when the CPU is in idle mode.
     */

    /* Unlock sequence */
    SYSKEY = 0x00000000;
    SYSKEY = 0xAA996655;
    SYSKEY = 0x556699AA;

    OSCCONCLR = _OSCCON_SLPEN_MASK;

    /* Lock register */
    SYSKEY = 0x00000000;

    while (ticks != 0) {
        os_time_t t = ticks;

        /* Timer 1 cannot generate an interrupt after any duration. */
        if (t > max_ticks)
            t = max_ticks;
        char buffer[64];
        sprintf(buffer, "max_ticks=%u\n", max_ticks);
        console_write(buffer, strlen(buffer));
        sprintf(buffer, "t=%u\n", t);
        console_write(buffer, strlen(buffer));

        timer1_expired = false;
        configure_timer1(t);

        while (!timer1_expired)
        asm volatile ("wait");

        /* Disable timer1 */
        T1CON = 0;
        IEC0CLR = _IEC0_T1IE_MASK;

        /* Advance time */
        if (timer1_expired) {
            os_time_advance(t);
        } else {
            //uint32_t prescaler_index = (T1CON & _T1CON_TCKPS_MASK) >>
            //                           _T1CON_TCKPS_POSITION;
            //uint32_t prescaler = timer1_prescalers[prescaler_index];
            //os_time_advance(compute_ticks(prescaler, PR1 - TMR1));
            //break;
        }

        ticks -= t;
    }
}

void
os_tick_init(uint32_t os_ticks_per_sec, int prio)
{
    (void)os_ticks_per_sec;
    (void)prio;

    /*
     * Nothing to do as Core timer is used to generate an interrupt for every
     * tick.
     */
}