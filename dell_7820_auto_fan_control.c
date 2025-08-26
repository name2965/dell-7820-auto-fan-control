#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/signalfd.h>
#include <pthread.h>
#include <linux/i8k.h>
#include <unistd.h>

#include "dell_7820_auto_fan_control.h"

char machine_id[16] = {'\0',};
int min_temp, max_temp;

void dell_proc_show()
{
    int cpu0_temp = dell_get_temp(TEMP_CPU_0);
    int cpu0_fan = dell_get_fan_speed(I8K_FAN_CPU_0);
    int cpu0_status = dell_get_fan_status(I8K_FAN_CPU_0);

    int cpu1_temp = dell_get_temp(TEMP_CPU_1);
    int cpu1_fan = dell_get_fan_speed(I8K_FAN_CPU_1);
    int cpu1_status = dell_get_fan_status(I8K_FAN_CPU_1);

    int sys0_fan = dell_get_fan_speed(I8K_FAN_SYS_0);
    int sys0_status = dell_get_fan_status(I8K_FAN_SYS_0);

    int sys1_fan = dell_get_fan_speed(I8K_FAN_SYS_1);
    int sys1_status = dell_get_fan_status(I8K_FAN_SYS_1);

    int sys2_fan = dell_get_fan_speed(I8K_FAN_SYS_2);
    int sys2_status = dell_get_fan_status(I8K_FAN_SYS_2);

    int rear0_fan = dell_get_fan_speed(I8K_FAN_REAR_0);
    int rear0_status = dell_get_fan_status(I8K_FAN_REAR_0);

    int rear1_fan = dell_get_fan_speed(I8K_FAN_REAR_1);
    int rear1_status = dell_get_fan_status(I8K_FAN_REAR_1);

    printf("============================================================\n");
    printf("Dell Precision Workstation 7820 Auto Fan Control - name2965\n");
    printf("============================================================\n");
    printf("Machine ID : %s\n", machine_id);
    printf("CPU 0 Temp(C): %d\n", cpu0_temp);
    printf("CPU 1 Temp(C): %d\n", cpu1_temp);
    printf("Min Temp(C): %d\n", min_temp);
    printf("Max Temp(C): %d\n", max_temp);
    printf("------------------------------------------------------------\n");
    printf("%-6s  %-7s  %8s  %6s\n", "IDX", "NAME", "RPM", "STATE");
    printf("------------------------------------------------------------\n");
    printf("%-6d  %-7s  %8d  %s\n", I8K_FAN_CPU_0, fan_name[I8K_FAN_CPU_0], cpu0_fan, fan_status[cpu0_status]);
    printf("%-6d  %-7s  %8d  %s\n", I8K_FAN_CPU_1, fan_name[I8K_FAN_CPU_1], cpu1_fan, fan_status[cpu1_status]);
    printf("%-6d  %-7s  %8d  %s\n", I8K_FAN_SYS_0, fan_name[I8K_FAN_SYS_0], sys0_fan, fan_status[sys0_status]);
    printf("%-6d  %-7s  %8d  %s\n", I8K_FAN_SYS_1, fan_name[I8K_FAN_SYS_1], sys1_fan, fan_status[sys1_status]);
    printf("%-6d  %-7s  %8d  %s\n", I8K_FAN_SYS_2, fan_name[I8K_FAN_SYS_2], sys2_fan, fan_status[sys2_status]);
    printf("%-6d  %-7s  %8d  %s\n", I8K_FAN_REAR_0, fan_name[I8K_FAN_REAR_0], rear0_fan, fan_status[rear0_status]);
    printf("%-6d  %-7s  %8d  %s\n", I8K_FAN_REAR_1, fan_name[I8K_FAN_REAR_1], rear1_fan, fan_status[rear1_status]);
    printf("============================================================\n");
}

void dell_set_fan(int speed)
{
    dell_set_fan_speed(I8K_FAN_REAR_0, speed);
    dell_set_fan_speed(I8K_FAN_REAR_1, speed);
    dell_set_fan_speed(I8K_FAN_CPU_0, speed);
    dell_set_fan_speed(I8K_FAN_CPU_1, speed);
    dell_set_fan_speed(I8K_FAN_SYS_0, speed);
    dell_set_fan_speed(I8K_FAN_SYS_1, speed);
    dell_set_fan_speed(I8K_FAN_SYS_2, speed);
}

int find_dell_smm_hwmon()
{
    int n;

    for (int i = 0; i <= 100; i++) {
        char buf[BUF_MAX_SIZE] = {'\0',};
        char path[BUF_MAX_SIZE] = {'\0',};

        snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/name", i);

        n = read_file_content(path, buf);
        if (n < 0)
            continue;
        if (strncmp("dell_smm", buf, 8) != 0)
            continue;

        snprintf(path, sizeof(path), "/sys/class/hwmon/hwmon%d/", i);
        memcpy(hwmon_path, path, BUF_MAX_SIZE);
        return 0;
    }

    return -1;
}

int main(int argc, char **argv)
{
    if (install_signalfd() < 0) {
        perror("[-] install_signalfd");
        return 1;
    }

    i8k_fd = open(I8K_PROC, O_RDONLY);
    if (i8k_fd < 0) {
        perror("[-] open /proc/i8k");
        return 1;
    }

    if (find_dell_smm_hwmon() < 0) {
        perror("[-] find hwmon path");
        return 1;
    }

    printf("Min Temp(C) > ");
    scanf("%d", &min_temp);
    if (min_temp < 0 || min_temp > 127) {
        perror("[-] Invalid min temp (out of range, 0 <= temp <= 127)");
        return 1;
    }
    printf("Max Temp(C) > ");
    scanf("%d", &max_temp);
    if (max_temp < 0 || max_temp > 127) {
        perror("[-] Invalid max temp (out of range, 0 <= temp <= 127)");
        return 1;
    } else if (max_temp <= min_temp) {
        perror("[-] Invalid max temp (max_temp <= min_temp)");
        return 1;
    }

    atexit(atexit_close);
    atexit(atexit_cleanup);
    atexit(term_leave_alt);

    setvbuf(stdout, NULL, _IONBF, 0);
    term_enter_alt();

    dell_get_machine_id(machine_id);
    int min_temp_cnt = 0, max_temp_cnt = 0;

    for (;;) {
        term_clear_home();
        dell_proc_show();

        int cpu0_temp = dell_get_temp(TEMP_CPU_0);
        int cpu1_temp = dell_get_temp(TEMP_CPU_1);

        if (cpu0_temp >= max_temp || cpu1_temp >= max_temp)
            max_temp_cnt++;
        else if (cpu0_temp < min_temp || cpu1_temp < min_temp)
            min_temp_cnt++;

        if (max_temp_cnt >= 5) {
            dell_set_fan(I8K_FAN_HIGH);
            max_temp_cnt = 0;
        } else if (min_temp_cnt >= 5) {
            dell_set_fan(I8K_FAN_LOW);
            min_temp_cnt = 0;
        }

        struct timespec ts = { .tv_sec = 2, .tv_nsec = 0 };
        nanosleep(&ts, NULL);
    }

    return 0;
}
