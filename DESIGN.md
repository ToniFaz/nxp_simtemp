
Architecture

┌─────────────────┐    sysfs/ioctl    ┌──────────────────┐
│  Userspace      │◄─────────────────►│   Kernel Space   │
│  Application    │                   │                  │
│                 │                   │ ┌──────────────┐ │
│                 │                   │ │NXP Simtemp   │ │
│                 │                   │ │ Driver       │ │
└─────────────────┘                   │ │              │ │
         │                            │ │ ┌──────────┐ │ │
         │ /dev/simtemp               │ │ │Timer     │ │ │
         ▼                            │ │ │Callback  │ │ │
┌─────────────────┐                   │ │ └──────────┘ │ │
│  SysFS Interface│                   │ │     │        │ │
│ /sys/class/...  │                   │ │ ┌──────────┐ │ │
└─────────────────┘                   │ │ │Buffer    │ │ │
                                      │ │ │Management│ │ │
                                      │ │ └──────────┘ │ │
                                      │ └──────────────┘ │
                                      └──────────────────┘
Userspace ↔ Kernel Communication
Character Device (/dev/simtemp): Primary interface for data reading
read(): Blocks until temperature samples are available
poll()/select(): Notifies when data is ready or threshold events occur
ioctl(): Control operations (set sampling, threshold, mode, get stats)

SysFS Interface: Configuration and monitoring
/sys/class/nxp_simtemp/simtemp/sampling_ms - RW sampling interval
/sys/class/nxp_simtemp/simtemp/threshold_mC - RW temperature threshold
/sys/class/nxp_simtemp/simtemp/mode - RW operation mode
/sys/class/nxp_simtemp/simtemp/stats - RO driver statistics

