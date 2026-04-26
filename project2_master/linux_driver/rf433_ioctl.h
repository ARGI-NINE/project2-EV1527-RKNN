/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * rf433_ioctl.h - shared header between rf433_drv kernel module and userspace
 *
 * Frame structure, statistics, and ioctl command definitions for the
 * project2 RF433 serdev receiver driver.
 */
#ifndef _RF433_IOCTL_H
#define _RF433_IOCTL_H

#ifdef __KERNEL__
#include <linux/types.h>
#include <linux/ioctl.h>
#else
#include <stdint.h>
#include <sys/ioctl.h>
typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;
#endif

/* ------------------------------------------------------------------ */
/* Frame delivered to userspace via read()                             */
/* ------------------------------------------------------------------ */
#define RF433_MAX_PULSES  1024

struct rf433_frame {
    __u64 timestamp_ns;          /* ktime_get_real_ns() at frame-complete */
    __u16 pulse_count;           /* number of valid pulse entries         */
    __u16 reserved;              /* padding, set to 0                     */
    __u32 seq;                   /* monotonic frame sequence number       */
    __u16 pulse[RF433_MAX_PULSES];
};

/* ------------------------------------------------------------------ */
/* Runtime statistics                                                  */
/* ------------------------------------------------------------------ */
struct rf433_stats {
    __u64 frame_ok;              /* successfully queued frames            */
    __u64 crc_err;               /* CRC mismatch count                   */
    __u64 len_err;               /* invalid length count                  */
    __u64 drop_cnt;              /* frames dropped (queue full)           */
};

/* ------------------------------------------------------------------ */
/* Driver status                                                       */
/* ------------------------------------------------------------------ */
struct rf433_status {
    __u8  online;                /* 1 = receiving frames, 0 = offline     */
    __u8  reserved[3];
    __u32 seq;                   /* current sequence number               */
    __u32 queue_depth;           /* frames currently in kfifo             */
    __u32 queue_capacity;        /* max frames in kfifo                   */
};

/* ------------------------------------------------------------------ */
/* ioctl commands — magic 'R', numbers 0x10-0x13                       */
/* ------------------------------------------------------------------ */
#define RF433_IOC_MAGIC  'R'

#define RF433_IOC_GET_STATS    _IOR(RF433_IOC_MAGIC, 0x10, struct rf433_stats)
#define RF433_IOC_CLR_STATS    _IO(RF433_IOC_MAGIC,  0x11)
#define RF433_IOC_GET_STATUS   _IOR(RF433_IOC_MAGIC, 0x12, struct rf433_status)
#define RF433_IOC_FLUSH_QUEUE  _IO(RF433_IOC_MAGIC,  0x13)

#endif /* _RF433_IOCTL_H */
