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

The same is required for the key handler script:
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

Tuning system performance for Basler cameras
--------------------------------------------

Basler has an excellent document that compares performance of different SBCs with their cameras [3]. The document also includes many tips for tuning the performance. Here's a few.

With USB cameras, disable autosuspend (so that system does not power down the camera):
```
cat /sys/module/usbcore/parameters/autosuspend
sudo echo -1 /sys/module/usbcore/parameters/autosuspend
cat /sys/module/usbcore/parameters/autosuspend
```
You should see that the value changes (e.g. from '2') to '-1'.

With USB cameras, enable support for large image transmission:
```
cat /sys/module/usbcore/parameters/usbfs_memory_mb'
sudo sh -c 'echo 1000 > /sys/module/usbcore/parameters/usbfs_memory_mb'
cat /sys/module/usbcore/parameters/usbfs_memory_mb'
```
You should see that value changes (e.g. from '16') to '1000'.

To make these configurations survive over reboots, add them to your /etc/rc.local script:
```
sudo nano /etc/rc.local
```
Then add the following lines before last line 'exit 0':
```
echo -1 /sys/module/usbcore/parameters/autosuspend
echo 1000 > /sys/module/usbcore/parameters/usbfs_memory_mb
```

Reboot and check with cat command that the changes survived the reboot.

Discussion on system performance
--------------------------------

Which OS image to use? We have found that GUI image is better for development since you can install an IDE tool for modifying the source code and browsing the internet, but Xorg consumes quite a lot of CPU even if the developer isn't doing anything with it. For example, with Odroid Xorg consumes in practice all the power of one CPU core. Hence, we use GUI image for development, but headless image for production use.

As mentioned in the beginning of this document, machine vision cameras provide raw video stream and hence require huge bandwidth when using higher resolutions/frame rates. One CPU core may not be enough for retrieving the images, encoding them to JPG, and serving the MJPG stream to clients. Hence, the input_pylon.so plugin for mjpg-streamer uses multiple threads:
- 1 thread for grabbing images from Basler camera
- 1-4 threads for encoding the images to JPG (default: 2 encoder threads)

If one encoder thread is enough, that is it the simplest solution and should be used. With multiple encoder threads it is possible that image encoding will finish in "wrong order", e.g. frame number 123 is encoded and copied to output before 122. This can be handled by postponing copying an image that was ready early, but it causes a performance penalty. Hence, this code is currently commented out and it is possible that occasionally a frame appears in wrong order.

Furthermore, we have noticed that pushing the system to its limits easily causes stability issues in the grabbing side: the system appears to work well, but suddenly encoding times increase, the grabbing buffers are not freed soon enough, and the grabber bails out because it cannot continue filling the buffers. There are more performance tuning tips in Basler's document [2], and probably room for improvement if needed. Some ideas:
- move from libjpeg8-dev to jpeg-turbo (unless the platform already uses turbo behind the scenes)
- consider if performance is better when using RGB or YUV output from the camera, input_pylon.so supports both and makes the conversion if necessary but JPEG library could also take YUV input without conversion to RGB first?
- consider implementing other optimizations recommended by Basler, such as running in triggered mode instead of free-running mode, enabling real-time priority, etc.

However, with the current settings we have managed to get our use case working and stable streaming on Odroid XU4 platform - tests done so far show 60+ hours of streaming without a single restart by the watchdog or users. Longer tests are yet to be made.

Networking
----------

By default, Odroid's Ubuntu Linux installation assumes that DHCP is used and therefore the device's IP address can change. This makes it difficult to connect to the device via SSH or accessing the MJPG stream. A simple solution is assign a fixed IP address to it from the DHCP server's settings, but this is not always possible. Another solution is to set a fixed IP address:

Ubuntu 18.04 release uses a new network configuration system - editing the traditional /etc/network/interfaces file has been replaced with netplan, which means that a) configuration file path is different b) there is no configuration file by default c) the syntax is different. Here is one way to configure a fixed IP address:

First, we will create a configuration file and open an editor:
```
sudo nano /etc/netplan/50-cloud-init.yaml
```

Example content:
```
network:
    version: 2
    renderer: networkd
    ethernets:
        eth0: # Odroid's physical ethernet port
            # DHCP assigned IP:
            dhcp4: yes
            # OR static IP:
#            dhcp4: no
#            addresses: [192.168.1.2/24]
#            gateway4: 192.168.1.1
#            nameservers:
#                addresses: [192.168.1.1, 8.8.8.8, 8.8.4.4]
        eth1: # Virtual ethernet when using USB tethering, or USB ethernet dongle
            optional: true # do not wait for this adapter in boot
            # DHCP assigned IP:
            dhcp4: yes
            # OR static IP:
#            dhcp4: no
#            addresses: [192.168.42.100/24]
#            gateway4: 192.168.42.1
#            nameservers:
#                addresses: [192.168.42.1, 8.8.8.8, 8.8.4.4]
```
Warning: do not mix spaces and tabs in whitespace!

Notice that the sample content contains example for both DHCP and static configuration; comment out the one that you don't need. There is also configuration for both internal Ethernet adapter (eth0) and virtual/external adapter (eth1). The latter could be for example an Ethernet dongle or a phone/tablet being used as a modem (USB Tethering mode).

After creating/editing the configuration file, you must apply the changes as follows:
```
sudo netplan --debug apply
```
The --debug switch is optional and prints extra info on screen.

Mobile streaming
----------------

MJPG stream requires more bandwidth than more efficient encoders such as H.264/H.265, but both Wifi and modern LTE networks do offer enough uplink bandwidth for streaming a fair quality MJPG stream. This means that a Wifi dongle or a smartphone / tablet with 4G/LTE network and SIM card can be used for providing network connection to Odroid, and hence a truly mobile MJPG streaming station is achievable. 

Using a Wifi dongle is trivial, hence we discuss here how a phone/tablet could be used. In general, there are two ways to connect the devices:

1) Via wired Ethernet

Connect a USB-OTG adapter and USB-Ethernet adapter in sequence to your Android phone/tablet, then add a standard Ethernet cable and connect it's other end to Odroid's built-in Ethernet socket.

This setup requires that the Android device has suitable Ethernet drivers built-in, else the USB Ethernet adapter cannot work. This configuration has been tested to work on many Android devices and Ethernet adapters, but not all! You just have to test.

To try it out, first disable Wifi and LTE, then make the wired connection, and be sure that all devices are indeed connected and powered on. Now, Android device's notification area should so that you have a wired Ethernet connection available, and you should find this also from the device's Settings (under Connections). There you can also set a fixed IP. If you can't find the connection, try a different phone/tablet or a different USB Ethernet adapter.

After testing that the wired connection works, you can enable Wifi/LTE. Just be aware that Android prefers to use wireless adapters for all new connections (and Wifi over LTE). For example, if you use a web browser for testing, you should first establish the connection to the MJPG stream from Odroid over wired connection, then enable Wifi/LTE and open another connection in a new tab to some other URL like youtube.com - this will be now routed through Wifi/LTE adapter. The existing (wired) connection to MJPG stream will not jump from wired to wireless, though. The order in which you do these steps is crucial.

Finally, to access the MJPG stream from Wifi/LTE network, you must use a port forwarding app that allows directing the traffic from your Android device's Wifi/LTE adapter to wired Ethernet adapter. We have not tested this yet.

2) Via USB cable

In the old times before setting up a Wifi hotspot became a ubiquitous feature in mobile devices, people used to connect their laptops to Internet by using their phone as a modem - via USB cable. This feature is called USB Tethering and it is still supported in Android.

Notice that in this configuration your phone/laptop acts as the USB peripheral and Odroid becomes the USB central, meaning that you can use a standard USB-to-microUSB or USB-to-USB-C cable (no need for OTG adapter - don't use it). Odroid will also beging charging your Android device, so be sure to use powerful enough power source for your Odroid (we use 5V/6A model).

After making the connection, on your Android device navigate to Settings -> Connections -> Mobile Hotspot and Tethering, and activate USB tethering (grayed out when cable is not connected). Notice that this has to be done every time you re-connect the devices!

Now, test the connection from your Odroid:
```
ping 8.8.8.8
```
If you are lucky, the connection works right away and Google's name server responds to ping. For example, Samsung Galaxy S7 Edge worked like this.

With another device you may be less lucky. For example, with Samsung Galaxy Tab S3 (LTE) we had to go through several steps to make the connection working:

Show network adapters:
```
ifconfig -a
```
You must use the -a switch to reveal also adapters that are not (yet) properly configured. When you connect the USB cable and enable USB Tethering as explained above, a new device will appear in the output (S7 Edge: usb0, Tab S3: eth1).

With Tab S3 using the -a switch was crucial and revealed that eth1 adapter was there but its MAC address was not set up (00:00:00:00:00:00) and hence no IP address. Here's how to fix that:

```
sudo apt install macchanger
macchanger -b -a eth1
ifconfig -a
```
eth1 MAC address is now reasonable, and eth1 should get an IP address as well (192.168.42.xxx). Now you should be able to access the MJPG stream from your Android device, using the IP address that was given to eth1 and listed in ifconfig command output.

Automate the MAC address fix as follows:
```
sudo nano /etc/udev/rules.d/70-usb-tethering.rules
```

Add this line (here we assume USB Tethering adapter appears as eth1):
```
ACTION=="add", SUBSYSTEM=="net", ENV{INTERFACE}=="eth1", RUN+="/usr/bin/macchanger -b -a eth1", OPTIONS="last_rule"
```

Now that we can view the MJPG stream via the phone/tablet browser, we must solve how to set a fixed IP to your Odroid - now the address will be different every time you plug in the USB cable and enable USB Tethering. If we enable static IP in netplan, then Odroid doesn't know what is tablet's IP (gateway, DNS, etc.). The solution is to use DHCP in netplan, but add another IP address to eth1 whenever it gets up. Here's how to do that:

Create a new script for networkd-dispatcher:
```
sudo nano /usr/lib/networkd-dispatcher/routable.d/usb-tethering.sh
```

Add these lines:
```
#!/bin/bash
ip addr add 192.168.42.100/24 dev eth1
```
Notice that we have selected IP address 192.168.42.100 as our fixed IP address for Odroid.

Add execution rights:
```
chmod +x /usr/lib/networkd-dispatcher/routable.d/usb-tethering.sh
```

To test it, disable USB Tethering from your Android device, wait a moment, and enable it again. Your eth1 adapter will now get two different IP addresses (and both will work, but only one of them will be fixed address). Notice that ifconfig command is not the right tool for checking the addresses, use ip command instead:
```
ip addr show
```

Now you can access your MJPG stream from Android phone/tablet when using USB Tethering by using 192.168.42.100 as address. To access the stream from Wifi/LTE network as well, continue with port forwarding as explained in the next chapter.


Port Forwarding
---------------




References
----------

[1] Raspberry Pi
https://www.raspberrypi.org/

[2] Odroid XU4
https://www.hardkernel.com/main/products/prdt_info.php?g_code=G143452239825

[3] Using Single Board Computers (SBCs) with Basler USB3 Vision and GigE Vision Cameras
https://www.baslerweb.com/fp-1507807893/media/downloads/documents/application_notes/AW00145002000_AppNote_Using_Single_Board_Computers_with_Basler_USB_and_GigE_Cameras.pdf


