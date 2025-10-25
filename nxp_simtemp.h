#ifndef _NXP_SIMTEMP_H_
#define _NXP_SIMTEMP_H_

#include <linux/types.h>

#define DRIVER_NAME "nxp_simtemp"
#define DEVICE_NAME "simtemp"
#define SIMTEMP_MAX_SAMPLES 32

// IOCTL commands
#define SIMTEMP_IOCTL_MAGIC 'S'
#define SIMTEMP_SET_SAMPLING _IOW(SIMTEMP_IOCTL_MAGIC, 1, __u32)
#define SIMTEMP_SET_THRESHOLD _IOW(SIMTEMP_IOCTL_MAGIC, 2, __s32)
#define SIMTEMP_SET_MODE _IOW(SIMTEMP_IOCTL_MAGIC, 3, __u32)
#define SIMTEMP_GET_STATS _IOR(SIMTEMP_IOCTL_MAGIC, 4, struct simtemp_stats)

// Operation modes
enum simtemp_mode {
    MODE_NORMAL = 0,
    MODE_NOISY = 1,
    MODE_RAMP = 2,
    MODE_MAX
};

// Sample structure
struct simtemp_sample {
    __u64 timestamp_ns;
    __s32 temp_mC;
    __u32 flags;
} __attribute__((packed));

// Statistics
struct simtemp_stats {
    __u32 samples_produced;
    __u32 alerts_triggered;
    __u32 read_errors;
    __u32 last_error;
};

// Flags
#define FLAG_NEW_SAMPLE          (1 << 0)
#define FLAG_THRESHOLD_CROSSED   (1 << 1)

#endif /* _NXP_SIMTEMP_H_ */