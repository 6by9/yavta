# yavta
Fork of yavta from git://git.ideasonboard.org/yavta.git, modified to support V4L2 to MMAL.

Sets up the pipe:
```
V4L2 -> isp -> video_splitter -> video_render
                              -> video_encode -> file
```
Example command line:
```
./yavta --capture=1000 -n 3 --encode-to=file.h264 -f UYVY -m -T /dev/video0
```
Captures 1000 frames, 3 V4L2 buffers, encoder to file.h264, sets V4L2 format to UYVY (optional), -m for MMAL,
-T to set dv-timings (required for TC358743 only).

Intended/tested on:
- TC358743 HDMI to CSI2 bridge (eg Auvidea B101 - https://auvidea.com/b101-hdmi-to-csi-2-bridge-15-pin-fpc/). Need to load an EDID first.
- Analog Devices ADV7282-M analogue video to CSI2 bridge (eval board hooked on to Pi camera board - http://www.analog.com/en/design-center/evaluation-hardware-and-software/evaluation-boards-kits/EVAL-ADV7282MEBZ.html#eb-overview).
- Omnivision OV5647 (Pi V1.3 camera module).

Kernel tree https://github.com/6by9/linux/tree/unicam_4_13/ should have all the required drivers, and has overlays for the above.
