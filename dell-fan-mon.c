/*
 * dell-fan-mon.c -- Fan monitor and control using dell-smm-hwmon(i8k) kernel module 
 * or direct SMM BIOS calls on Dell laptops.
 * 
 * Copyright (C) 2019 https://github.com/ru-ace
 * Using code for get/set temp/fan_states from https://github.com/vitorafsr/i8kutils
 * Using code for enable/disable bios fan control from https://github.com/clopez/dellfan
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/io.h>
#include <unistd.h>
#include <signal.h>
#include "dell-fan-mon.h"
struct t_state state[2]; // 0 - cpu, 1 - gpu
struct t_fan_state fan_state[MAX_FANS];

struct t_cfg cfg = {
    .mode = 1,                  // 0 - i8k, 1 - smm
    .fan_ctrl_logic_mode = 0,   // 0 - default (see end of dell-fan-mon.conf), 1 - allow bios to control fans: stops/starts fans оnly at boundary temps
    .bios_disable_method = 0,   // 0 - allow bios to control fans, 1/2 - smm calls for disabling bios fan control from https://github.com/clopez/dellfan
    .period = 1000,             // period in ms for checking temp and setting fans
    .fan_check_period = 1000,   // period in ms for check fans state and recover it (if bios control fans)
    .jump_timeout = 2000,       // period in ms, when cpu temp was ingoring, after detected abnormal temp jump
    .jump_temp_delta = 5,       // cpu temp difference between checks(.period), at which the new value is considered abnormal
    .t_low = 45,                // low cpu temp threshold
    .t_mid = 60,                // mid cpu temp threshold
    .t_high = 80,               // high cpu temp threshold
    .t_low_fan = I8K_FAN_OFF,   // fan state for low cpu temp threshold
    .t_mid_fan = I8K_FAN_LOW,   // fan state for mid cpu temp threshold
    .t_high_fan = I8K_FAN_HIGH, // fan state for high cpu temp threshold
    .verbose = false,           // start in verbose mode
    .daemon = false,            // run daemonize()?
    .foolproof_checks = true,   // run foolproof_checks()?
    .monitor_only = false,      // get_only mode - just monitor cpu temp & fan state(.monitor_fan_id)
    .tick = 100,                // internal step in ms of main monitor loop
    .discrete_gpu_mode = 1,     // 0 - cpu integrated, 1 - use max(cpu_temp, gpu_temp), 2 - separate control for gpu and cpu fans
    .cpu_temp_sensor_id = 9,    // 9 - autodetect(use info from smm bios)
    .gpu_temp_sensor_id = 9,    // 9 - autodetect(use info from smm bios)
    .gpu_fan_id = 9,            // 0 - right, 1 - left, 9 - autodetect(use info from smm bios)
    .fans_count = 9,            // 9 - autodetect(use info from smm bios)
    .skip_signature_check = 0,  // skip check_dell_smm_signature()
    .get_gpu_temp_cmd = NULL,   // command for obtains gpu temp if gpu_temp_sensor_id autodetect fails
    .test_mode = false,         // try config with --test and exit
};

//i8kctl/smm start
int i8k_fd;

int i8k_presence()
{
    i8k_fd = open(I8K_PROC, O_RDONLY);
    if (i8k_fd < 0)
        return false;

    close(i8k_fd);
    return true;
}

void i8k_init()
{
    i8k_fd = open(I8K_PROC, O_RDONLY);
    if (i8k_fd < 0)
    {
        show_header();
        perror("Can't open " I8K_PROC);
        puts("Seems missing dell-smm-hwmon(i8k) kernel module");
        puts("You can try \"--mode 1\" for using Dell SMM BIOS calls");
        exit(EXIT_FAILURE);
    }
}
void scan_fans()
{
    if (cfg.verbose)
        printf("Fans autodetect:");

    if (!(cfg.fans_count == 9 || (cfg.gpu_fan_id == 9 && cfg.discrete_gpu_mode == 2)))
    {
        puts(" skipped - all params set by config");
        return;
    }

    for (int fan_id = 0; fan_id < MAX_FANS; fan_id++)
    {
        int fan_type = smm_send(I8K_SMM_GET_FAN_TYPE, fan_id);

        if (cfg.verbose)
            printf(" ƒ%d·%d", fan_id, fan_type);

        if (fan_type < 0)
        {
            if (cfg.fans_count == 9)
                cfg.fans_count = fan_id;
            break;
        }
        else if (fan_type == 0)
        {
            if (cfg.verbose)
                printf("·cpu");
        }
        else if (fan_type == 2 && cfg.gpu_fan_id == 9 && cfg.discrete_gpu_mode == 2)
        {
            if (cfg.verbose)
                printf("·gpu");
            cfg.gpu_fan_id = fan_id;
        }
    }
    if (cfg.verbose)
        printf(" fans_count = %d\n", cfg.fans_count);

    if ((cfg.fans_count == 9 || (cfg.gpu_fan_id == 9 && cfg.discrete_gpu_mode == 2)))
        exit_failure("Couldn't autodetect gpu_fan_id.\nSet it manualy on your own risk.");
}

void scan_sensors()
{
    if (cfg.verbose)
        printf("Sensors autodetect:");

    for (int sensor_id = 0; sensor_id < MAX_SENSORS; sensor_id++)
    {
        int sensor_type = smm_send(I8K_SMM_GET_TEMP_TYPE, sensor_id);
        if (cfg.verbose)
            printf(" t%d·%d", sensor_id, sensor_type);

        if (sensor_type >= 0)
        {
            int temp = smm_send(I8K_SMM_GET_TEMP, sensor_id);

            if (temp > 0 && temp < 128)
            {
                if (sensor_type == 0 && cfg.cpu_temp_sensor_id == 9)
                {
                    if (cfg.verbose)
                        printf("·cpu");
                    cfg.cpu_temp_sensor_id = sensor_id;
                }
                else if (sensor_type == 1 && cfg.gpu_temp_sensor_id == 9)
                {
                    if (cfg.verbose)
                        printf("·gpu");
                    cfg.gpu_temp_sensor_id = sensor_id;
                }
            }
            if (cfg.verbose)
                printf("·%d°", temp);
        }
    }
    if (cfg.verbose)
        puts("");

    if (cfg.cpu_temp_sensor_id == 9)
        exit_failure("Couldn't autodetect cpu_temp_sensor_id.");

    if (cfg.discrete_gpu_mode && cfg.gpu_temp_sensor_id == 9)
    {
        if (cfg.get_gpu_temp_cmd != NULL)
        {
            get_gpu_temp_via_cmd();
            cfg.gpu_temp_sensor_id = -1;
        }
        else
        {
            if (cfg.verbose || cfg.test_mode)
            {

                puts("Couldn't autodetect gpu_temp_sensor_id. Seems there no discrete gpu.");
                puts("Using discrete_gpu_mode 0 (cpu temp monitor only)\n");
                puts("If you don't have discrete GPU - you can ignore this message.\n");
                puts("PLEASE NOTICE: If you have discrete gpu and can get temp via command line utilites");
                puts("You can set get_gpu_temp_cmd in " CFG_FILE " to provide command for acquiring gpu temp");
                puts("As output of command dell-fan-mon awaiting line with number.\n");
                if (cfg.test_mode)
                    exit_failure("");
            }
            cfg.discrete_gpu_mode = 0;
        }
    }
}

void set_fan_state(int fan, int state)
{
    if (cfg.mode)
    {
        //smm
        if (smm_send(I8K_SMM_SET_FAN, fan + (state << 8)) == -1)
            exit_failure("set_fan_state smm_send error");
    }
    else
    {
        //i8k
        int args[2] = {fan, state};
        if (ioctl(i8k_fd, I8K_SET_FAN, &args) != 0)
        {
            perror("set_fan_state ioctl error");
            exit_failure("");
        }
    }
}

int get_fan_state(int fan)
{
    if (cfg.mode)
    {
        //smm
        usleep(50);
        int res = smm_send(I8K_SMM_GET_FAN, fan);
        if (res == -1)
            exit_failure("get_fan_state smm_send error");
        else
            return res;
    }
    else
    {
        //i8k
        int args[1] = {fan};
        if (ioctl(i8k_fd, I8K_GET_FAN, &args) == 0)
            return args[0];
        perror("get_fan_state ioctl error");
        exit_failure("");
    }
    return -1;
}

int get_temp(int sensor_id)
{
    if (sensor_id == -1)
        return get_gpu_temp_via_cmd();

    if (cfg.mode)
    {
        //smm
        int res = smm_send(I8K_SMM_GET_TEMP, sensor_id);
        if (res == -1)
            exit_failure("get_temp smm_send error");
        else
            return res;
    }
    else
    {
        //i8k
        int args[1];
        if (ioctl(i8k_fd, I8K_GET_TEMP, &args) == 0)
            return args[0];
        perror("get_cpu_temp ioctl error");
        exit_failure("");
    }
    return -1;
}
//i8kctl/smm end

// dellfan start
void smm_init()
{
    if (geteuid() != 0)
    {
        show_header();
        exit_failure("For using \"mode 1\"(smm) you need root privileges");
    }
    else
    {
        smm_init_ioperm();
        if (!cfg.skip_signature_check && !(smm_check_dell_signature(I8K_SMM_GET_DELL_SIG1) || smm_check_dell_signature(I8K_SMM_GET_DELL_SIG2)))
        {
            //last chance: is dell-smm-hwmon(i8k) kernel module loaded?
            if (!i8k_presence())
            {
                show_header();
                exit_failure("Dell SMM BIOS signature not detected: dell-fan-mon works only on Dell Laptops.\nYou can disable this check ON YOUR ON RISK with --skip_signature_check in command line or \"skip_signature_check 1\" in config file.");
            }
        }
    }
}

void smm_init_ioperm()
{
    if (ioperm(0xb2, 4, 1))
    {
        perror("init_ioperm");
        exit_failure("");
    }
    if (ioperm(0x84, 4, 1))
    {
        perror("init_ioperm");
        exit_failure("");
    }
}

int smm_asm_call(struct smm_regs *regs)
{
    int rc;
    int eax = regs->eax;
    asm volatile("pushq %%rax\n\t"
                 "movl 0(%%rax),%%edx\n\t"
                 "pushq %%rdx\n\t"
                 "movl 4(%%rax),%%ebx\n\t"
                 "movl 8(%%rax),%%ecx\n\t"
                 "movl 12(%%rax),%%edx\n\t"
                 "movl 16(%%rax),%%esi\n\t"
                 "movl 20(%%rax),%%edi\n\t"
                 "popq %%rax\n\t"
                 "out %%al,$0xb2\n\t"
                 "out %%al,$0x84\n\t"
                 "xchgq %%rax,(%%rsp)\n\t"
                 "movl %%ebx,4(%%rax)\n\t"
                 "movl %%ecx,8(%%rax)\n\t"
                 "movl %%edx,12(%%rax)\n\t"
                 "movl %%esi,16(%%rax)\n\t"
                 "movl %%edi,20(%%rax)\n\t"
                 "popq %%rdx\n\t"
                 "movl %%edx,0(%%rax)\n\t"
                 "pushfq\n\t"
                 "popq %%rax\n\t"
                 "andl $1,%%eax\n"
                 : "=a"(rc)
                 : "a"(regs)
                 : "%ebx", "%ecx", "%edx", "%esi", "%edi", "memory");

    if (rc != 0 || (regs->eax & 0xffff) == 0xffff || regs->eax == eax)
        return -1;

    return 0;
}

int smm_send(unsigned int cmd, unsigned int arg)
{
    usleep(1000);
    struct smm_regs regs = {
        .eax = cmd,
        .ebx = arg,
    };

    int res = smm_asm_call(&regs);
    //if (cfg.verbose)
    //printf("smm_send(%#06x, %#06x): i8k_smm returns %#06x, eax = %#06x\n", cmd, arg, res, regs.eax);
    return res == -1 ? res : regs.eax;
}

int smm_check_dell_signature(unsigned int signature_cmd)
{
    usleep(1000);
    struct smm_regs regs = {
        .eax = signature_cmd,
        .ebx = 0,
    };
    int res = smm_asm_call(&regs);
    //if (cfg.verbose)
    //printf("dell_smm_signature: %#06x, %#06x, %#06x\n\n", res, regs.eax, regs.edx);

    // regs.edx = DELL 0x44454c4c
    // regs.eax = DIAG 0x44494147
    return res == 0 && regs.eax == 0x44494147 && regs.edx == 0x44454c4c;
}

void set_bios_fan_control(int enable)
{
    if (!cfg.mode)
    {
        if (geteuid() != 0)
            exit_failure("For using \"bios_disable_method\" you need root privileges");

        smm_init_ioperm();
    }
    int res = -1;
    if (cfg.bios_disable_method == 1)
    {
        if (enable)
            res = smm_send(ENABLE_BIOS_METHOD1, 0);
        else
            res = smm_send(DISABLE_BIOS_METHOD1, 0);
    }
    else if (cfg.bios_disable_method == 2)
    {
        if (enable)
            res = smm_send(ENABLE_BIOS_METHOD2, 0);
        else
            res = smm_send(DISABLE_BIOS_METHOD2, 0);
    }
    else if (cfg.bios_disable_method == 3)
    {
        if (enable)
            res = smm_send(ENABLE_BIOS_METHOD3, 0);
        else
            res = smm_send(DISABLE_BIOS_METHOD3, 0);
    }
    else
        exit_failure("bios_disable_method can be 0 (dont try disable bios), 1 or 2");

    if (res == -1)
    {
        printf("%s bios fan control failed. Exiting.", enable ? "Enabling" : "Disabling");
        exit_failure("");
    }
    else if (cfg.verbose)
    {
        printf("%s bios fan control MAY BE succeeded.\n", enable ? "Enabling" : "Disabling");
    }
}
// dellfan end

// dell-fan-mon start

int get_gpu_temp_via_cmd()
{
    int temp = 0;
    static char output[256] = "";

    FILE *fp = popen(cfg.get_gpu_temp_cmd, "r");
    if (fp == NULL)
    {
        perror("Failed to run get_gpu_temp_cmd");
        exit_failure("");
    }
    if (fgets(output, sizeof(output) - 1, fp) != NULL)
        temp = atoi(output);

    pclose(fp);

    if (temp == 0)
    {
        printf("Failed to get temp from get_gpu_temp_cmd = [%s].\nAwaiting string with number, but got:\n[%s]\n", cfg.get_gpu_temp_cmd, output);
        exit_failure("");
    }
    return temp;
}

void monitor_show_legend()
{
    if (cfg.verbose)
    {
        puts("\nConfig:");
        printf("  mode                  %s\n", cfg.mode ? "smm" : "i8k");
        printf("  discrete_gpu_mode     %s\n", (cfg.discrete_gpu_mode ? (cfg.discrete_gpu_mode == 1 ? "max(cpu_temp, gpu_temp)" : "separate fans control") : "cpu integrated"));
        printf("  fan_ctrl_logic_mode   %s\n", cfg.fan_ctrl_logic_mode == 0 ? "default" : "simple");
        printf("  bios_disable_method   %d\n", cfg.bios_disable_method);
        printf("  period                %ld ms\n", cfg.period);
        if (cfg.bios_disable_method == 0)
            printf("  fan_check_period      %ld ms\n", cfg.fan_check_period);
        printf("  jump_timeout          %ld ms\n", cfg.jump_timeout);
        printf("  jump_temp_delta       %d°\n", cfg.jump_temp_delta);
        printf("  t_low  / t_low_fan    %d° / %s\n", cfg.t_low, cfg.t_low_fan ? (cfg.t_low_fan == 1 ? "low" : "high") : "off");
        if (cfg.fan_ctrl_logic_mode == 0)
            printf("  t_mid  / t_mid_fan    %d° / %s\n", cfg.t_mid, cfg.t_mid_fan ? (cfg.t_mid_fan == 1 ? "low" : "high") : "off");
        printf("  t_high / t_high_fan   %d° / %s\n", cfg.t_high, cfg.t_high_fan ? (cfg.t_high_fan == 1 ? "low" : "high") : "off");
        printf("  cpu_temp_sensor_id    %d %s\n", cfg.cpu_temp_sensor_id, cfg.mode ? "(autodetected)" : "");
        if (cfg.discrete_gpu_mode)
        {
            if (cfg.gpu_temp_sensor_id == -1)
                printf("  get_gpu_temp_cmd      [%s]\n", cfg.get_gpu_temp_cmd);
            else
                printf("  gpu_temp_sensor_id    %d (autodetected)\n", cfg.gpu_temp_sensor_id);
        }
        if (cfg.discrete_gpu_mode == 2)
        {
            printf("  gpu_fan_id            %d\n", cfg.gpu_fan_id);
        }
        puts("Legend:");
        if (cfg.discrete_gpu_mode == 0)
        {
            puts("  [TT·F] Current temp and fan state. TT - CPU temp, F - fan state");
            puts("  [ƒI=F] Set fan with id = I to state F. Fan states: 0 = OFF, 1 = LOW, 2 = HIGH");
            puts("  [¡TT!] Abnormal temp jump detected. TT - CPU temp");
        }
        else
        {
            puts("  [t****] t is temp sensor type: c = CPU, g = GPU");
            puts("  [tTT·F] Current temp and fan state. TT - temp, F - fan state");
            puts("  [ ƒI=F] Set fan with id = I to state F. Fan states: 0 = OFF, 1 = LOW, 2 = HIGH");
            puts("  [t¡TT!] Abnormal temp jump detected. TT - temp");
        }
        if (cfg.monitor_only)
        {
            puts("WARNING: working in monitor_only mode. No action will be taken. Abnormal temp jump detection disabled");
        }

        puts("Monitor:");
    }
}

void monitor()
{

    monitor_show_legend();
    monitor_init_state(0);
    monitor_init_state(1);
    monitor_init_fans_state();

    unsigned long tick = cfg.tick * 1000; // 100 ms
    int period_ticks = cfg.period / cfg.tick;
    int fan_check_period_ticks = cfg.bios_disable_method ? period_ticks : cfg.fan_check_period / cfg.tick;

    int period_tick = 0;
    int fan_check_period_tick = 0;

    while (1)
    {
        usleep(tick);

        period_tick -= period_tick == 0 ? 0 : 1;
        fan_check_period_tick -= fan_check_period_tick == 0 ? 0 : 1;
        state[0].ignore_current_temp -= state[0].ignore_current_temp == 0 ? 0 : 1;
        state[1].ignore_current_temp -= state[1].ignore_current_temp == 0 ? 0 : 1;
        if (period_tick && (fan_check_period_tick || cfg.monitor_only))
            continue;

        // get real fan state
        if (fan_check_period_tick == 0)
        {
            fan_check_period_tick = fan_check_period_ticks;
            monitor_get_fans_state();
        }

        // get temp and use fan control logic
        if (period_tick == 0)
        {
            period_tick = period_ticks;
            monitor_state(0);
            if (cfg.discrete_gpu_mode == 2)
                monitor_state(1);
        }

        monitor_set_fans_state();
    }
}

void monitor_init_fans_state()
{
    for (int fan_id = 0; fan_id < cfg.fans_count; fan_id++)
    {
        fan_state[fan_id].fan_state = I8K_FAN_OFF;
        fan_state[fan_id].real_fan_state = 9; // gets fans state on start
        fan_state[fan_id].last_setted_fan_state = -1;
        if (cfg.discrete_gpu_mode == 2 && fan_id == cfg.gpu_fan_id)
            fan_state[fan_id].state_id = 1;
        else
            fan_state[fan_id].state_id = 0;
    }
}
void monitor_init_state(int state_id)
{
    monitor_get_temp_state(state_id);
    state[state_id].temp_prev = state[state_id].temp;
    state[state_id].jump_timeout_ticks = cfg.jump_timeout / cfg.tick;
    state[state_id].ignore_current_temp = 0; // jump_timeout_tick
}
void monitor_set_fans_state()
{
    for (int fan_id = 0; fan_id < cfg.fans_count; fan_id++)
    {
        if ((fan_state[fan_id].fan_state != fan_state[fan_id].last_setted_fan_state) && (fan_state[fan_id].fan_state != fan_state[fan_id].real_fan_state) && !cfg.monitor_only)
        {
            if (cfg.verbose)
            {
                printf(" ƒ%d=%d" MON_SPACE, fan_id, fan_state[fan_id].fan_state);
                fflush(stdout);
            }
            fan_state[fan_id].last_setted_fan_state = fan_state[fan_id].fan_state;
            set_fan_state(fan_id, fan_state[fan_id].fan_state);
        }
    }
}

void monitor_get_fans_state()
{
    for (int fan_id = 0; fan_id < cfg.fans_count; fan_id++)
    {
        if (cfg.bios_disable_method && fan_state[fan_id].real_fan_state != 9)
        {
            //don't monitor fans states on bios_disable_method
            fan_state[fan_id].real_fan_state = fan_state[fan_id].fan_state;
        }
        else
        {
            fan_state[fan_id].real_fan_state = get_fan_state(fan_id);
        }

        if (fan_state[fan_id].real_fan_state == fan_state[fan_id].last_setted_fan_state)
            fan_state[fan_id].last_setted_fan_state = -1;
    }
}

void monitor_get_temp_state(int state_id)
{
    if (cfg.discrete_gpu_mode == 1)
    {
        int cpu_temp = get_temp(cfg.cpu_temp_sensor_id);
        int gpu_temp = get_temp(cfg.gpu_temp_sensor_id);
        state[state_id].temp_type = cpu_temp > gpu_temp ? 'c' : 'g';
        state[state_id].temp = cpu_temp > gpu_temp ? cpu_temp : gpu_temp;
    }
    else if (cfg.discrete_gpu_mode == 2)
    {
        state[state_id].temp_type = state_id == 0 ? 'c' : 'g';
        state[state_id].temp = state_id == 0 ? get_temp(cfg.cpu_temp_sensor_id) : get_temp(cfg.gpu_temp_sensor_id);
    }
    else
    {
        state[state_id].temp_type = ' ';
        state[state_id].temp = get_temp(cfg.cpu_temp_sensor_id);
    }
}

void monitor_state(int state_id)
{
    int real_fan_state = 0;
    if (!state[state_id].ignore_current_temp)
    {
        state[state_id].temp_prev = state[state_id].temp;
        monitor_get_temp_state(state_id);
        if (state[state_id].temp - state[state_id].temp_prev > cfg.jump_temp_delta && !cfg.monitor_only)
            // abnormal temp jump detected
            state[state_id].ignore_current_temp = state[state_id].jump_timeout_ticks;
        else
        {
            for (int fan_id = 0; fan_id < cfg.fans_count; fan_id++)
            {
                if (fan_state[fan_id].state_id == state_id)
                {
                    fan_state[fan_id].last_setted_fan_state = -1;
                    if (cfg.fan_ctrl_logic_mode)
                    {
                        // simple: allow bios to control fans: stops/starts fans оnly at boundary temps
                        if (state[state_id].temp <= cfg.t_low && fan_state[fan_id].real_fan_state > cfg.t_low_fan)
                            fan_state[fan_id].fan_state = cfg.t_low_fan;
                        else if (state[state_id].temp >= cfg.t_high && fan_state[fan_id].real_fan_state < cfg.t_high_fan)
                            fan_state[fan_id].fan_state = cfg.t_high_fan;
                        else
                            fan_state[fan_id].fan_state = fan_state[fan_id].real_fan_state;
                    }
                    else
                    {
                        // default fan control logic
                        if (state[state_id].temp <= cfg.t_low)
                            fan_state[fan_id].fan_state = cfg.t_low_fan;
                        else if (state[state_id].temp > cfg.t_high)
                            fan_state[fan_id].fan_state = cfg.t_high_fan;
                        else if (state[state_id].temp >= cfg.t_mid)
                            fan_state[fan_id].fan_state = fan_state[fan_id].fan_state > cfg.t_mid_fan ? fan_state[fan_id].fan_state : cfg.t_mid_fan;
                    }
                    real_fan_state = fan_state[fan_id].real_fan_state;
                }
            }
        }
    }
    if (cfg.verbose)
    {
        if (state[state_id].ignore_current_temp)
            printf("%c¡%d!" MON_SPACE, state[state_id].temp_type, state[state_id].temp);
        else
            printf("%c%d·%d" MON_SPACE, state[state_id].temp_type, state[state_id].temp, real_fan_state);

        fflush(stdout);
    }
}

void foolproof_checks()
{
    int check_failed = false;
    check_failed += (cfg.mode != 0 && cfg.mode != 1) ? foolproof_error("mode = 0 (use i8k module) or 1 (direct smm calls) ") : false;
    check_failed += (cfg.fan_ctrl_logic_mode != 0 && cfg.fan_ctrl_logic_mode != 1) ? foolproof_error("fan_ctrl_logic_mode = 0 (default fan control logic) or 1 (stops/starts fans оnly at boundary temps) ") : false;
    check_failed += (cfg.bios_disable_method < 0 || cfg.bios_disable_method > 2) ? foolproof_error("bios_disable_method in [0,2]") : false;

    check_failed += (cfg.t_low < 30) ? foolproof_error("t_low >= 30") : false;
    check_failed += (cfg.t_high > 90) ? foolproof_error("t_high <= 90") : false;
    check_failed += (cfg.t_low < cfg.t_mid && cfg.t_mid < cfg.t_high) ? false : foolproof_error("thresholds t_low < t_mid < t_high");

    check_failed += (cfg.t_low_fan < I8K_FAN_OFF || cfg.t_low_fan > I8K_FAN_MAX) ? foolproof_error("t_low_fan = 0(OFF), 1(LOW), 2(HIGH)") : false;
    check_failed += (cfg.t_mid_fan < I8K_FAN_OFF || cfg.t_mid_fan > I8K_FAN_MAX) ? foolproof_error("t_mid_fan = 0(OFF), 1(LOW), 2(HIGH)") : false;
    check_failed += (cfg.t_high_fan < I8K_FAN_OFF || cfg.t_high_fan > I8K_FAN_MAX) ? foolproof_error("t_high_fan = 0(OFF), 1(LOW), 2(HIGH)") : false;

    check_failed += (cfg.period < 100 || cfg.period > 5000) ? foolproof_error("period in [100,5000]") : false;
    check_failed += (cfg.fan_check_period > cfg.period) ? foolproof_error("fan_check_period <= period") : false;
    check_failed += (cfg.fan_check_period < 100 || cfg.fan_check_period > 5000) ? foolproof_error("fan_check_period in [100,5000]") : false;
    check_failed += (cfg.jump_timeout < 100 || cfg.jump_timeout > 5000) ? foolproof_error("jump_timeout in [100,5000]") : false;
    check_failed += (cfg.jump_temp_delta < 2) ? foolproof_error("jump_temp_delta > 2") : false;

    check_failed += (cfg.discrete_gpu_mode != 0 && cfg.discrete_gpu_mode != 1 && cfg.discrete_gpu_mode != 2) ? foolproof_error("discrete_gpu_mode = 0 (cpu integrated) or 1 (max(cpu_temp,gpu_temp)) or 2 (separeted fan control)") : false;
    check_failed += (cfg.gpu_fan_id != 0 && cfg.gpu_fan_id != 1 && cfg.gpu_fan_id != 9) ? foolproof_error("gpu_fan_id = 0(left) or 1(right) or 9(autodetect)") : false;
    //check_failed += !((cfg.gpu_temp_sensor_id >= 0 && cfg.gpu_temp_sensor_id <= 3) || cfg.gpu_temp_sensor_id == 9) ? foolproof_error("gpu_temp_sensor_id = 0...3 or 9(autodetect)") : false;
    check_failed += (cfg.fans_count < 1 || cfg.fans_count > 3) && cfg.fans_count != 9 ? foolproof_error("fans_count = 1,2,3 or 9(autodetect)") : false;

    if (check_failed)
        exit_failure("foolproof_checks() was failed.\nIf you are sure what you do, checks can be disabled using \"--foolproof_checks 0\" in command line or add \"foolproof_checks 0\" in " CFG_FILE);
}

int foolproof_error(char *str)
{
    printf("foolproof_checks(): awaiting %s\n", str);
    return true;
}

void exit_failure(const char *str)
{
    puts(str);
    exit(EXIT_FAILURE);
}

void cfg_load()
{
    if (access(CFG_FILE, F_OK) == -1)
        return;

    FILE *fh;
    if ((fh = fopen(CFG_FILE, "r")) == NULL)
        return;

    char *str = malloc(256);
    int line_id = 0;
    while (!feof(fh))
        if (fgets(str, 254, fh))
        {
            line_id++;
            char *pos = strstr(str, "\n");
            pos[0] = '\0';
            if (str[0] == '#' || str[0] == ';' || str[0] == '\0' || isspace(str[0]))
                continue;
            pos = str;
            while (!isspace(pos[0]) && pos[0] != '\0')
                pos++;

            if (pos[0] == '\0')
                cfg_error(line_id);

            pos[0] = '\0';
            pos++;

            if (strcmp(str, "get_gpu_temp_cmd") == 0)
            {
                while (isspace(pos[0]) && pos[0] != '\0')
                    pos++;
                if (pos[0] == '\0')
                    cfg_error(line_id);
                int len = strlen(pos);
                cfg.get_gpu_temp_cmd = (char *)malloc((len + 1) * sizeof(char));
                int i;
                for (i = 0; i < len; i++)
                    cfg.get_gpu_temp_cmd[i] = pos[i];
                cfg.get_gpu_temp_cmd[len] = 0;
                continue;
            }

            while (!isdigit(pos[0]) && pos[0] != '\0')
                pos++;

            if (pos[0] == '\0')
                cfg_error(line_id);

            int value = atoi(pos);
            cfg_set(str, value, line_id);
        }

    free(str);
    fclose(fh);
}

void cfg_error(int line_id)
{
    printf("Error while loading " CFG_FILE " in line %d\n", line_id);
    exit_failure("");
}

void cfg_set(char *key, int value, int line_id)
{
    if (strcmp(key, "daemon") == 0)
        cfg.daemon = value;
    else if (strcmp(key, "period") == 0)
        cfg.period = value;
    else if (strcmp(key, "fan_check_period") == 0)
        cfg.fan_check_period = value;
    else if (strcmp(key, "jump_timeout") == 0)
        cfg.jump_timeout = value;
    else if (strcmp(key, "jump_temp_delta") == 0)
        cfg.jump_temp_delta = value;
    else if (strcmp(key, "t_low") == 0)
        cfg.t_low = value;
    else if (strcmp(key, "t_mid") == 0)
        cfg.t_mid = value;
    else if (strcmp(key, "t_high") == 0)
        cfg.t_high = value;
    else if (strcmp(key, "t_low_fan") == 0)
        cfg.t_low_fan = value;
    else if (strcmp(key, "t_mid_fan") == 0)
        cfg.t_mid_fan = value;
    else if (strcmp(key, "t_high_fan") == 0)
        cfg.t_high_fan = value;
    else if (strcmp(key, "foolproof_checks") == 0)
        cfg.foolproof_checks = value;
    else if (strcmp(key, "skip_signature_check") == 0)
        cfg.skip_signature_check = value;
    else if (strcmp(key, "bios_disable_method") == 0)
        cfg.bios_disable_method = value;
    else if (strcmp(key, "mode") == 0)
        cfg.mode = value;
    else if (strcmp(key, "fan_ctrl_logic_mode") == 0)
        cfg.fan_ctrl_logic_mode = value;
    else if (strcmp(key, "discrete_gpu_mode") == 0)
        cfg.discrete_gpu_mode = value;
    else if (strcmp(key, "gpu_fan_id") == 0)
        cfg.gpu_fan_id = value;
    else if (strcmp(key, "fans_count") == 0)
        cfg.fans_count = value;
    else if (strcmp(key, "cpu_fan_id") == 0 || strcmp(key, "monitor_fan_id") == 0)
    {
        if (line_id > 0)
            printf(CFG_FILE " line %d: parameter %s is deprecated(ignoring). Please remove this line.\n", line_id, key);
        else
            printf("WARNING: argument --%s is deprecated(ignoring)\n", key);
    }
    else
    {
        if (line_id > 0)
            printf(CFG_FILE " line %d: unknown parameter %s \n", line_id, key);
        else
            printf("Unknown argument --%s\n", key);

        exit_failure("");
    }
}
void show_header()
{
    if (cfg.test_mode)
        return;
    puts("dell-fan-mon v1.2 by https://github.com/ru-ace");
    puts("Fan monitor and control for Dell laptops via direct SMM BIOS calls or dell-smm-hwmon(i8k) kernel module.\n");
}
void usage()
{
    show_header();
    puts("Usage: dell-fan-mon [OPTIONS]");
    puts("  -h  Show this help");
    puts("  -v  Verbose mode");
    puts("  -d  Daemon mode (detach from console)");
    puts("  -m  No control - monitor only (useful to monitor daemon work)");
    puts("  -t  Try config and exit (useful for scripts)");
    puts("Options (see " CFG_FILE " or manpage for explains):");
    printf("  --mode MODE                       (default: %d = %s)\n", cfg.mode, cfg.mode ? "smm" : "i8k");
    printf("  --discrete_gpu_mode MODE          (default: %d = %s)\n", cfg.discrete_gpu_mode, (cfg.discrete_gpu_mode ? (cfg.discrete_gpu_mode == 1 ? "max(cpu_temp, gpu_temp)" : "separate fans control") : "cpu integrated"));
    printf("  --fan_ctrl_logic_mode MODE        (default: %d = %s)\n", cfg.fan_ctrl_logic_mode, cfg.fan_ctrl_logic_mode == 0 ? "default" : "simple");
    printf("  --bios_disable_method METHOD      (default: %d)\n", cfg.bios_disable_method);
    printf("  --period MILLISECONDS             (default: %ld ms)\n", cfg.period);
    printf("  --fan_check_period MILLISECONDS   (default: %ld ms)\n", cfg.fan_check_period);
    printf("  --jump_timeout MILLISECONDS       (default: %ld ms)\n", cfg.jump_timeout);
    printf("  --jump_temp_delta CELSIUS         (default: %d°)\n", cfg.jump_temp_delta);
    printf("  --t_low CELSIUS                   (default: %d°)\n", cfg.t_low);
    printf("  --t_mid CELSIUS                   (default: %d°)\n", cfg.t_mid);
    printf("  --t_high CELSIUS                  (default: %d°)\n", cfg.t_high);
    printf("  --t_low_fan FAN_STATE             (default: %d = %s)\n", cfg.t_low_fan, cfg.t_low_fan ? (cfg.t_low_fan == 1 ? "low" : "high") : "off");
    printf("  --t_mid_fan FAN_STATE             (default: %d = %s)\n", cfg.t_mid_fan, cfg.t_mid_fan ? (cfg.t_mid_fan == 1 ? "low" : "high") : "off");
    printf("  --t_high_fan FAN_STATE            (default: %d = %s)\n", cfg.t_high_fan, cfg.t_high_fan ? (cfg.t_high_fan == 1 ? "low" : "high") : "off");
    printf("  --gpu_fan_id FAN_ID               (default: %d%s)\n", cfg.gpu_fan_id, (cfg.gpu_fan_id == 9 ? " = autodetect" : ""));
    printf("  --fans_count NUMBER_OF_FANS       (default: %d%s)\n", cfg.fans_count, (cfg.fans_count == 9 ? " = autodetect" : ""));

    puts("");
}
void parse_args(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++)
    {
        if ((strcmp(argv[i], "-h") == 0) || (strcmp(argv[i], "--help") == 0))
        {
            usage();
            exit(EXIT_SUCCESS);
        }
        else if ((strcmp(argv[i], "-v") == 0) || (strcmp(argv[i], "--verbose") == 0))
        {
            cfg.verbose = true;
        }
        else if ((strcmp(argv[i], "-t") == 0) || (strcmp(argv[i], "--test") == 0))
        {
            cfg.test_mode = true;
        }
        else if ((strcmp(argv[i], "-d") == 0) || (strcmp(argv[i], "--daemon") == 0))
        {
            cfg.daemon = true;
        }
        else if ((strcmp(argv[i], "-m") == 0) || (strcmp(argv[i], "--monitor_only") == 0))
        {
            cfg.monitor_only = true;
            cfg.verbose = true;
        }
        else if ((strcmp(argv[i], "--skip_signature_check") == 0))
        {
            cfg.skip_signature_check = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == '-')
        {
            char *key = argv[i];
            key += 2;
            i++;
            if (i == argc || !isdigit(argv[i][0]))
            {
                printf("Argument --%s need value\n", key);
                exit_failure("");
            }
            else
            {
                int value = atoi(argv[i]);
                cfg_set(key, value, 0);
            }
        }
        else
        {
            printf("Unknown argument %s\n", argv[i]);
            exit_failure("");
        }
    }
}

void daemonize()
{
    int pid = fork();
    if (pid == -1) //failure
    {
        perror("can't fork()");
        exit_failure("");
    }
    else if (pid == 0) //child
        cfg.verbose = false;
    else
    { //parent
        printf("%d\n", pid);
        exit(EXIT_SUCCESS);
    }
}
void signal_handler(int signal_id)
{

    if (!cfg.monitor_only)
    {
        if (cfg.verbose)
            printf("\nCatch signal %d. Restoring bios fan control.\n", signal_id);

        for (int fan_id = 0; fan_id < cfg.fans_count; fan_id++)
            set_fan_state(fan_id, I8K_FAN_HIGH);

        if (cfg.bios_disable_method == 1 || cfg.bios_disable_method == 2 || cfg.bios_disable_method == 3)
            set_bios_fan_control(true);
    }
    else if (cfg.verbose)
    {
        puts("");
    }
    exit(EXIT_SUCCESS);
}

void signal_handler_init()
{
    if (signal(SIGINT, signal_handler) == SIG_ERR)
    {
        perror("can't catch SIGINT");
        exit_failure("");
    }
    if (signal(SIGTERM, signal_handler) == SIG_ERR)
    {
        perror("can't catch SIGTERM");
        exit_failure("");
    }
}

void smm_bios_fan_control_scanner()
{
    // Please DON'T USE THIS CODE - it may be DANGEROUS
    // Also: algorithm of c based on fact from my Dell 7559:
    //       if i set fan_state = LOW(1), bios set it to HIGH(2) within 10 seconds
    //       i don't sure that this fact is true for all dell notebooks.
    // No success on Dell 7559 :(
    exit_failure("Please DON'T USE THIS CODE - it may be DANGEROUS");
    int i;
    for (i = 0x5; i <= 0xff; i++)
    {
        int cmd = (i << 8) + 0xa3;
        printf("smm_send(%#06x) ", cmd);
        int res = -1; // smm_send(cmd, 0);
        printf("returned %#06x", res);
        if (res == -1)
        {
            printf(", bad :(\n");
            continue;
        }
        else
        {
            sleep(1);
            set_fan_state(0, I8K_FAN_LOW);
            printf(", sleep(10s)");
        }

        fflush(stdout);
        sleep(10);
        int real_fan_state = get_fan_state(0);
        if (real_fan_state == 1)
        {
            printf(", SEEMS WE FOUND DISABLE BIOS FAN CONTROL :)))\n");
            exit(1);
        }
        else
        {
            printf(", fan = %d - bad :(\n", real_fan_state);
        }
    }
}

int main(int argc, char **argv)
{
    //cfg.get_gpu_temp_cmd = "nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader";

    cfg_load();
    parse_args(argc, argv);

    if (cfg.mode)
        smm_init();
    else
        i8k_init();

    if (cfg.verbose)
        show_header();

    signal_handler_init();

    if (cfg.foolproof_checks)
        foolproof_checks();

    if (cfg.fans_count != 9 && ((cfg.discrete_gpu_mode == 0) ||
                                (cfg.discrete_gpu_mode == 1 && cfg.get_gpu_temp_cmd != NULL) ||
                                (cfg.discrete_gpu_mode == 2 && cfg.get_gpu_temp_cmd != NULL && cfg.gpu_fan_id != 9)))
    {
        //can be run under unprivileged user in mode 0, cause no need to run autodetect

        //check get_gpu_temp_cmd
        if (cfg.discrete_gpu_mode > 0)
            get_gpu_temp_via_cmd();

        cfg.cpu_temp_sensor_id = 0;
        cfg.gpu_temp_sensor_id = -1;
    }
    else
    {
        if (cfg.mode != 1)
            exit_failure("Autodetection types of fans & sensors required mode = 1 (direct SMM BIOS calls)\n");

        scan_fans();
        scan_sensors();
    }
    if (cfg.test_mode)
        exit(EXIT_SUCCESS);

    if (cfg.bios_disable_method && !cfg.monitor_only)
        set_bios_fan_control(false);

    if (cfg.daemon && !cfg.monitor_only)
        daemonize();

    monitor();
}
