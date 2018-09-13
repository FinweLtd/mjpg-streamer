Building an Odroid XU4 Based MJPG Server for Basler Cameras
===========================================================

Abstract
--------

Basler's machine vision cameras are very configurable both harware and software wise. Being targeted for machine vision applications, they output raw video stream. This means extremely high bandwidth requirement and in practice either USB3.0 or 1Gbps ethernet connection if you want to achieve high resolution video at a decent framerate.

If such bandwidth is not available, the video must be compressed. MJPEG (motion JPEG) is fairly light-weight option both from encoding and decoding point of views, although it is not as efficient as more modern H.264 or H.265, etc. Yet, with MJPG raw video stream can be compressed to 1-5% while still maintaining reasonable quality for many applications.

mjpg-streamer is a well-known open source solution for providing low-latency MJPG streams over network connection. Until now, there hasn't been an input plugin that supports Basler cameras. This repository contains a forked version of mjpg-streamer where such a plugin has been added.

To keep the cost down, one can use an affordable single-board computer (SBC) as a camera server (platform for running mjpg-streamer). For example, Raspberry Pi is very popular for this task. However, as of writing this, the newest model of Raspberry Pi 3+ still doesn't have USB3.0 ports nor a decent 1Gbps Ethernet port, and also the CPU is not up to the task of encoding high quality video to MJPG (you'd need to use Pi's GPU for this).

HardKernel's Odroid XU4 is another platform similar to Raspberry Pi, but comes with USB3.0, 1Gbps Ethernet, and much more powerful CPU. In this readme we'll provide instructions for setting up an Odroid XU4 based camera server, using mjpg-streamer from this repository, and a Basler machine vision camera.










