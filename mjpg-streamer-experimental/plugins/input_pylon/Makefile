###############################################################
#
# Purpose: Makefile for "M-JPEG Streamer"
# Author.: Tom Stoeveken (TST), Tapani Rantakokko
# Version: 0.4
# License: GPL
#
###############################################################

CC = gcc

# Installation directories for pylon
PYLON_ROOT ?= /opt/pylon5

OTHER_HEADERS = ../../mjpg_streamer.h ../../utils.h ../output.h ../input.h

CFLAGS += -O2 -DLINUX -D_GNU_SOURCE -Wall -shared -fPIC -I$(PYLON_ROOT)/include -Wl,--no-as-needed $(shell $(PYLON_ROOT)/bin/pylon-config --libs-rpath pylonc)
#CFLAGS += -g
#CFLAGS += -DDEBUG
LFLAGS += -lpthread -ldl -ljpeg -L/opt/pylon5/lib -lpylonc
#LFLAGS += -lpthread -ldl -ljpeg-turbo8-dev -L/opt/pylon5/lib -lpylonc

all: input_pylon.so

clean:
	rm -f *.a *.o core *~ *.so *.lo

# pictures will not get cleaned automatically, it may be possible
# that other users do not have the required tools for re-building installed
# required tools are:
#  * convert (ImageMagick)
#  * od, sed, tr
clean_pictures:
	rm -f testpylon.h
	rm -f pictures/160x120_1.jpg pictures/160x120_2.jpg
	rm -f pictures/320x240_1.jpg pictures/320x240_2.jpg
	rm -f pictures/640x480_1.jpg pictures/640x480_2.jpg

input_pylon.so: $(OTHER_HEADERS) input_pylon.c testpylon.h jpeg_utils.lo
	$(CC) $(CFLAGS) -o $@ input_pylon.c jpeg_utils.lo $(LFLAGS)

jpeg_utils.lo: jpeg_utils.c jpeg_utils.h
	$(CC) -c $(CFLAGS) -o $@ jpeg_utils.c

#NAME       := input_pylon
#$(NAME).so: $(NAME).o
#	$(LD) $(LDFLAGS) -o $@ $^ $(LDLIBS)
#$(NAME).o: $(OTHER_HEADERS) $(NAME).c testpylon.h
#	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

# converts multiple JPG files to a single C header file
testpictures.h: pictures/960x720_1.jpg pictures/640x480_1.jpg pictures/320x240_1.jpg pictures/160x120_1.jpg pictures/160x120_2.jpg pictures/320x240_2.jpg pictures/640x480_2.jpg pictures/960x720_2.jpg
	@echo "/* automatically generated content, do not edit manually!" > testpictures.h.tmp
	@echo " */ " >> testpictures.h.tmp
	@FILES="160x120_1 320x240_1 640x480_1 960x720_1 160x120_2 320x240_2 640x480_2 960x720_2" && \
	  for file in $$FILES; do \
	    echo "converting file: $$file.jpg"; \
	    echo "static const unsigned char PIC_$$file[] = { " >> testpictures.h.tmp; \
	    od -An -v -t x1 pictures/$$file.jpg | sed -e "s/\ \([0-9a-fA-F]*\)/\ 0x\1,/g" >> testpictures.h.tmp; \
	    echo "};" >> testpictures.h.tmp; \
	  done;
	@cat testpictures.h.tmp | tr '\n' 'N' | sed -e "s/,N};/};/g" | tr 'N' '\n' > testpylon.h
	@rm testpictures.h.tmp

pictures/160x120_1.jpg: pictures/960x720_1.jpg
	convert pictures/960x720_1.jpg -resize 160x120! pictures/160x120_1.jpg

pictures/160x120_2.jpg: pictures/960x720_2.jpg
	convert pictures/960x720_2.jpg -resize 160x120! pictures/160x120_2.jpg

pictures/320x240_1.jpg: pictures/960x720_1.jpg
	convert pictures/960x720_1.jpg -resize 320x240! pictures/320x240_1.jpg

pictures/320x240_2.jpg: pictures/960x720_2.jpg
	convert pictures/960x720_2.jpg -resize 320x240! pictures/320x240_2.jpg

pictures/640x480_1.jpg: pictures/960x720_1.jpg
	convert pictures/960x720_1.jpg -resize 640x480! pictures/640x480_1.jpg

pictures/640x480_2.jpg: pictures/960x720_2.jpg
	convert pictures/960x720_2.jpg -resize 640x480! pictures/640x480_2.jpg
