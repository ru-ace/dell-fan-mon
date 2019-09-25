
#ifndef _DELL_FAN_MON_H
#define _DELL_FAN_MON_H
//dell-smm-hwmon
#define I8K_SMM_SET_FAN 0x01a3
#define I8K_SMM_GET_FAN 0x00a3
#define I8K_SMM_GET_FAN_TYPE 0x03a3
#define I8K_SMM_GET_TEMP 0x10a3
#define I8K_SMM_GET_TEMP_TYPE 0x11a3
#define I8K_SMM_GET_DELL_SIG1 0xfea3
#define I8K_SMM_GET_DELL_SIG2 0xffa3

//common for i8kctl & delfan

void set_fan_status(int, int);
int get_fan_status(int);
int get_temp(int);

// dellfan start
#define DISABLE_BIOS_METHOD1 0x30a3
#define ENABLE_BIOS_METHOD1 0x31a3
#define DISABLE_BIOS_METHOD2 0x34a3
#define ENABLE_BIOS_METHOD2 0x35a3

#define DISABLE_BIOS_METHOD3 0x32a3
#define ENABLE_BIOS_METHOD3 0x33a3

struct smm_regs
{
    unsigned int eax;
    unsigned int ebx __attribute__((packed));
    unsigned int ecx __attribute__((packed));
    unsigned int edx __attribute__((packed));
    unsigned int esi __attribute__((packed));
    unsigned int edi __attribute__((packed));
};
void smm_init();
void smm_init_ioperm();
int smm_check_dell_signature(unsigned int);
int smm_asm_call(struct smm_regs *);
int smm_send(unsigned int, unsigned int);
void set_bios_fan_control(int);

// dellfan end

// i8kctl start
#define I8K_PROC "/proc/i8k"
//#define I8K_FAN_LEFT 1
//#define I8K_FAN_RIGHT 0

#define I8K_FAN_OFF 0
#define I8K_FAN_LOW 1
#define I8K_FAN_HIGH 2
#define I8K_FAN_MAX I8K_FAN_HIGH

#define I8K_GET_TEMP _IOR('i', 0x84, size_t)
#define I8K_GET_FAN _IOWR('i', 0x86, size_t)
#define I8K_SET_FAN _IOWR('i', 0x87, size_t)

void i8k_init();
int i8k_presence();

// i8kctl end

// dell-fan-mon

#define CFG_FILE "/etc/dell-fan-mon.conf"
#define MON_SPACE "  "

#define true 1
#define false 0

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define MAX_FANS 3
#define MAX_SENSORS 4

struct t_cfg
{
    // brief descriptions at declaration of "struct t_cfg cfg" in dell-fan-mon.c
    // more specific in dell-fan-mon.conf
    int verbose;
    unsigned long period;
    unsigned long fan_check_period;
    int fan_ctrl_logic_mode;
    unsigned long jump_timeout;
    int jump_temp_delta;
    int t_low;
    int t_mid;
    int t_high;
    int t_low_fan;
    int t_mid_fan;
    int t_high_fan;
    int foolproof_checks;
    int daemon;
    int bios_disable_method;
    int monitor_only;
    int tick;
    int mode;
    int discrete_gpu_mode;
    int cpu_temp_sensor_id;
    int gpu_fan_id;
    int gpu_temp_sensor_id;
    int fans_count;
    int skip_signature_check;
    char *get_gpu_temp_cmd;
    int test_mode;
};
//temp state
struct t_state
{
    int temp;
    char temp_type;
    int temp_prev;
    int ignore_current_temp;
    int jump_timeout_ticks;
};

//fan state
struct t_fan_state
{
    int state_id;
    int fan_state;
    int real_fan_state;
    int last_setted_fan_state;
};

int get_gpu_temp_via_cmd();

void monitor();
void monitor_init_state(int);
void monitor_get_temp_state(int);
void monitor_state(int);
void monitor_init_fans_state();
void monitor_set_fans_state();
void monitor_get_fans_state();
void monitor_show_legend();
void parse_args(int, char **);
void exit_failure();
void usage();
void show_header();

void signal_handler(int);
void signal_handler_init();

void cfg_load();
void cfg_set(char *, int, int);
void cfg_error(int);

int foolproof_error(char *);
void foolproof_checks();

#endif //_DELL_FAN_MON_H
