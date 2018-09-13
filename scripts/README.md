Building an Odroid XU4 Based MJPG Server for Basler Cameras
===========================================================

Abstract
--------

Basler's machine vision cameras are very configurable both harware and software wise. Being targeted for machine vision applications, they output raw video stream. This means extremely high bandwidth requirement and in practice either USB3.0 or 1Gbps ethernet connection if you want to achieve high resolution video at a decent framerate.

If such bandwidth is not available, the video must be compressed. MJPEG (motion JPEG) is fairly light-weight option both from encoding and decoding point of views, although it is not as efficient as more modern H.264 or H.265, etc. Yet, with MJPG raw video stream can be compressed to 1-5% while still maintaining reasonable quality for many applications.

mjpg-streamer is a well-known open source solution for providing low-latency MJPG streams over network connection. Until now, there hasn't been an input plugin that supports Basler cameras. This repository contains a forked version of mjpg-streamer where such a plugin has been added using Basler's Pylon SDK.

To keep the cost down, one can use an affordable single-board computer (SBC) as a camera server (platform for running mjpg-streamer). For example, Raspberry Pi [1] is very popular device for this task. However, as of writing this, the newest model of Raspberry Pi 3+ still doesn't have USB3.0 ports nor a decent 1Gbps Ethernet port, and also the CPU is not up to the task of encoding high quality video to MJPG (you'd need to use Pi's GPU for this). HardKernel's Odroid XU4 [2] is another platform similar to Raspberry Pi, but comes with USB3.0, 1Gbps Ethernet, and much more powerful CPU. For more information about performance of Basler cameras when connected to various SBCs, see [3].

In this readme we'll provide instructions for setting up an Odroid XU4 based camera server, using mjpg-streamer from this repository and a Basler machine vision camera. The instructions should also work with other Debian-based Linux computers with minor modifications.

Installing the OS
-----------------

<i>Note: If you use other computer hardware in place of Odroid XU4, skip this phase and install some Debian based Linux distribution, such as Ubuntu.</i>

Odroid XU4 does not include a hard disk; the OS runs from a uSD card. Hence, installing an OS is as easy as burning a suitable OS image to a uSD card and plugging the card into the Odroid's uSD card slot.

Ubuntu Linux OS images for Odroid XU4 can be downloaded from here:
https://wiki.odroid.com/odroid-xu4/os_images/linux/ubuntu_4.14/ubuntu_4.14

In this case we will create a headless setup, so select "Ubuntu 18.04 (20180531) (MINIMAL, BARE OS)" and download file "ubuntu-18.04-4.14-minimal-odroid-xu4-20180531.img.xz". You can also choose an image that comes with a full GUI environment.

After downloading and extracting the .xz file with 7zip tool, you will get an .img file that is ready to be burned to an uSD card. We recommend to use 8GB or larger card.

<b>WARNING: Burning the image to the uSD card removes ALL existing data permanently!</b>

Detailed instructions for burning an OS image to an SD card:
https://wiki.odroid.com/troubleshooting/odroid_flashing_tools

Quick instructions for Windows:

1. First unzip the downloaded package with 7zip tool, and notice the path of the extracted .img file

2. Put the uSD card into the USB SD card reader/writer

3. If your uSD card contains a previous release or something else, you should first remove all existing partitions, and then format the card (for example to a FAT32 partition, just to make Windows recognize the card). You can use Window's own Disk Management tool for this.

4. Download and start Win32DiskImager application

5. Set "Image File" to point to the .img file you unzipped

6. Check that "Device" shows the uSD card's drive letter. <b>WARNING: Be very careful here - you don't want to burn the image to system disk!</b>

7. Click "Write" button to burn the image. This will take a while.

8. Verifying the image is recommended. Once finished, eject the uSD card.

9. Put the uSD card into the Odroid's SD card slot, and check that the tiny switch is set to "uSD", not "eMMC".

10. Connect Ethernet cable, USB keyboard, HDMI display, and finally 5V 4A (minimum) power source. Odroid will boot automatically.

<i>Note: if this is the first boot for your Odroid, it will be turned off automatically for system configuration. Press the Odroid's power button to restart the device. This time you should be greeted with a login screen.</i>

First boot
----------

From now on, we expect you to be logged in to your system either locally with keyboard and display, or remotely using SSH. If necessary, you can check the IP address where to SSH from your network router's DHCP device list (and perhaps assign a static IP to it in your DHCP settings).

<i>If you are using Odroid, the default username for headless setup is "root" ("odroid" in OS images with GUI). The default password in both is "odroid". We recommend that you change the password immediately using "passwd" command!</i>

First, update the system (this will take a while):
```
sudo apt update
sudo apt upgrade
sudo apt dist-upgrade
sudo reboot
```

You can check your IP address as follows:
```
ip address
```

Or, if you prefer to use the older ifconfig:
```
sudo apt install net-tools
ifconfig
```




References
----------

[1] Raspberry Pi
https://www.raspberrypi.org/

[2] Odroid XU4
https://www.hardkernel.com/main/products/prdt_info.php?g_code=G143452239825

[3] Using Single Board Computers (SBCs) with Basler USB3 Vision and GigE Vision Cameras
https://www.baslerweb.com/fp-1507807893/media/downloads/documents/application_notes/AW00145002000_AppNote_Using_Single_Board_Computers_with_Basler_USB_and_GigE_Cameras.pdf


