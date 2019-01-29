# Changelog 
This file contains changelog of [Dell Fan Monitor](https://github.com/ru-ace/dell-fan-mon/)
## [Unreleased]
### Added
- **--discrete_gpu_mode** supports laptops with discrete gpu (read docs for more information)
- **get_gpu_temp_cmd** config option for laptops with discrete gpu. Useful when gpu_temp_sensor_id autodetect failed.
- **--test**/**-t** command line option. Useful for scripts. If init code works without errors - exits with code 0, 
  otherwise - outputs error in stdout and exits with code 1.  
- install section in Makefile and readme. 
### Changed
- Set direct SMM BIOS calls (**mode 1**) by default (main cause: default **discrete_gpu_mode 1** with autodetect needs **mode 1**)
- Doesn't checks and corrects fan state if **bios_disable_method** used. Considered that **bios_disable_method** works and fan state setted only by dell-fan-mon. 
<!--## [Unreleased]

### Added
### Changed
### Fixed
### Removed
-->

## [1.0] 2019-01-26
### Changed
- Renamed from i8kmon-ng to dell-fan-mon<br>
  Initially i8kmon-ng created to be part of [i8kutils](https://github.com/vitorafsr/i8kutils), but [pull request](https://github.com/vitorafsr/i8kutils/pull/18) was ignored and i wasn't able to contact with maintainer. So I decide to separate from i8kutils.
### Removed
- Others source files from [i8kutils](https://github.com/vitorafsr/i8kutils)  

[Unreleased]: https://github.com/ru-ace/dell-fan-mon/compare/v1.0...HEAD
[1.0]: https://github.com/ru-ace/dell-fan-mon/compare/v0.0...v1.0
