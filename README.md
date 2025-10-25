# nxp_simtemp
 Build a small system that simulates a hardware sensor in the Linux kernel and exposes it to user space



video
https://drive.google.com/file/d/1azi5fwyC3uo7WkWqxI3HC1SKLlSGD5zI/view?usp=sharing


├─ simtemp/
│  ├─ Makefile
│  ├─ nxp_simtemp.c
│  ├─ nxp_simtemp.h
|  |  decode_sample.c
│  └─ scripts/
│     └─ build.sh  // 
|     └─ demo.sh   //

1) compilar
gcc -o decode_samples decode_samples.c
chmod +x build.sh demo.sh

2) then
./scripts/build.sh
./scripts/demo.sh

echo "50" | sudo tee /sys/class/simtemp/simtemp/device/sampling_ms
echo "42000" | sudo tee /sys/class/simtemp/simtemp/device/threshold_mC
echo "ramp" | sudo tee /sys/class/simtemp/simtemp/device/mode

sudo dd if=/dev/simtemp of=./test.bin bs=16 count=10
./decode_samples test.bin





                                      


