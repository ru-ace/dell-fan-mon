# Dell Fan Monitor - temperature monitor for some Dell Laptops

[![version][version-badge]][CHANGELOG] [![license][license-badge]][COPYING]

### DISCLAIMER

This program calls undocumented functions of Dell SMM BIOS  by defaults. **USE ON YOUR OWN RISK**   
### SYNOPSIS

dell-fan-mon \[*OPTION*\]...

### DESCRIPTION

The dell-fan-mon is a daemon that monitors cpu / gpu temperatures, and controls the fan speed on some Dell laptops.
It does that by means of regularly reading the system temperature, and
choosing appropriate speed of the fans.

All options can be customized at **/etc/dell-fan-mon.conf**, or via
command-line options


### ACCESS MODE

dell-fan-mon can access for temp and fan control with two modes (**--mode** \<*mode*\>):

  - *mode* 0: use dell-smm-hwmon(i8k) kernel module. This mode is more
    safety and can be used by unprivileged user.

  - *mode* 1: use direct SMM BIOS calls. To use
    this mode, dell-fan-mon needs root privileges. (default) 

### DISABLE BIOS FAN CONTROL

dell-fan-mon can try disable BIOS fan control with two
methods(**--bios\_disable\_method** \<*method*\>):

  - *method* 1: using code from *https://github.com/clopez/dellfan*

  - *method* 2: identical with
    *https://github.com/TomFreudenberg/dell-bios-fan-control*

To use this feature, dell-fan-mon needs root privileges.

Before set this option in config file, stop dell-fan-mon (`sudo service
dell-fan-mon stop`) and try every method in verbose mode (`sudo dell-fan-mon -v
--bios_disable_method 2 --fan_ctrl_logic_mode 0`). Wait until cpu
temp will be greater **t\_mid** and fans was on. If during cpu temp
lowering BIOS doesn't try change fan state: seems this method works.

If you cant disable bios fan control, please pay attention to **fan_ctrl_logic_mode 1**

On exit (SIGTERM, SIGINT) dell-fan-mon set fans speed to max and try to
restore BIOS fan control with corresponding *method*

Use *method* 0 for disable this feature. In *method* 0 dell-fan-mon checks and controls fan state.<br>
See **fan_check_period** and **monitor_fan_id** options.

### LAPTOPS WITH DISCRETE GPU 

dell-fan-mon supports monitoring discrete GPU temp (**--discrete_gpu_mode** \<*mode*\>) with 3 modes:

  - *mode* 0: monitor cpu_temp only (for integrated GPU) 

  - *mode* 1: use temp = max(cpu_temp, gpu_temp) in fan control logic (default)

  - *mode* 2: use separate fans control (for laptops with dedicated fan for discrete gpu)

dell-fan-mon supports two methods for obtaining GPU temp
  - Using direct SMM BIOS calls(**--mode 1**) dell-fan-mon try to autodetect GPU temp sensor. If autodetection failed **discrete_gpu_mode** automatically switch to **0** (monitor cpu_temp only). If it happened for you: please use second method.
  - Using third party console utility. Set **get_gpu_temp_cmd** options in config file. In this case you need provide command which output one line with one number (GPU temp) 

*mode* 2 use **cpu_fan_id** and **gpu_fan_id** options, that also attempts to automatically detect using the SMM BIOS (**--mode 1**). You can switch back to  **--mode 0** if you setting **get_gpu_temp_cmd**, **cpu_fan_id** amd **gpu_fan_id** manualy (you can see **cpu_fan_id** and **gpu_fan_id** values in verbose mode after successed autodetection) 

If you have discrete GPU it's good idea to run dell-fan-mon in verbose mode to be sure that autodetection works fine.

### FAN CONTROL LOGIC

dell-fan-mon has two modes of fans control
logic(**--fan\_ctrl\_logic\_mode** \<*mode*\>) regarding 3 temperature
thresholds **t\_low**, **t\_mid**, **t\_high** and corresponding fan
states **t\_low\_fan**, **t\_mid\_fan**, **t\_high\_fan**:

  - *mode* 0: Default logic (with default t\_\*\_fan)
    
      - When temperature rising:<br> 
        if temp in \[t\_low,t\_mid\] and fan\_state = OFF: fan\_state = OFF <br>
        if temp \> t\_mid: fan\_state = LOW<br>
        if temp \> t\_high: fan\_state = HIGH<br>
    
      - When temperature lowering: <br>
        if temp \< t\_low: fan\_state = OFF <br>
        if temp in \[t\_low,t\_mid\] and fan\_state = LOW: fan\_state = LOW <br>
        if temp in \[t\_low, t\_high\] and fan\_state = HIGH: fan\_state = HIGH<br>
    
    <!-- end list -->
    
      - Real code:<br> 
        if (temp \<= cfg.t\_low) fan\_state = cfg.t\_low\_fan; <br>
        else if (temp \> cfg.t\_high) fan\_state = cfg.t\_high\_fan;<br>
        else if (temp \>= cfg.t\_mid) fan\_state = fan\_state \> cfg.t\_mid\_fan ? fan\_state : cfg.t\_mid\_fan;

<!-- end list -->

  - *mode* 1: Simple logic which allow BIOS to control fans. Stop/start
    fans оnly at boundary temps<br> 
    if temp \< t\_low: fan\_state = t\_low\_fan <br>
    if temp \> t\_high: fan\_state = t\_high\_fan

### ABNORMAL TEMP JUMP DETECTION

dell-fan-mon has buildin mechanism for detect abnormal cpu temp jumps:
short(1-2 seconds) cpu temp jumps on +20/+30 celsius which are cause of
starting fans. You can control it via **--jump\_temp\_delta** and
**--jump\_timeout** options.

To disable this feature set **jump\_temp\_delta** to 100.

### OPTIONS

dell-fan-mon accepts the following command-line options

  - **-h**, **--help**  
    Show help and current values of all options. Before showing help
    programm load /etc/dell-fan-mon.conf. So it can be used for config file
    validation.

  - **-v**, **--verbose**  
    Report hardware status and program actions on stdout.

  - **-m**, **--monitor\_only**  
    Report hardware status on stdout in get\_only mode. Enable verbose
    output.

  - **-d**, **--daemon**  
    Reports child PID in stdout and detach from console. Please don't
    use this option, until you know what you do: current version of the
    startup scripts works only with default value. Default is 0
    (foreground mode).

  - **-t**, **--test**  
  Useful for scripts. If init code works without errors - exits with code 0, 
  otherwise - outputs error in stdout and exits with code 1.  
  
  - **--mode** \<*mode*\>  
    Set mode for accessing temp and fan controls. Default is 1.<br>
    *mode* 0: use dell-smm-hwmon(i8k) kernel module<br>
    *mode* 1: use direct SMM BIOS calls (default)<br>

  - **--discrete_gpu_mode** \<*mode*\>  
    Monitoring mode of discrete GPU temp. Default is 1.<br> 
    *mode* 0: monitor cpu_temp only(integrated GPU)<br>
    *mode* 1: use temp = max(cpu_temp, gpu_temp) in fan control logic (default)<br>
    *mode* 2: use separate fans control (for laptops with dedicated fan for discrete gpu)<br>


  - **--fan\_ctrl\_logic\_mode** \<*mode*\>  
    Set fan control logic. Default is 0.  
    *mode* 0: default logic (see above)  
    *mode* 1: allow BIOS to control fans. Stop/start fans оnly at
    boundary temps(see above)

  - **--bios\_disable\_method** \<*method*\>  
    Set disable BIOS fans control method. Not
    always works. Default is 0.  
    *method* 0: don't disablе BIOS fans control  
    *method* 1: use DISABLE\_BIOS\_METHOD1  
    *method* 2: use DISABLE\_BIOS\_METHOD2

  - **--period** \<*milliseconds*\>  
    Specifies the interval at which the daemon checks the hardware
    status. Default is 1000 milliseconds.

  - **--fan\_check\_period** \<*milliseconds*\>  
    Specifies the interval at which the daemon checks the fans speed and
    set it. Used only when **bios_disable_method 0**. Default is 1000
    milliseconds.

  - **--monitor\_fan\_id** \<*fan_id*\>  
    Fan ID for monitoring: 0 = right, 1 = left. State of this fan will
    shows in verbose mode. Used only when **bios_disable_method 0**. Default is 1. 

  - **--jump\_timeout** \<*milliseconds*\>  
    Specifies the interval at which the daemon ignore cpu temperature,
    after an abnormal temperature jump detected. Default is 2000
    milliseconds.

  - **--jump\_temp\_delta** \<*celsius*\>  
    Temperature difference between checks, at which the new value is
    considered abnormal. Default is 5° celsius. 

  - **--t\_low** \<*celsius*\>  
    Temperature threshold "low" in celsius. Default is 45° celsius.

  - **--t\_mid** \<*celsius*\>  
    Temperature threshold "middle" in celsius. Default is 60° celsius.

  - **--t\_high** \<*celsius*\>  
    Temperature threshold "high" in celsius. Default is 80° celsius. 

  - **--t\_low\_fan** \<*fan\_state\_id*\>  
    Fan state corresponding to temperature threshold "low". Default is 0
    (OFF).

  - **--t\_mid\_fan** \<*fan\_state\_id*\>  
    Fan state corresponding to temperature threshold "middle". Default
    is 1 (LOW).

  - **--t\_high\_fan** \<*fan\_state\_id*\>  
    Fan state corresponding to temperature threshold "high". Default is
    2 (HIGH).

  - **--cpu_fan_id** \<*fan_id*\>
    CPU fan_id, sometimes determined automatically through SMM BIOS. Used only when **discrete_gpu_mode** is 2. 0 = right, 1 = left. Default is 9 (autodetect).  

  - **--gpu_fan_id** \<*fan_id*\>
  GPU fan_id, sometimes determined automatically through SMM BIOS. Used only when **discrete_gpu_mode** is 2. 0 = right, 1 = left. Default is 9 (autodetect).  

### CONFIGURATION

dell-fan-mon has builtin default values of all options. User can see
current values using **--help** option.

All options with double dash described before can be changed in
/etc/dell-fan-mon.conf using same name.

### FILES

*/etc/dell-fan-mon.conf*

### AUTHOR

ace (https://github.com/ru-ace)

### CREDITS

Code for access to temp and fan control using dell-smm-hwmon(i8k) kernel
module from *https://github.com/vitorafsr/i8kutils*  
Code for enable/disable BIOS fan control and direct SMM BIOS calls from
*https://github.com/clopez/dellfan*

### INSTALLATION
```bash
git clone --depth 1 https://github.com/ru-ace/dell-fan-mon.git
cd dell-fan-mon
make
sudo make test
# no output is good.

# check if disabling bios works for you:
sudo ./dell-fan-mon -v --bios_disable_method 1
sudo ./dell-fan-mon -v --bios_disable_method 2
# wait until cpu temp will be greater t_mid(60°) and fans was on. If during cpu temp lowering BIOS doesn't try change fan state: seems this method works for you.

sudo make install

# edit /etc/dell-fan-mon.conf :
# set bios_disable_method which got earlier
# if bios_disable_method does not work for you: i recommends use fan_ctrl_logic_mode 1
# after config changed, lets check that no errors output on
sudo dell-fan-mon --test
sudo service dell-fan-mon restart

# I highly recommend first running dell-fan-mon in verbose mode for a while to make sure that it suits you.
sudo service dell-fan-mon stop
sudo dell-fan-mon -v
```
[version-badge]: https://img.shields.io/badge/version-1.0-blue.svg
[CHANGELOG]: ./CHANGELOG.md
[license-badge]: https://img.shields.io/badge/license-GPL3-blue.svg
[COPYING]: ./COPYING
