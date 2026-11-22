# PS3NTP

This application is a network time synchronization tool for the PlayStation 3, enabling the system clock to be synchronized with NTP servers.

This tool runs as a background service, automatically correcting the system time when needed.

## Requirements

I am targeting Evilnat 4.93 CEX Cobra 8.5 as my development environment.
It is untested on any other firmware or version at this time.

## Installation

1) Download ```ps3ntp.sprx``` from the [releases](https://github.com/tbwcjw/ps3ntp/releases/latest) page.
2) Using FTP, copy the SPRX to ```/dev_hdd0/plugins/ps3ntp.sprx```
3) At this point, you will need to add ```/dev_hdd0/plugins/ps3ntp.sprx```
   to boot plugins. There are two ways to achieve this:

   Manual:
   - Navigate to ```/dev_hdd0/``` and open ```boot_plugins.txt```
   - Append ```/dev_hdd0/plugins/ps3ntp.sprx``` to the bottom of the file.
   - Restart your system to take effect.

   Using webMAN MOD:
   - Navigate to the ```PS3MAPI``` page.
   - Scroll to ```VSH Plugins```.
   - In the first empty slot, enter ```/dev_hdd0/plugins/ps3ntp.sprx```, and press ```Load```.
   - Press ```Save > boot_plugins.txt```.
   - No restart is required.

## Configuration

PS3NTP creates the directory ```/dev_hdd0/tmp/ps3ntp``` and ```ps3ntp.cfg``` within. 

ps3ntp.cfg example:

```
ntp_poll_interval_s=1020         	#time between polling ntp servers (seconds)
ntp_socket_timeout_s=30         	#socket timeout (seconds)
no_answer_interval_s=60        		#all ntp servers unreachable, retry after (seconds)
drift_tolerance_ns=100000000      	#allow x nanoseconds of clock drift
log_enable=1                     	#enable/disable logging (1=true, 0=false)*
log_persist=1                     	#log clears on each boot (1=true, 0=false)
notify_enable=1                     	#on screen notifications for sync events, and failures (1=true, 0=false)
```

*The log file can be found at ```/dev_hdd0/tmp/ps3ntp/ps3ntp.log```.

## Serverlist

The serverlist.txt file can be found at ```/dev_hdd0/tmp/ps3ntp/serverlist.txt```.
PS3NTP defaults to ```pool.ntp.org``` when this file is not found.

serverlist.txt example:

```
pool.ntp.org
time.nist.gov
time.cloudflare.com
time.aws.com
```

One server per line. 16 servers maximum. Single entry length should not exceed 128 characters.

## Common Errors

 - ```PS3NTP Configuration Error. Bad line #X, using defaults.```: A value inside the config file is invalid,
see the example configuration above and check validity.
 - ```Failed to load serverlist, using default NTP server.```: The serverlist wasn't found, is unreadable, or couldn't be created. You can manually create it using the above example.
 - ```NTP server(s) are unreachable. Retrying in X seconds.```: Are you connected to the internet?

## Building

You will need the official Playstation 3 SDK 475, with the Visual Studio (2010/2013) extensions

## Credits
 - [**webMAN MOD**](https://github.com/aldostools/webman-mod): for the export stubs.

## License
 - This software is licensed under the [MIT License](https://github.com/tbwcjw/ps3ntp/LICENSE.md)

