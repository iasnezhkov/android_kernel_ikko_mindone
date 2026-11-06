/* SPDX-License-Identifier: GPL-2.0 */
/* SPDX-FileCopyrightText: 2026 The LineageOS Project */
#ifndef MINDONE_ETA_POLICY_H
#define MINDONE_ETA_POLICY_H

#include <linux/types.h>

#define ETA_OP_READ_REGISTER 1
#define ETA_OP_ENABLE 2
#define ETA_OP_IS_ENABLED 3
#define ETA_OP_SET_ICHG 4
#define ETA_OP_GET_ICHG 5
#define ETA_OP_SET_CV 6
#define ETA_OP_GET_CV 7
#define ETA_OP_SET_IINLIM 8
#define ETA_OP_GET_IINLIM 9
#define ETA_OP_SET_ITERM 10
#define ETA_OP_GET_ITERM 11
#define ETA_OP_SET_VINDPM 12
#define ETA_OP_GET_VINDPM 13
#define ETA_OP_SET_BOOST_LIMIT 14
#define ETA_OP_RUN_AICL 15
#define ETA_OP_KICK_WATCHDOG 16
#define ETA_OP_ENABLE_TERMINATION 17
#define ETA_OP_ENABLE_SAFETY_TIMER 18
#define ETA_OP_IS_SAFETY_TIMER_ENABLED 19
#define ETA_OP_ENABLE_OTG 20
#define ETA_OP_IS_OTG_ENABLED 21
#define ETA_OP_IS_CHARGING_DONE 22
#define ETA_OP_SEND_PE_PATTERN 23
#define ETA_OP_READ_SAFETY_STATE 24
#define ETA_OP_GET_PROPERTY 25
#define ETA_OP_INITIALIZE 26

#define ETA_PROPERTY_ONLINE 1
#define ETA_PROPERTY_STATUS 2
#define ETA_PROPERTY_TYPE 3
#define ETA_PROPERTY_USB_TYPE 4
#define ETA_PROPERTY_VOLTAGE_MAX 5
#define ETA_PROPERTY_CURRENT_MAX 6
#define ETA_PROPERTY_CONSTANT_CHARGE_VOLTAGE 7
#define ETA_PROPERTY_CHARGE_TERM_CURRENT 8

int mindone_eta_call(void *context, u32 operation, u32 argument, u32 *output);
void mindone_eta_io_lock(void *context);
void mindone_eta_io_unlock(void *context);
u8 mindone_eta_io_suspended(void *context);
int mindone_eta_io_read(void *context, u8 reg, u8 *value);
int mindone_eta_io_write(void *context, u8 reg, u8 value);
int mindone_eta_io_pin(void *context, u8 enabled);
void mindone_eta_io_sleep(void *context, u32 milliseconds);

#endif
