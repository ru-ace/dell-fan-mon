# Changelog 
This file contains changelog of [Dell Fan Monitor](https://github.com/ru-ace/dell-fan-mon/)

<!--## [Unreleased]
### Added
### Changed
### Fixed
### Removed
-->
## [Version 1.2.1] 2019-10-11
### Fixed
- Unexpected exit with `smm_send error` during operation.

## [Version 1.2] 2019-09-25
### Added
- Autodetection of fans number 
- Support devices with 1 and 3 fans (before worked only on devices with 2 fans)
- **--fans_count** for manual set number of fans
### Changed
- Autodetection of fans types now work properly (with any amount of fans in device) 
- CPU/GPU termal sensor now autodetected properly (ignoring bad values)
- Fans controls: now each of fans controlled separately in all modes (before was only in **discrete_gpu_mode 2**)
- Output in verbose mode: Shows result of autodetection fans and sensors + add fan_id to monitoring. 
### Fixed
- Checks fans state on start when **bios_disable_method** enabled.    
### Removed
- **--monitor_fan_id** - ignored for backward compatibility (output warning)
- **--cpu_fan_id** - ignored for backward compatibility (output warning)

## [Version 1.1] 2019-02-02
### Added
- **--discrete_gpu_mode** supports laptops with discrete gpu (read docs for more information)
- **get_gpu_temp_cmd** config option for laptops with discrete gpu. Useful when gpu_temp_sensor_id autodetect failed.
- **--test**/**-t** command line option. Useful for scripts. If init code works without errors - exits with code 0, 
  otherwise - outputs error in stdout and exits with code 1.  
- install/uninstall section in Makefile 
- instruction for manual installation 
### Changed
- Set direct SMM BIOS calls (**mode 1**) by default (main cause: default **discrete_gpu_mode 1** with autodetect needs **mode 1**)
- Doesn't checks and corrects fan state if **bios_disable_method** used. Considered that **bios_disable_method** works and fan state setted only by dell-fan-mon. 

## [Version 1.0] 2019-01-26
### Changed
- Renamed from i8kmon-ng to dell-fan-mon<br>
  Initially i8kmon-ng created to be part of [i8kutils](https://github.com/vitorafsr/i8kutils), but [pull request](https://github.com/vitorafsr/i8kutils/pull/18) was ignored and i wasn't able to contact with maintainer. So I decide to separate from i8kutils.
### Removed
- Others source files from [i8kutils](https://github.com/vitorafsr/i8kutils)  

[Unreleased]: https://github.com/ru-ace/dell-fan-mon/compare/v1.2.1...HEAD
[Version 1.2.1]: https://github.com/ru-ace/dell-fan-mon/compare/v1.2...v1.2.1
[Version 1.2]: https://github.com/ru-ace/dell-fan-mon/compare/v1.1...v1.2
[Version 1.1]: https://github.com/ru-ace/dell-fan-mon/compare/v1.0...v1.1
[Version 1.0]: https://github.com/ru-ace/dell-fan-mon/compare/v0.0...v1.0
