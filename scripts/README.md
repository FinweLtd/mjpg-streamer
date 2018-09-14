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

Or, if you prefer to use the older ifconfig tool:
```
sudo apt install net-tools
ifconfig
```

If needed, also change the keyboard layout:
```
sudo dpkg-reconfigure keyboard-configuration
```

Cloning mjpg-streamer source code
---------------------------------

Before retrieving the source code, first install a client for Git version control:
```
sudo apt install git
```

Then create a directory for the source code:
```
cd ~
mkdir Source
cd Source
mkdir GitHub
cd GitHub
```

Now, clone the source code from the repository to this directory:
```
git clone https://github.com/FinweLtd/mjpg-streamer.git
```

Later, you can get updates by writing (in the same directory):
```
git pull
```

Installing Basler Pylon SDK
---------------------------

In order to use Basler cameras with mjpg-streamer, we need to install Basler's Pylon software suite. It is available for many platforms free of charge, including Linux on x64 and ARM architectures:
https://www.baslerweb.com/en/products/software/pylon-linux-arm/

You need to select a suitable package for your target hardware. For example, if you are using Odroid XU4, select "pylon 5.1.0 Camera Software Suite Linux ARM 32 bit hardfloat - Debian Installer Package".

<i>Since Basler requires that all downloaders provide their name & email address and accept their terms, we don't provide a direct download link here. Once you have given the requested information, you can see the download link (right click it and select Copy link address to get the full URL into Notepad or similar text editor). Either download the file with another PC and copy it to the target machine via a USB stick, or use wget as shown below.</i>

To download the package directly to your target hardware:
```
cd ~
mkdir Downloads
cd Downloads
wget [URL TO THE FILE TO DOWNLOAD]
```

Install the package:
(Note: here we assume that it is pylon_5.1.0.12682-deb0_armhf.deb in ~/Downloads directory)
```
sudo apt install ./pylon_5.1.0.12682-deb0_armhf.deb
```

Pylon software suite is now installed under /opt/pylon5 directory. Let's make a quick test:

1. Connect a Basler Pylon compatible camera to your target hardware, for example a Basler Pulse (we have been using model puA2500-14uc). Make sure to use a USB 3.0 port, not USB 2.0. Typically the faster 3.0 ports are marked with blue color.

2. Check USB connection:
```
lsusb
```
You should see a list of devices, the one that says "Basler AG" is your camera.

3. Compile a sample application:
```
cd /opt/pylon5/Samples/C/GenApiParam
make
ls
```
You should see a compiled program "GanApiParam", in addition to "GenApiParam.c" and "GenApiParam.o".

4. Run the sample:
```
./GenApiParam
```
This should connect to your camera and print a long list of details about the current configuration of the camera.

Compiling & running mjpg-streamer
---------------------------------

Change to mjpg-streamer root directory:
```
cd ~/Source/GitHub/mjpg-streamer/mjpg-streamer-experimental
```

Install the necessary packages:
```
sudo apt install cmake libjpeg8-dev
```

Tell where Pylon software suite is located by modifying enviroment variables:
```
source /opt/pylon5/bin/pylon-setup-env.sh /opt/pylon5
```

Build mjpg-streamer and plugins:
```
make
```
(next time: run "make distclean" before "make" to clean-up first)

If the program compiles without errors, you can try to run it. Buf first, check your IP address:
```
ip address
```

Now we are ready to run mjpg-streamer:
```
./mjpg_streamer -i "input_pylon.so" -o "output_http.so -w ./www"
```

If all goes well, mjpg-streamer starts, finds your Basler camera, connects to it, and begins streaming raw video from the camera. It also uses JPG library for encoding the raw video frames to JPG images, and HTTP output plugin for providing them to clients as a MJPG stream.

To view the stream, open a web browser in another computer and go to http://[your target machine's IP address]:8080/?action=stream

To see the usual mjpg-streamer's home page, go to http://[your target machine's IP address]:8080 

Making your setup more robust
-----------------------------

There are a set of useful scripts in the same repository. Copy them to the correct place:
```
cd ~/Source/GitHub/mjpg-streamer/scripts
cp *.sh ~
cd ~
```

Check the install path:
```
pwd
```

Then edit the start-up script:
```
nano start_stream.sh
```
In variable MJPG_STREAMER_PATH, replace "/home/odroid/" with what you got from pwd command, for example "/root".

Now you can start the stream as follows:
```
./start_stream.sh
```
(if the output from the streamer is filling the console, press CTRL+F2 to open another console)

To stop the stream, there is a separate script. First, install 'killall':
```
sudo apt install psmisc
```

Then stop the stream:
```
./stop_stream.sh
```

To use a watchdog feature that automatically restarts streaming:
```
./start_stream.sh --watchdog
```

You can now stop the stream and see that in 5 seconds it automatically starts again! You can even unplug the camera, wait a few seconds, and plug it in again - and the stream will restart automatically. Awesome!

To stop the watchdog and the stream:
```
killall start_stream.sh
./stop_stream.sh
```

Restarting the stream with Odroid's power key
---------------------------------------------

By default, you can press the power key to gracefully shutdown the system. Headless setup will shut it down right away, GUI will ask what to do and wait 60 seconds before going down. But you can also repurpose the button for restarting the stream instead.

First thing is to disable power key from the OS:
- In GUI installation, navigate to Ubuntu desktop's Power management settings and set power key handling to "Do nothing".
- In headless installation:
```
sudo nano /etc/systemd/logind.conf
```
Look for line #HandlePowerKey=[value], remove the comment char # and set "ignore" as value. The line should be:
<i>HandlePowerKey=ingore</i>

Reboot computer and check that nothing happens when you press the power key.

Next, install the necessary packages:
```
sudo apt install evtest expect
```

Edit the target action script path:
```
cd ~
pwd
nano watchdog_powerkey.sh
```
In variable POWERKEY_CMD, replace path "/home/odroid" with output of pwd command, e.g. "/root".

The same is required for the keyd handler script:
```
nano handle_powerkey.sh
```
In line starting with "bash -c", replace path "/home/odroid" with output of pwd command, e.g. "/root".

Then run this script:
```
./watchdog_powerkey.sh
```
... and press the power key. Now it should be captured, and the stream should be stopped (if it was running). And, if the stream was started with the --watchdog switch, it will automatically start after 5 seconds.

Now you can simply press the power button to restart the stream.

Now that power button is repurposed, how to gracefully shutdown the system? That is the downside... you need to either pull the power plug out (not recommended) or SSH to Odroid and type this:
```
sudo shutdown now  
```

Making the scripts survive over reboot
--------------------------------------

Open your crontab for editing:
```
crontab -e
```

Navigate to the end of the file and add these lines:
```
@reboot /home/odroid/watchdog_powerkey.sh
@reboot /home/odroid/start_stream.sh --watchdog
```
Note: replace /home/odroid with output from 'pwd' command!

The first line ensures that power key is captured (optional, only add if you use this feature). The second line auto-starts streaming on boot, meaning that you can simply power-on Odroid and it will start streaming in a moment.

References
----------

[1] Raspberry Pi
https://www.raspberrypi.org/

[2] Odroid XU4
https://www.hardkernel.com/main/products/prdt_info.php?g_code=G143452239825

[3] Using Single Board Computers (SBCs) with Basler USB3 Vision and GigE Vision Cameras
https://www.baslerweb.com/fp-1507807893/media/downloads/documents/application_notes/AW00145002000_AppNote_Using_Single_Board_Computers_with_Basler_USB_and_GigE_Cameras.pdf


