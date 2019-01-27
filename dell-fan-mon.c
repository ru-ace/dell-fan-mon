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
struct t_state state[2];
struct t_cfg cfg = {
    .mode = 1,                      // 0 - i8k, 1 - smm
    .fan_ctrl_logic_mode = 0,       // 0 - default (see end of dell-fan-mon.conf), 1 - allow bios to control fans: stops/starts fans оnly at boundary temps
    .bios_disable_method = 0,       // 0 - allow bios to control fans, 1/2 - smm calls for disabling bios fan control from https://github.com/clopez/dellfan
    .period = 1000,                 // period in ms for checking temp and setting fans
    .fan_check_period = 1000,       // period in ms for check fans state and recover it (if bios control fans)
    .monitor_fan_id = I8K_FAN_LEFT, // ID of fan for monitoring: 0 = right, 1 = left
    .jump_timeout = 2000,           // period in ms, when cpu temp was ingoring, after detected abnormal temp jump
    .jump_temp_delta = 5,           // cpu temp difference between checks(.period), at which the new value is considered abnormal
    .t_low = 45,                    // low cpu temp threshold
    .t_mid = 60,                    // mid cpu temp threshold
    .t_high = 80,                   // high cpu temp threshold
    .t_low_fan = I8K_FAN_OFF,       // fan state for low cpu temp threshold
    .t_mid_fan = I8K_FAN_LOW,       // fan state for mid cpu temp threshold
    .t_high_fan = I8K_FAN_HIGH,     // fan state for high cpu temp threshold
    .verbose = false,               // start in verbose mode
    .daemon = false,                // run daemonize()?
    .foolproof_checks = true,       // run foolproof_checks()?
    .monitor_only = false,          // get_only mode - just monitor cpu temp & fan state(.monitor_fan_id)
    .tick = 100,                    // internal step in ms of main monitor loop
    .discrete_gpu_mode = 1,         // 0 - cpu integrated, 1 - use max(cpu_temp/gpu_temp), 2 - separate control for gpu and cpu fans
    .cpu_fan_id = 9,                // 0 - right, 1 - left, 9 - autodetect(use info from smm bios)
    .gpu_fan_id = 9,                // 0 - right, 1 - left, 9 - autodetect(use info from smm bios)
    .gpu_temp_sensor_id = 9,        // 9 - autodetect(use info from smm bios)
};

//i8kctl/smm start
int i8k_fd;

void i8k_open()
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
int get_cpu_fan_id()
{
    int fan0_type = send_smm(I8K_SMM_GET_FAN_TYPE, 0);
    int fan1_type = send_smm(I8K_SMM_GET_FAN_TYPE, 1);
    if (cfg.verbose)
        printf("Fan types detection: right(0) is type %d, left(1) is type %d\n\n", fan0_type, fan1_type);

    if (fan0_type == 0 && fan1_type == 2)
        return 0;
    if (fan1_type == 0 && fan0_type == 2)
        return 1;
    puts("Couldn't autodetect cpu_fan_id and gpu_fan_id.\nPlease use discrete_gpu_mode = 1. Or set it manualy on your own risk.");
    exit(EXIT_FAILURE);
}
int get_gpu_temp_sensor_id()
{
    for (int sensor_id = 0; sensor_id < 4; sensor_id++)
    {
        int sensor_type = send_smm(I8K_SMM_GET_TEMP_TYPE, sensor_id);
        if (sensor_type == 1)
            return sensor_id;
        //printf("sensor_id = %d, type is %d\n", sensor_id, sensor_type);
    }
    if (cfg.verbose)
        puts("Coudn't autodetect gpu_temp_sensor_id. Seems there no discrete gpu.\nSwitch to discrete_gpu_mode 0 (cpu temp monitor only).");

    cfg.discrete_gpu_mode = 0;
    return 0;
}

void set_fan_state(int fan, int state)
{
    if (cfg.mode)
    {
        //smm
        if (send_smm(I8K_SMM_SET_FAN, fan + (state << 8)) == -1)
        {
            puts("set_fan_state send_smm error");
            exit(EXIT_FAILURE);
        }
    }
    else
    {
        //i8k
        int args[2] = {fan, state};
        if (ioctl(i8k_fd, I8K_SET_FAN, &args) != 0)
        {
            perror("set_fan_state ioctl error");
            exit(EXIT_FAILURE);
        }
    }
}

int get_fan_state(int fan)
{
    if (cfg.mode)
    {
        //smm
        usleep(50);
        int res = send_smm(I8K_SMM_GET_FAN, fan);
        if (res == -1)
        {
            puts("get_fan_state send_smm error");
            exit(EXIT_FAILURE);
        }
        else
        {
            return res;
        }
    }
    else
    {
        //i8k
        int args[1] = {fan};
        if (ioctl(i8k_fd, I8K_GET_FAN, &args) == 0)
            return args[0];
        perror("get_fan_state ioctl error");
        exit(EXIT_FAILURE);
    }
}

int get_temp(int sensor_id)
{
    if (cfg.mode)
    {
        //smm
        int res = send_smm(I8K_SMM_GET_TEMP, sensor_id);
        if (res == -1)
        {
            puts("get_temp send_smm error");
            exit(EXIT_FAILURE);
        }
        else
        {
            return res;
        }
    }
    else
    {
        //i8k
        int args[1];
        if (ioctl(i8k_fd, I8K_GET_TEMP, &args) == 0)
            return args[0];
        perror("get_cpu_temp ioctl error");
        exit(EXIT_FAILURE);
    }
}
//i8kctl/smm end

// dellfan start
void init_smm()
{
    if (geteuid() != 0)
    {
        show_header();
        puts("For using \"mode 1\"(smm) you need root privileges\n");
        exit_failure();
    }
    else
    {
        init_ioperm();
        if (!check_dell_smm_signature())
        {
            show_header();
            puts("Dell SMM BIOS signature not detected.\ndell-fan-mon works only on Dell Laptops.");
            exit_failure();
        }
    }
}

void init_ioperm()
{
    if (ioperm(0xb2, 4, 1))
    {
        perror("init_ioperm");
        exit_failure();
    }
    if (ioperm(0x84, 4, 1))
    {
        perror("init_ioperm");
        exit_failure();
    }
}

int i8k_smm(struct smm_regs *regs)
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

int send_smm(unsigned int cmd, unsigned int arg)
{
    usleep(1000);
    struct smm_regs regs = {
        .eax = cmd,
        .ebx = arg,
    };

    int res = i8k_smm(&regs);
    //if (cfg.verbose)
    //printf("send_smm(%#06x, %#06x): i8k_smm returns %#06x, eax = %#06x\n", cmd, arg, res, regs.eax);
    return res == -1 ? res : regs.eax;
}

int check_dell_smm_signature()
{
    struct smm_regs regs = {
        .eax = I8K_SMM_GET_DELL_SIGNATURE,
        .ebx = 0,
    };
    int res = i8k_smm(&regs);
    if (cfg.verbose)
        printf("dell_smm_signature: %#06x, %#06x, %#06x\n\n", res, regs.eax, regs.edx);

    // regs.edx = DELL 0x44454c4c
    // regs.eax = DIAG 0x44494147
    return res == 0 && regs.eax == 0x44494147 && regs.edx == 0x44454c4c;
}

void bios_fan_control(int enable)
{
    if (!cfg.mode)
    {
        if (geteuid() != 0)
        {
            printf("For using \"bios_disable_method\" you need root privileges\n");
            exit_failure();
        }
        init_ioperm();
    }
    int res = -1;
    if (cfg.bios_disable_method == 1)
    {
        if (enable)
            res = send_smm(ENABLE_BIOS_METHOD1, 0);
        else
            res = send_smm(DISABLE_BIOS_METHOD1, 0);
    }
    else if (cfg.bios_disable_method == 2)
    {
        if (enable)
            res = send_smm(ENABLE_BIOS_METHOD2, 0);
        else
            res = send_smm(DISABLE_BIOS_METHOD2, 0);
    }
    else if (cfg.bios_disable_method == 3)
    {
        if (enable)
            res = send_smm(ENABLE_BIOS_METHOD3, 0);
        else
            res = send_smm(DISABLE_BIOS_METHOD3, 0);
    }
    else
    {
        printf("bios_disable_method can be 0 (dont try disable bios), 1 or 2");
        exit_failure();
    }

    if (res == -1)
    {
        printf("%s bios fan control failed. Exiting.\n", enable ? "Enabling" : "Disabling");
        exit_failure();
    }
    else if (cfg.verbose)
    {
        printf("%s bios fan control MAY BE succeeded.\n", enable ? "Enabling" : "Disabling");
    }
}
// dellfan end

// dell-fan-mon start
void set_fans_state(int state)
{
    set_fan_state(I8K_FAN_LEFT, state);
    set_fan_state(I8K_FAN_RIGHT, state);
}

void monitor_show_legend()
{
    if (cfg.verbose)
    {
        puts("Config:");
        printf("  mode                  %s\n", cfg.mode ? "smm" : "i8k");
        printf("  discrete_gpu_mode     %s\n", (cfg.discrete_gpu_mode ? (cfg.discrete_gpu_mode == 1 ? "max(cpu_temp, gpu_temp)" : "separate fans control") : "cpu integrated"));
        printf("  fan_ctrl_logic_mode   %s\n", cfg.fan_ctrl_logic_mode == 0 ? "default" : "simple");
        printf("  bios_disable_method   %d\n", cfg.bios_disable_method);
        printf("  period                %ld ms\n", cfg.period);
        if (cfg.bios_disable_method == 0)
        {
            printf("  fan_check_period      %ld ms\n", cfg.fan_check_period);
            printf("  monitor_fan_id        %s\n", cfg.monitor_fan_id == I8K_FAN_RIGHT ? "right" : "left");
        }
        printf("  jump_timeout          %ld ms\n", cfg.jump_timeout);
        printf("  jump_temp_delta       %d°\n", cfg.jump_temp_delta);
        printf("  t_low  / t_low_fan    %d° / %s\n", cfg.t_low, cfg.t_low_fan ? (cfg.t_low_fan == 1 ? "low" : "high") : "off");
        if (cfg.fan_ctrl_logic_mode == 0)
            printf("  t_mid  / t_mid_fan    %d° / %s\n", cfg.t_mid, cfg.t_mid_fan ? (cfg.t_mid_fan == 1 ? "low" : "high") : "off");
        printf("  t_high / t_high_fan   %d° / %s\n", cfg.t_high, cfg.t_high_fan ? (cfg.t_high_fan == 1 ? "low" : "high") : "off");
        if (cfg.discrete_gpu_mode)
            printf("  gpu_temp_sensor_id    %d\n", cfg.gpu_temp_sensor_id);
        if (cfg.discrete_gpu_mode == 2)
        {
            printf("  cpu_fan_id            %s\n", cfg.cpu_fan_id == I8K_FAN_RIGHT ? "right" : "left");
            printf("  gpu_fan_id            %s\n", cfg.gpu_fan_id == I8K_FAN_RIGHT ? "right" : "left");
        }
        puts("Legend:");
        if (cfg.discrete_gpu_mode == 0)
        {
            puts("  [TT·F] Current temp and fan state. TT - CPU temp, F - fan state");
            puts("  [ƒ(F)] Set fans state to F. Fan states: 0 = OFF, 1 = LOW, 2 = HIGH");
            puts("  [¡TT!] Abnormal temp jump detected. TT - CPU temp");
        }
        else
        {
            puts("  [t****] t is temp sensor type: c = CPU, g = GPU");
            puts("  [tTT·F] Current temp and fan state. TT - temp, F - fan state");
            puts("  [tƒ(F)] Set fans state to F. Fan states: 0 = OFF, 1 = LOW, 2 = HIGH");
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
            monitor_get_fan_state(0);
            if (cfg.discrete_gpu_mode == 2)
                monitor_get_fan_state(1);
        }

        // get temp and use fan control logic
        if (period_tick == 0)
        {
            period_tick = period_ticks;
            monitor_state(0);
            if (cfg.discrete_gpu_mode == 2)
                monitor_state(1);
        }

        monitor_set_fan_state(0);
        if (cfg.discrete_gpu_mode == 2)
            monitor_set_fan_state(1);
    }
}

void monitor_init_state(int state_id)
{
    monitor_get_temp_state(state_id);
    state[state_id].temp_prev = state[state_id].temp;
    state[state_id].fan_state = I8K_FAN_OFF;
    state[state_id].real_fan_state = state[state_id].fan_state;
    state[state_id].jump_timeout_ticks = cfg.jump_timeout / cfg.tick;

    state[state_id].ignore_current_temp = 0; // jump_timeout_tick
    state[state_id].last_setted_fan_state = -1;
}
void monitor_set_fan_state(int state_id)
{
    // set fan state
    if ((state[state_id].fan_state != state[state_id].last_setted_fan_state) && (state[state_id].fan_state != state[state_id].real_fan_state) && !cfg.monitor_only)
    {
        if (cfg.verbose)
        {
            printf("%cƒ(%d)" MON_SPACE, state[state_id].temp_type, state[state_id].fan_state);
            fflush(stdout);
        }
        state[state_id].last_setted_fan_state = state[state_id].fan_state;
        if (cfg.discrete_gpu_mode == 2)
            set_fan_state(state_id == 0 ? cfg.cpu_fan_id : cfg.gpu_fan_id, state[state_id].fan_state);
        else
            set_fans_state(state[state_id].fan_state);
    }
}

void monitor_get_fan_state(int state_id)
{
    if (cfg.bios_disable_method)
    {
        //don't monitor fans states on bios_disable_method
        state[state_id].real_fan_state = state[state_id].fan_state;
    }
    else
    {
        if (cfg.discrete_gpu_mode == 2)
            state[state_id].real_fan_state = get_fan_state(state_id == 0 ? cfg.cpu_fan_id : cfg.gpu_fan_id);
        else
            state[state_id].real_fan_state = get_fan_state(cfg.monitor_fan_id);
    }

    if (state[state_id].real_fan_state == state[state_id].last_setted_fan_state)
        state[state_id].last_setted_fan_state = -1;
}

void monitor_get_temp_state(int state_id)
{
    if (cfg.discrete_gpu_mode == 1)
    {
        int cpu_temp = get_temp(0);
        int gpu_temp = get_temp(cfg.gpu_temp_sensor_id);
        state[state_id].temp_type = cpu_temp > gpu_temp ? 'c' : 'g';
        state[state_id].temp = cpu_temp > gpu_temp ? cpu_temp : gpu_temp;
    }
    else if (cfg.discrete_gpu_mode == 2)
    {
        state[state_id].temp_type = state_id == 0 ? 'c' : 'g';
        state[state_id].temp = state_id == 0 ? get_temp(0) : get_temp(cfg.gpu_temp_sensor_id);
    }
    else
    {
        state[state_id].temp_type = ' ';
        state[state_id].temp = get_temp(0);
    }
}

void monitor_state(int state_id)
{

    state[state_id].last_setted_fan_state = -1;
    if (!state[state_id].ignore_current_temp)
    {
        state[state_id].temp_prev = state[state_id].temp;
        monitor_get_temp_state(state_id);
        if (state[state_id].temp - state[state_id].temp_prev > cfg.jump_temp_delta && !cfg.monitor_only)
            // abnormal temp jump detected
            state[state_id].ignore_current_temp = state[state_id].jump_timeout_ticks;
        else
        {
            if (cfg.fan_ctrl_logic_mode)
            {
                // simple: allow bios to control fans: stops/starts fans оnly at boundary temps
                if (state[state_id].temp <= cfg.t_low && state[state_id].real_fan_state > cfg.t_low_fan)
                    state[state_id].fan_state = cfg.t_low_fan;
                else if (state[state_id].temp >= cfg.t_high && state[state_id].real_fan_state < cfg.t_high_fan)
                    state[state_id].fan_state = cfg.t_high_fan;
                else
                    state[state_id].fan_state = state[state_id].real_fan_state;
            }
            else
            {
                // default fan control logic
                if (state[state_id].temp <= cfg.t_low)
                    state[state_id].fan_state = cfg.t_low_fan;
                else if (state[state_id].temp > cfg.t_high)
                    state[state_id].fan_state = cfg.t_high_fan;
                else if (state[state_id].temp >= cfg.t_mid)
                    state[state_id].fan_state = state[state_id].fan_state > cfg.t_mid_fan ? state[state_id].fan_state : cfg.t_mid_fan;
            }
        }
    }
    if (cfg.verbose)
    {
        if (state[state_id].ignore_current_temp)
            printf("%c¡%d!" MON_SPACE, state[state_id].temp_type, state[state_id].temp);
        else
            printf("%c%d·%d" MON_SPACE, state[state_id].temp_type, state[state_id].temp, state[state_id].real_fan_state);

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
    check_failed += (cfg.monitor_fan_id != 0 && cfg.monitor_fan_id != 1) ? foolproof_error("monitor_fan_id = 0(left) or 1(right)") : false;
    check_failed += (cfg.jump_timeout < 100 || cfg.jump_timeout > 5000) ? foolproof_error("jump_timeout in [100,5000]") : false;
    check_failed += (cfg.jump_temp_delta < 2) ? foolproof_error("jump_temp_delta > 2") : false;

    check_failed += (cfg.discrete_gpu_mode != 0 && cfg.discrete_gpu_mode != 1 && cfg.discrete_gpu_mode != 2) ? foolproof_error("discrete_gpu_mode = 0 (cpu integrated) or 1 (max(cpu_temp,gpu_temp)) or 2 (separeted fan control)") : false;
    check_failed += (cfg.cpu_fan_id != 0 && cfg.cpu_fan_id != 1 && cfg.cpu_fan_id != 9) ? foolproof_error("cpu_fan_id = 0(left) or 1(right) or 9(autodetect)") : false;
    check_failed += (cfg.gpu_fan_id != 0 && cfg.gpu_fan_id != 1 && cfg.gpu_fan_id != 9) ? foolproof_error("gpu_fan_id = 0(left) or 1(right) or 9(autodetect)") : false;
    check_failed += !((cfg.gpu_temp_sensor_id >= 0 && cfg.gpu_temp_sensor_id <= 3) || cfg.gpu_temp_sensor_id == 9) ? foolproof_error("gpu_temp_sensor_id = 0...3 or 9(autodetect)") : false;
    if (check_failed)
    {
        printf("foolproof_checks() was failed.\nIf you are sure what you do, checks can be disabled using \"--foolproof_checks 0\" in command line or add \"foolproof_checks 0\" in %s\n", CFG_FILE);
        exit_failure();
    }
}

int foolproof_error(char *str)
{
    printf("foolproof_checks(): awaiting %s\n", str);
    return true;
}

void exit_failure()
{

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
    exit_failure();
}

void cfg_set(char *key, int value, int line_id)
{
    if (strcmp(key, "daemon") == 0)
        cfg.daemon = value;
    else if (strcmp(key, "period") == 0)
        cfg.period = value;
    else if (strcmp(key, "fan_check_period") == 0)
        cfg.fan_check_period = value;
    else if (strcmp(key, "monitor_fan_id") == 0)
        cfg.monitor_fan_id = value;
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
    else if (strcmp(key, "bios_disable_method") == 0)
        cfg.bios_disable_method = value;
    else if (strcmp(key, "mode") == 0)
        cfg.mode = value;
    else if (strcmp(key, "fan_ctrl_logic_mode") == 0)
        cfg.fan_ctrl_logic_mode = value;
    else if (strcmp(key, "discrete_gpu_mode") == 0)
        cfg.discrete_gpu_mode = value;
    else if (strcmp(key, "cpu_fan_id") == 0)
        cfg.cpu_fan_id = value;
    else if (strcmp(key, "gpu_fan_id") == 0)
        cfg.gpu_fan_id = value;
    else if (strcmp(key, "gpu_temp_sensor_id") == 0)
        cfg.gpu_temp_sensor_id = value;
    else
    {
        if (line_id > 0)
            printf(CFG_FILE " line %d: unknown parameter %s \n", line_id, key);
        else
            printf("Unknown argument --%s\n", key);

        exit_failure();
    }
}
void show_header()
{
    puts("dell-fan-mon v1.1 by https://github.com/ru-ace");
    puts("Fan monitor and control for Dell laptops via dell-smm-hwmon(i8k) kernel module or direct SMM BIOS calls.\n");
}
void usage()
{
    show_header();
    puts("Usage: dell-fan-mon [OPTIONS]");
    puts("  -h  Show this help");
    puts("  -v  Verbose mode");
    puts("  -d  Daemon mode (detach from console)");
    puts("  -m  No control - monitor only (useful to monitor daemon work)");
    puts("Options (see " CFG_FILE " or manpage for explains):");
    printf("  --mode MODE                       (default: %d = %s)\n", cfg.mode, cfg.mode ? "smm" : "i8k");
    printf("  --discrete_gpu_mode MODE          (default: %d = %s)\n", cfg.discrete_gpu_mode, (cfg.discrete_gpu_mode ? (cfg.discrete_gpu_mode == 1 ? "max(cpu_temp, gpu_temp)" : "separate fans control") : "cpu integrated"));
    printf("  --fan_ctrl_logic_mode MODE        (default: %d = %s)\n", cfg.fan_ctrl_logic_mode, cfg.fan_ctrl_logic_mode == 0 ? "default" : "simple");
    printf("  --bios_disable_method METHOD      (default: %d)\n", cfg.bios_disable_method);
    printf("  --period MILLISECONDS             (default: %ld ms)\n", cfg.period);
    printf("  --fan_check_period MILLISECONDS   (default: %ld ms)\n", cfg.fan_check_period);
    printf("  --monitor_fan_id FAN_ID           (default: %d = %s)\n", cfg.monitor_fan_id, cfg.monitor_fan_id == I8K_FAN_RIGHT ? "right" : "left");
    printf("  --jump_timeout MILLISECONDS       (default: %ld ms)\n", cfg.jump_timeout);
    printf("  --jump_temp_delta CELSIUS         (default: %d°)\n", cfg.jump_temp_delta);
    printf("  --t_low CELSIUS                   (default: %d°)\n", cfg.t_low);
    printf("  --t_mid CELSIUS                   (default: %d°)\n", cfg.t_mid);
    printf("  --t_high CELSIUS                  (default: %d°)\n", cfg.t_high);
    printf("  --t_low_fan FAN_STATE             (default: %d = %s)\n", cfg.t_low_fan, cfg.t_low_fan ? (cfg.t_low_fan == 1 ? "low" : "high") : "off");
    printf("  --t_mid_fan FAN_STATE             (default: %d = %s)\n", cfg.t_mid_fan, cfg.t_mid_fan ? (cfg.t_mid_fan == 1 ? "low" : "high") : "off");
    printf("  --t_high_fan FAN_STATE            (default: %d = %s)\n", cfg.t_high_fan, cfg.t_high_fan ? (cfg.t_high_fan == 1 ? "low" : "high") : "off");
    printf("  --gpu_temp_sensor_id SENSOR_ID    (default: %d = %s)\n", cfg.gpu_temp_sensor_id, (cfg.gpu_temp_sensor_id == 9 ? "autodetect" : "manual"));
    printf("  --cpu_fan_id FAN_ID               (default: %d = %s)\n", cfg.cpu_fan_id, (cfg.cpu_fan_id == I8K_FAN_RIGHT ? "right" : (cfg.cpu_fan_id == I8K_FAN_LEFT ? "left" : "autodetect")));
    printf("  --gpu_fan_id FAN_ID               (default: %d = %s)\n", cfg.gpu_fan_id, (cfg.gpu_fan_id == I8K_FAN_RIGHT ? "right" : (cfg.gpu_fan_id == I8K_FAN_LEFT ? "left" : "autodetect")));

    puts("");
}
void parse_args(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
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
        else if ((strcmp(argv[i], "-d") == 0) || (strcmp(argv[i], "--daemon") == 0))
        {
            cfg.daemon = true;
        }
        else if ((strcmp(argv[i], "-m") == 0) || (strcmp(argv[i], "--monitor_only") == 0))
        {
            cfg.monitor_only = true;
            cfg.verbose = true;
        }
        else if (argv[i][0] == '-' && argv[i][1] == '-')
        {
            char *key = argv[i];
            key += 2;
            i++;
            if (i == argc || !isdigit(argv[i][0]))
            {
                printf("Argument --%s need value\n", key);
                exit_failure();
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
            exit_failure();
        }
    }
}

void daemonize()
{
    int pid = fork();
    if (pid == -1) //failure
    {
        perror("can't fork()");
        exit_failure();
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
        set_fans_state(I8K_FAN_HIGH);
        if (cfg.bios_disable_method == 1 || cfg.bios_disable_method == 2 || cfg.bios_disable_method == 3)
            bios_fan_control(true);
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
        exit_failure();
    }
    if (signal(SIGTERM, signal_handler) == SIG_ERR)
    {
        perror("can't catch SIGTERM");
        exit_failure();
    }
}

void bios_fan_control_scanner()
{
    // Please DON'T USE THIS CODE - it may be DANGEROUS
    // Also: algorithm of c based on fact from my Dell 7559:
    //       if i set fan_state = LOW(1), bios set it to HIGH(2) within 10 seconds
    //       i don't sure that this fact is true for all dell notebooks.
    // No success on Dell 7559 :(
    exit_failure();
    for (int i = 0x5; i <= 0xff; i++)
    {
        int cmd = (i << 8) + 0xa3;
        printf("send_smm(%#06x) ", cmd);
        int res = -1; // send_smm(cmd, 0);
        printf("retured %#06x", res);
        if (res == -1)
        {
            printf(", bad :(\n");
            continue;
        }
        else
        {
            sleep(1);
            set_fans_state(I8K_FAN_LOW);
            printf(", sleep(10s)");
        }

        fflush(stdout);
        sleep(10);
        int real_fan_state = get_fan_state(I8K_FAN_LEFT);
        if (real_fan_state == 1)
        {
            printf(", SEEMS WE FOUND DISABLE BIOS FAN COTROL :)))\n");
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

    cfg_load();
    parse_args(argc, argv);

    if (cfg.mode)
        init_smm();
    else
        i8k_open();

    if (cfg.verbose)
        show_header();

    signal_handler_init();

    if (cfg.foolproof_checks)
        foolproof_checks();

    if (cfg.discrete_gpu_mode > 0)
    {
        if (cfg.mode != 1)
        {
            printf("discrete_gpu_mode = %d required mode = 1 (direct SMM BIOS calls)", cfg.discrete_gpu_mode);
            exit(EXIT_FAILURE);
        }
        if (cfg.gpu_temp_sensor_id == 9)
        {
            cfg.gpu_temp_sensor_id = get_gpu_temp_sensor_id();
        }
        if (cfg.discrete_gpu_mode == 2)
        {
            if (cfg.cpu_fan_id == 9)
                cfg.cpu_fan_id = get_cpu_fan_id();
            if (cfg.gpu_fan_id == 9)
                cfg.gpu_fan_id = 1 - cfg.cpu_fan_id;
        }
    }

    if (cfg.bios_disable_method && !cfg.monitor_only)
        bios_fan_control(false);

    if (cfg.daemon && !cfg.monitor_only)
        daemonize();

    monitor();
}
