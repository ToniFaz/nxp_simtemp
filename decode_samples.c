#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

struct simtemp_sample {
    uint64_t timestamp_ns;
    int32_t temp_mC;
    uint32_t flags;
} __attribute__((packed));

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <sample.bin>\n", argv[0]);
        return 1;
    }
    
    FILE *file = fopen(argv[1], "rb");
    if (!file) {
        perror("Failed to open file");
        return 1;
    }
    
    struct simtemp_sample sample;
    int count = 0;
    
    printf("=== Temperature Samples ===\n");
    printf("%-4s %-15s %-12s %-8s %s\n", 
           "#", "Timestamp(ns)", "Temp(°C)", "Temp(mC)", "Flags");
    printf("-------------------------------------------------\n");
    
    while (fread(&sample, sizeof(sample), 1, file) == 1) {
        double temp_c = sample.temp_mC / 1000.0;
        printf("%-4d %-15llu %-10.2f°C %-8d 0x%08X", 
               ++count,
               (unsigned long long)sample.timestamp_ns,
               temp_c,
               sample.temp_mC,
               sample.flags);
        // Decode flags
        if (sample.flags & 0x1) printf(" NEW_SAMPLE");
        if (sample.flags & 0x2) printf(" THRESHOLD_CROSSED");
        printf("\n");
    }
    fclose(file);
    return 0;
}
