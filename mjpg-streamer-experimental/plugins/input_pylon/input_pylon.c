/*******************************************************************************
# Basler Pylon input-plugin for MJPG-streamer                                  #
#                                                                              #
# This plugin works with Basler Pylon based machine vision cameras             #
# and allows encoding their raw image data to MJPG stream.                     #
#                                                                              #
# The plugin is based on                                                       #
# -mjpeg-streamer input_uvc plugin:                                            #
#   Originally Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard      #
#   Modifications Copyright (C) 2006  Gabriel A. Devenyi                       #
#   Modifications Copyright (C) 2007  Tom Stöveken                             #
# -Pylon software suite's sample "OverlappedGrab" (C) 2018 Basler              #
#                                                                              #
# Integration and modifications Copyright (C) 2018  Tapani Rantakokko / Finwe  #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getopt.h>
#include <pthread.h>
#include <syslog.h>
#include <malloc.h>
#include <time.h>
#include <jpeglib.h>

#include <pylonc/PylonC.h>

#include "../../mjpg_streamer.h"
#include "../../utils.h"
#include "jpeg_utils.h"

#define INPUT_PLUGIN_NAME "Pylon input plugin"

#define NUM_BUFFERS 7
//#define ALLOW_FRAME_SIZE_CHANGE_DURING_STREAMING
//#define ENSURE_CORRECT_FRAME_ORDER

void print_error_and_exit(GENAPIC_RESULT errc);
#define CHECK( errc ) if ( GENAPI_E_OK != errc ) print_error_and_exit( errc )

/* Private functions and variables to this plugin. */

/* Frame grabber thread. */
static pthread_t worker_grabber_th1;

/* Encoder threads 1-2. */
static pthread_t worker_encoder_th1;
static pthread_t worker_encoder_th2;
static pthread_t worker_encoder_th3;
static pthread_t worker_encoder_th4;

/* Mutexes and conditions. */
static pthread_mutex_t controls_mutex;
static pthread_mutex_t encode_queue_mutex;
static pthread_mutex_t release_buf_queue_mutex;
static pthread_cond_t encode_cond;

/* Handle for mjpeg-streamer globals. */
static globals *pglobal;

/* Plugin number. */
static int plugin_number;

/* Grabber thread function. */
void *worker_grabber(void *);

/* Grabber thread cleanup function. */
void worker_grabber_cleanup(void *);

/* Encoder thread function . */
void *worker_encoder(void *);

/* Encoder thread cleanup function. */
void worker_encoder_cleanup(void *);

/* Print help message to screen. */
void help(void);

/* Return value of pylon methods. */
GENAPIC_RESULT res;

/* Number of available pylon devices. */
size_t numDevices;

/* Handle for the pylon device. */
PYLON_DEVICE_HANDLE hDev;

/* Handle for the pylon stream grabber. */
PYLON_STREAMGRABBER_HANDLE hGrabber;

/* Handle used for waiting for a grab to be finished. */
PYLON_WAITOBJECT_HANDLE hWait;

/* Size of an image frame in bytes. */
int32_t payloadSize;

/* Buffers used for grabbing. */
unsigned char *buffers[NUM_BUFFERS];

/* Handles for the buffers. */
PYLON_STREAMBUFFER_HANDLE bufHandles[NUM_BUFFERS];

/* Stores the result of a grab operation. */
PylonGrabResult_t grabResult;

/* The number of streams the device provides. */
size_t nStreams;

/* Used for checking feature availability. */
_Bool isAvail;

/* Used as an output parameter. */
_Bool isReady;

/* Linked list node. */
typedef struct node {
    void *ptr;
    struct node *next;
} node_t;

/* Push node to the end of the list. */
void push(node_t *head, void* ptr) {
    node_t *current = head;
    while(current->next != NULL) {
        current = current->next;
    }
    current->next = malloc(sizeof(node_t));
    current->next->ptr = ptr;
    current->next->next = NULL;
}

/* Pop node from the head of the list. */
void* pop(node_t **head) {
    void* retval = NULL;
    node_t *next_node = NULL;
    if(*head == NULL) {
        return NULL;
    }
    next_node = (*head)->next;
    retval = (*head)->ptr;
    free(*head);
    *head = next_node;
    return retval;
}

/* Destroy the whole list. */
void destroy(node_t **head) {
    if (*head == NULL) {
        return;
    } else {
        destroy(&((*head)->next));
        free(*head);
    }
}

/* Debug print encode queue contents. */
void print_encode_queue(node_t *head) {
    node_t *current = head;

    while(current != NULL) {
        struct image_config* image = (struct image_config*) current->ptr;
        DBG("frame=%d, width=%d, height=%d, size=%d\n, buffer=%d, ptr=%p\n",
            image->number, image->width, image->height,
            image->size, image->buffer, (void*)&image->data);
        current = current->next;
    }
}

/* Queue of buffer indexes that have a frame waiting to be encoded. */
node_t *encodeQueue = NULL;

/* Queue of buffer indexes that are no longer used by encoder threads. */
node_t *releaseBufQueue = NULL;

/* Configuration struct for encoder thread. */
typedef struct encoder_config {
    int index;
    unsigned char *buffer;
} encoder_config_t;

/* Configuration objects for encoder threads. */
struct encoder_config enc1;
struct encoder_config enc2;
struct encoder_config enc3;
struct encoder_config enc4;

/* The number of encoder threads currently online. */
int encoders_online = 0;

/* Running number of last grabbed frame. */
unsigned int frameNumGrabbed = -1;

/* Running number of last sent frame (frame given to output plugins). */
unsigned int frameNumLastSent = -1;

/* The number of grab buffers currently in use. */
int used_buffers = 0;

static int delay = 1000;

/* Plugin interface functions */

/******************************************************************************
Description.: parse input parameters
Input Value.: param contains the command line string and a pointer to globals
Return Value: 0 if everything is ok
******************************************************************************/
int input_init(input_parameter *param, int plugin_no)
{
    int i;

    if(pthread_mutex_init(&controls_mutex, NULL) != 0) {
        IPRINT("could not initialize control mutex variable\n");
        exit(EXIT_FAILURE);
    }

    if(pthread_mutex_init(&encode_queue_mutex, NULL) != 0) {
        IPRINT("could not initialize compress mutex variable\n");
        exit(EXIT_FAILURE);
    }

    if(pthread_mutex_init(&release_buf_queue_mutex, NULL) != 0) {
        IPRINT("could not initialize release buf mutex variable\n");
        exit(EXIT_FAILURE);
    }

    if(pthread_cond_init(&encode_cond, NULL) != 0) {
        IPRINT("could not initialize compress condition variable\n");
        exit(EXIT_FAILURE);
    }

    param->argv[0] = INPUT_PLUGIN_NAME;

    /* show all parameters for DBG purposes */
    for(i = 0; i < param->argc; i++) {
        DBG("argv[%d]=%s\n", i, param->argv[i]);
    }

    reset_getopt();
    while(1) {
        int option_index = 0, c = 0;
        static struct option long_options[] = {
            {"h", no_argument, 0, 0},
            {"help", no_argument, 0, 0},
            {"d", required_argument, 0, 0},
            {"delay", required_argument, 0, 0},
            {"r", required_argument, 0, 0},
            {"resolution", required_argument, 0, 0},
            {0, 0, 0, 0}
        };

        c = getopt_long_only(param->argc, param->argv, "", long_options, &option_index);

        /* no more options to parse */
        if(c == -1) break;

        /* unrecognized option */
        if(c == '?') {
            help();
            return 1;
        }

        switch(option_index) {
            /* h, help */
        case 0:
        case 1:
            DBG("case 0,1\n");
            help();
            return 1;
            break;

            /* d, delay */
        case 2:
        case 3:
            DBG("case 2,3\n");
            delay = atoi(optarg);
            break;

            /* r, resolution */
        case 4:
        case 5:
            DBG("case 4,5\n");
            break;

        default:
            DBG("default case\n");
            help();
            return 1;
        }
    }

    pglobal = param->global;

    IPRINT("delay.............: %i\n", delay);
    //IPRINT("resolution........: %s\n", pics->resolution);

    return 0;
}

/******************************************************************************
Description.: stops the execution of the worker thread
Input Value.: -
Return Value: 0
******************************************************************************/
int input_stop(int id)
{
    DBG("Stop received, cancelling ALL threads\n");
    pthread_cancel(worker_encoder_th1);
    pthread_cancel(worker_encoder_th2);
//    pthread_cancel(worker_encoder_th3);
//    pthread_cancel(worker_encoder_th4);
    pthread_cancel(worker_grabber_th1);

    return 0;
}

/******************************************************************************
Description.: start the worker threads
Input Value.: plugin id
Return Value: 0
******************************************************************************/
int input_run(int id)
{
    DBG("Creating frame grabber thread 1\n");
    if (pthread_create(&worker_grabber_th1, 0, worker_grabber, NULL) != 0) {
        free(pglobal->in[id].buf);
        fprintf(stderr, "ERROR: Failed to start grabber thread 1!\n");
        exit(EXIT_FAILURE);
    }
    pthread_detach(worker_grabber_th1);

    DBG("Creating JPEG encoder thread 1\n");
    enc1.index = 1;
    enc1.buffer = NULL;
    if (pthread_create(&worker_encoder_th1, 0, worker_encoder, (void*)&enc1) != 0) {
        fprintf(stderr, "ERROR: Could not start encoder thread 1\n");
        exit(EXIT_FAILURE);
    }
    pthread_detach(worker_encoder_th1);
    encoders_online++;

    DBG("Creating JPEG encoder thread 2\n");
    enc2.index = 2;
    enc2.buffer = NULL;
    if (pthread_create(&worker_encoder_th2, 0, worker_encoder, (void*)&enc2) != 0) {
        fprintf(stderr, "ERROR: Could not start encoder thread 2\n");
        exit(EXIT_FAILURE);
    }
    pthread_detach(worker_encoder_th2);
    encoders_online++;

//    DBG("Creating JPEG encoder thread 3\n");
//    enc3.index = 3;
//    enc3.buffer = NULL;
//    if (pthread_create(&worker_encoder_th3, 0, worker_encoder, (void*)&enc3) != 0) {
//        fprintf(stderr, "ERROR: Could not start encoder thread 3\n");
//        exit(EXIT_FAILURE);
//    }
//    pthread_detach(worker_encoder_th3);
//    encoders_online++;
//
//    DBG("Creating JPEG encoder thread 4\n");
//    enc4.index = 4;
//    enc4.buffer = NULL;
//    if (pthread_create(&worker_encoder_th4, 0, worker_encoder, (void*)&enc4) != 0) {
//        fprintf(stderr, "ERROR: Could not start encoder thread 4\n");
//        exit(EXIT_FAILURE);
//    }
//    pthread_detach(worker_encoder_th4);
//    encoders_online++;

    return 0;
}

/******************************************************************************
Description.: print help message
Input Value.: -
Return Value: -
******************************************************************************/
void help(void)
{
    fprintf(stderr, " ---------------------------------------------------------------\n" \
    " Help for input plugin..: "INPUT_PLUGIN_NAME"\n" \
    " ---------------------------------------------------------------\n" \
    " The following parameters can be passed to this plugin:\n\n" \
    " [-d | --delay ]........: delay to pause between frames\n" \
    " [-r | --resolution]....: can be 960x720, 640x480, 320x240, 160x120\n"
    " ---------------------------------------------------------------\n");
}

/******************************************************************************
Description.: acquire a picture from camera and signal encoders
Input Value.: arg is not used
Return Value: NULL
******************************************************************************/
void* worker_grabber(void *arg)
{
    int i = 0;
    NODEMAP_HANDLE      hNodeMap;
    NODE_HANDLE         hNode;
    const char          * pFeatureName;
    _Bool                val, val_read, val_write;

    /* Set cleanup handler to cleanup allocated resources. */
    pthread_cleanup_push(worker_grabber_cleanup, NULL);

    /* Initialize pylon runtime. */
    PylonInitialize();

    /* Enumerate all camera devices. */
    res = PylonEnumerateDevices(&numDevices);
    CHECK(res);
    if (0 == numDevices) {
        fprintf(stderr, "No pylon devices found!\n");
        PylonTerminate();
        exit(EXIT_FAILURE);
    } else {
        IPRINT("cameras found.....: %i\n", numDevices);
    }

    /* Get a handle for the first device found. */
    res = PylonCreateDeviceByIndex(0, &hDev);
    CHECK(res);

    /* Open it for configuring parameters and for grabbing images. */
    res = PylonDeviceOpen(hDev,
        PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM);
    CHECK(res);

    /* Print out the name of the camera. */
    {
        char buf[256];
        size_t siz = sizeof(buf);
        _Bool isReadable;

        isReadable = PylonDeviceFeatureIsReadable(hDev, "DeviceModelName");
        if (isReadable) {
            res = PylonDeviceFeatureToString(hDev,
                "DeviceModelName", buf, &siz);
            CHECK(res);
            IPRINT("using camera......: %s\n", buf);
        }
    }

    /* set the pixel format to YCbCr422_8 if available */
    /*isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_PixelFormat_Mono8");*/
    /*isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_PixelFormat_YCbCr422_8");*/
    isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_PixelFormat_RGB8");
//    isAvail = PylonDeviceFeatureIsAvailable(hDev,
//        "EnumEntry_PixelFormat_YUV422_YUYV_Packed");
    if (isAvail) {
        /*res = PylonDeviceFeatureFromString(hDev, "PixelFormat", "Mono8");*/
        /*res = PylonDeviceFeatureFromString(hDev, "PixelFormat", "YCbCr422_8");*/
        res = PylonDeviceFeatureFromString(hDev, "PixelFormat", "RGB8");
//        res = PylonDeviceFeatureFromString(hDev, "PixelFormat", "YUV422_YUYV_Packed");
        /*IPRINT("using format......: Mono8\n");*/
        /*IPRINT("using format......: YCbCr422_8\n");*/
        IPRINT("using format......: RGB8\n");
//        IPRINT("using format......: YUV422_YUYV_Packed\n");
        CHECK(res);
    }

    /* Get a handle for the device's node map. */
    res = PylonDeviceGetNodeMap(hDev, &hNodeMap);
    CHECK(res);

//    /* Check to see if a feature is implemented at all. The 'Width' feature is likely to
//    be implemented by just about every existing camera. */
//    pFeatureName = "Width";
//    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
//    CHECK(res);
//    if(GENAPIC_INVALID_HANDLE != hNode)
//    {
//        /* Node exists, check whether feature is implemented. */
//        res = GenApiNodeIsImplemented(hNode, &val);
//        CHECK(res);
//    }
//    else
//    {
//        /* Node does not exist --> feature is not implemented. */
//        val = 0;
//    }
//    printf("The '%s' feature %s implemented\n", pFeatureName, val ? "is" : "is not");
//
//    /* Although a feature is implemented by the device, it may not be available
//       with the device in its current state. Check to see if the feature is currently
//       available. The GenApiNodeIsAvailable sets val to 0 if either the feature
//       is not implemented or if the feature is not currently available. */
//    if(GENAPIC_INVALID_HANDLE != hNode)
//    {
//        /* Node exists, check whether feature is available. */
//        res = GenApiNodeIsAvailable(hNode, &val);
//        CHECK(res);
//    }
//    else
//    {
//        /* Node does not exist --> feature is not implemented, and hence not available. */
//        val = 0;
//    }
//    printf("The '%s' feature %s available\n", pFeatureName, val ? "is" : "is not");
//
//    /* If a feature is available, it could be read-only, write-only, or both
//    readable and writable. Use the GenApiNodeIsReadable() and the
//    GenApiNodeIsReadable() functions(). It is safe to call these functions
//    for features that are currently not available or not implemented by the device.
//    A feature that is not available or not implemented is neither readable nor writable.
//    The readability and writability of a feature can change depending on the current
//    state of the device. For example, the Width parameter might not be writable when
//    the camera is acquiring images. */
//    if(GENAPIC_INVALID_HANDLE != hNode)
//    {
//        /* Node exists, check whether feature is readable. */
//        res = GenApiNodeIsReadable(hNode, &val_read);
//        CHECK(res);
//        res = GenApiNodeIsReadable(hNode, &val_write);
//        CHECK(res);
//    }
//    else
//    {
//        /* Node does not exist --> feature is neither readable nor witable. */
//        val_read = val_write = 0;
//    }
//    printf("The '%s' feature %s readable\n", pFeatureName, val_read ? "is" : "is not");
//    printf("The '%s' feature %s writable\n", pFeatureName, val_write ? "is" : "is not");
//
    /* Set image width. */
    pFeatureName = "Width";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if (GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 2048);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 2048);
    }
    else
    {
        val = 0;
        printf("The '%s' feature is not implemented\n", pFeatureName);
    }

    /* Set image height. */
    pFeatureName = "Height";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if (GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 1536);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 1536);
    }
    else
    {
        val = 0;
        printf("The '%s' feature is not implemented\n", pFeatureName);
    }

    /* Set offset x. */
    pFeatureName = "OffsetX";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if (GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 272);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 272);
    }
    else
    {
        val = 0;
        printf("The '%s' feature is not implemented\n", pFeatureName);
    }

    /* Set offset y. */
    pFeatureName = "OffsetY";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if (GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 204);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 204);
    }
    else
    {
        val = 0;
        printf("The '%s' feature is not implemented\n", pFeatureName);
    }

    /* Set acquisition frame rate. */
    pFeatureName = "AcquisitionFrameRate";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if (GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiFloatSetValue(hNode, 7.5f);
        CHECK(res);
        printf("The '%s' is now %f\n", pFeatureName, 7.5f);
    }
    else
    {
        val = 0;
        printf("The '%s' feature is not implemented\n", pFeatureName);
    }

    /* Disable acquisition start trigger if available. */
    isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_TriggerSelector_AcquisitionStart");
    if (isAvail) {
        res = PylonDeviceFeatureFromString(hDev, "TriggerSelector",
            "AcquisitionStart");
        CHECK(res);
        res = PylonDeviceFeatureFromString(hDev, "TriggerMode", "Off");
        CHECK(res);
    }

    /* Disable frame burst start trigger if available. */
    isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_TriggerSelector_FrameBurstStart");
    if (isAvail) {
        res = PylonDeviceFeatureFromString(hDev, "TriggerSelector",
            "FrameBurstStart");
        CHECK(res);
        res = PylonDeviceFeatureFromString(hDev, "TriggerMode", "Off");
        CHECK(res);
    }

    /* Disable frame start trigger if available. */
    isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_TriggerSelector_FrameStart");
    if (isAvail) {
        res = PylonDeviceFeatureFromString(hDev, "TriggerSelector",
            "FrameStart");
        CHECK(res);
        res = PylonDeviceFeatureFromString(hDev, "TriggerMode", "Off");
        CHECK(res);
    }

    /* Use the Continuous frame acquisition mode, i.e., the camera delivers
       images continuously. */
    res = PylonDeviceFeatureFromString(hDev, "AcquisitionMode", "Continuous");
    CHECK(res);

    /* For GigE cameras, increase the packet size for better performance:
       when the network adapter supports jumbo frames, set the packet
       size to a value > 1500, e.g., to 8192 */
    isAvail = PylonDeviceFeatureIsWritable(hDev, "GevSCPSPacketSize");
    if (isAvail) {
        res = PylonDeviceSetIntegerFeature(hDev, "GevSCPSPacketSize", 1500);
        CHECK(res);
    }

    /* Number of streams supported by the device and the transport layer. */
    res = PylonDeviceGetNumStreamGrabberChannels(hDev, &nStreams);
    CHECK(res);
    if (nStreams < 1) {
        fprintf(stderr, "The transport layer doesn't support image streams\n");
        PylonTerminate();
        exit(EXIT_FAILURE);
    }

    /* Create and open a stream grabber for the first channel. */
    res = PylonDeviceGetStreamGrabber(hDev, 0, &hGrabber);
    CHECK(res);
    res = PylonStreamGrabberOpen(hGrabber);
    CHECK(res);

    /* Get a handle for the stream grabber's wait object. */
    res = PylonStreamGrabberGetWaitObject(hGrabber, &hWait);
    CHECK(res);

    /* Determine the required size of the grab buffer. */
    if(PylonDeviceFeatureIsReadable(hDev, "PayloadSize")) {
        res = PylonDeviceGetIntegerFeatureInt32(hDev, "PayloadSize",
            &payloadSize);
        CHECK(res);
    } else {
        NODEMAP_HANDLE hStreamNodeMap;
        NODE_HANDLE hNode;
        int64_t i64payloadSize;

        res = PylonStreamGrabberGetNodeMap(hGrabber, &hStreamNodeMap);
        CHECK(res);
        res = GenApiNodeMapGetNode(hStreamNodeMap, "PayloadSize", &hNode);
        CHECK(res);
        if (GENAPIC_INVALID_HANDLE == hNode) {
            fprintf(stderr, "There is no PayloadSize parameter.\n");
            PylonTerminate();
            exit(EXIT_FAILURE);
        }
        res = GenApiIntegerGetValue(hNode, &i64payloadSize);
        CHECK(res);
        payloadSize = (int32_t) i64payloadSize;
    }

    /* Allocate memory for grabbing buffers. */
    for (i = 0; i < NUM_BUFFERS; ++i) {
        buffers[i] = (unsigned char*) malloc(payloadSize);
        if (NULL == buffers[i]) {
            fprintf(stderr, "ERROR: Grabber memory alloc failed!\n");
            PylonTerminate();
            exit(EXIT_FAILURE);
        }
    }

    /* Tell the stream grabber the number and size of the buffers. */
    res = PylonStreamGrabberSetMaxNumBuffer(hGrabber, NUM_BUFFERS);
    CHECK(res);
    res = PylonStreamGrabberSetMaxBufferSize(hGrabber, payloadSize);
    CHECK(res);
    IPRINT("frame size (bytes): %i\n", payloadSize);

    /* Allocate the resources required for grabbing. */
    res = PylonStreamGrabberPrepareGrab(hGrabber);
    CHECK(res);

    /* Register buffers at the stream grabber. */
    for (i = 0; i < NUM_BUFFERS; ++i) {
        res = PylonStreamGrabberRegisterBuffer(hGrabber, buffers[i],
            payloadSize, &bufHandles[i]);
        CHECK(res);
    }

    /* Feed the buffers into the stream grabber's input queue. */
    for (i = 0; i < NUM_BUFFERS; ++i) {
        res = PylonStreamGrabberQueueBuffer(hGrabber, bufHandles[i], (void*)i);
        CHECK(res);
    }

    /* Now the stream grabber is prepared and as soon as the camera starts to
       acquire images, the image data will be grabbed into the buffers. */

    /* Let the camera acquire images. */
    res = PylonDeviceExecuteCommandFeature(hDev, "AcquisitionStart");
    CHECK(res);

    /* Grabber loop. */
    while (!pglobal->stop) {

        size_t bufferIndex;
        //unsigned char min, max;

        /* If encoders have buffers to be requeued, do it now. */
        pthread_mutex_lock(&release_buf_queue_mutex);
        while (releaseBufQueue != NULL) {
            video_frame_t *frame = pop(&releaseBufQueue);
            res = PylonStreamGrabberQueueBuffer(hGrabber,
                bufHandles[frame->buffer], (void*) frame->buffer);
            CHECK(res);
            --used_buffers;
            DBG("Freed grabber buffer %d\n", frame->buffer);
            free(frame);
            frame = NULL;
        }
        DBG("Used buffers: %d\n", used_buffers);
        pthread_mutex_unlock(&release_buf_queue_mutex);

        DBG("Waiting for frame from camera...\n");

        /* Ensure we have free buffers. */
        if (used_buffers > 4) {
            usleep(70000); /* Almost all used, sleep and check again. */
            continue;
        }

        /* Wait for the next buffer to be filled (up to 3000 ms). */
        res = PylonWaitObjectWait(hWait, 3000, &isReady);
        CHECK(res);
        if (!isReady) {
            fprintf(stderr, "Grab timeout occurred\n");
            break; /* Stop grabbing. */
        }

        DBG("Retrieving new frame from camera\n");

        /* Since the wait operation was successful, the result of at least
           one grab operation is available, retrieve it. */
        res = PylonStreamGrabberRetrieveResult(hGrabber, &grabResult,
            &isReady);
        CHECK(res);
        if (!isReady) {
            fprintf(stderr, "Failed to retrieve a grab result\n");
            break; /* Stop grabbing. */
        }

        /* Get the buffer index from the context information. */
        bufferIndex = (size_t) grabResult.Context;

        /* Check to see if the image was grabbed successfully. */
        if (grabResult.Status == Grabbed) {

            DBG("Successfully grabbed frame to buffer:%d\n", bufferIndex);

            //unsigned char* buffer;
            //buffer = (unsigned char*) grabResult.pBuffer;
            //get_min_max(buffer, grabResult.SizeX, grabResult.SizeY, &min, &max);
            //printf("Min. gray value = %3u, Max. gray value = %3u\n", min, max);

            struct timeval tv;
            gettimeofday(&tv, 0);
            video_frame_t* frame = malloc(sizeof(video_frame_t));
            frame->number = ++frameNumGrabbed;
            frame->timestamp = tv;
            frame->width = grabResult.SizeX;
            frame->height = grabResult.SizeY;
            frame->size = payloadSize;
            frame->buffer = bufferIndex;
            frame->data = buffers[bufferIndex];

            pthread_mutex_lock(&encode_queue_mutex);
            if (encodeQueue == NULL) {
                encodeQueue = malloc(sizeof(node_t));
                if (encodeQueue == NULL) {
                    fprintf(stderr, "ERROR: Grabber memory alloc failed!\n");
                    free(frame);
                    exit(EXIT_FAILURE);
                }
                encodeQueue->ptr = frame;
                encodeQueue->next = NULL;
            } else {
                push(encodeQueue, frame);
            }
            ++used_buffers;
            pthread_mutex_unlock(&encode_queue_mutex);
            pthread_cond_signal(&encode_cond);

        } else if (grabResult.Status == Failed) {
            fprintf(stderr, "Frame wasn't grabbed successfully. Error code = 0x%08X, bufs=%d\n",
                grabResult.ErrorCode, used_buffers);

            /* Requeue the buffer to be filled again. */
            res = PylonStreamGrabberQueueBuffer(hGrabber, grabResult.hBuffer,
                (void*) bufferIndex);
            CHECK(res);
        }
    }

    IPRINT("leaving input thread, calling cleanup function now\n");
    pthread_cleanup_pop(1);

    return NULL;
}

/******************************************************************************
Description.: clean up resources allocated by a grabber worker thread
Input Value.: arg is unused
Return Value: -
******************************************************************************/
void worker_grabber_cleanup(void *arg)
{
    static unsigned char first_run = 1;
    int i;

    if (!first_run) {
        DBG("Already cleaned up grabber resources\n");
        return;
    }

    first_run = 0;
    DBG("Cleaning up resources allocated by grabber thread\n");

    /* Stop the camera. */
    res = PylonDeviceExecuteCommandFeature(hDev, "AcquisitionStop");
    CHECK(res);

    /* Issue a cancel call to ensure that all pending buffers are put into the
       stream grabber's output queue. */
    res = PylonStreamGrabberCancelGrab(hGrabber);
    CHECK(res);

    /* The buffers can now be retrieved from the stream grabber. */
    do {
        res = PylonStreamGrabberRetrieveResult(hGrabber, &grabResult, &isReady);
        CHECK(res);
    } while (isReady);

    /* Deregister buffers and free the memory. */
    pthread_mutex_lock(&encode_queue_mutex);
    for (i = 0; i < NUM_BUFFERS; ++i) {
        res = PylonStreamGrabberDeregisterBuffer(hGrabber, bufHandles[i]);
        CHECK(res);
        free(buffers[i]);
    }
    pthread_mutex_unlock(&encode_queue_mutex);

    /* Release grabbing related resources. */
    res = PylonStreamGrabberFinishGrab(hGrabber);
    CHECK(res);

    /* Close the stream grabber. */
    res = PylonStreamGrabberClose(hGrabber);
    CHECK(res);

    /* Close and release the pylon device. */
    res = PylonDeviceClose(hDev);
    CHECK(res);

    /* Destroy the device. */
    res = PylonDestroyDevice(hDev);
    CHECK(res);

    /* Shut down the pylon runtime system. */
    PylonTerminate();

    /* Destroy encode queue. */
    pthread_mutex_lock(&encode_queue_mutex);
    destroy(&encodeQueue);
    encodeQueue = NULL;
    pthread_mutex_unlock(&encode_queue_mutex);

    /* Destroy buffer release queue. */
    pthread_mutex_lock(&release_buf_queue_mutex);
    destroy(&releaseBufQueue);
    releaseBufQueue = NULL;
    pthread_mutex_unlock(&release_buf_queue_mutex);
}

/******************************************************************************
Description.: encode raw image to JPEG format and signal to output plugins
Input Value.: encoder configuration struct
Return Value: NULL
******************************************************************************/
void *worker_encoder(void *arg)
{
    encoder_config_t *enc = (encoder_config_t*)arg;
    DBG("Allocating resources for encoder thread %d\n", enc->index);

    video_frame_t *frame = NULL;
    int encoded_size = -1;
    clock_t walltime;
    struct timespec tstart;
    struct timespec tstop;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    /* Set cleanup handler to cleanup allocated resources. */
    pthread_cleanup_push(worker_encoder_cleanup, arg);

    /* Create JPEG compress struct. We will reuse these in this thread. */
    create_compress(&cinfo, &jerr);

    /* Loop until worker is stopped. */
    while (!pglobal->stop) {

        /* Get a raw image from the encode queue. If queue is empty, wait. */
        encoded_size = -1;
        frame = NULL;
        pthread_mutex_lock(&encode_queue_mutex);
        if (encodeQueue == NULL) {
            DBG("Encoder %d is waiting for image from grabber\n", enc->index);
            pthread_cond_wait(&encode_cond, &encode_queue_mutex);
            if (encodeQueue == NULL) {
                fprintf(stderr, "ERROR: Frame signaled but queue is empty!\n");
                pthread_mutex_unlock(&encode_queue_mutex);
                continue; /* Maybe the next frame works... */
            }
        }

        frame = pop(&encodeQueue); /* Gain ownership of frame object. */
        print_encode_queue(encodeQueue);
        pthread_mutex_unlock(&encode_queue_mutex);

        /* Notify which image we picked from the queue (for debugging). */
        if (frame != NULL) {
            DBG("Encoder %d now compressing frame from buffer %d\n",
                enc->index, frame->buffer);
        } else {
            fprintf(stderr, "ERROR: Frame to encode is NULL!\n");
            continue; /* Maybe the next frame works... */
        }

        /* Now we know frame size; allocate memory for encode buffer. */
        if (enc->buffer == NULL) {
            enc->buffer = malloc(frame->size);
            if (enc->buffer == NULL) {
                fprintf(stderr, "ERROR: Encoder memory alloc failed!\n");
                free(frame);
                exit(EXIT_FAILURE);
            } else {
                DBG("Encoder %d allocated %d bytes for encoder buffer %p\n",
                    enc->index, frame->size, (void*)&enc->buffer);
            }
        }

        /* Encode raw image to JPEG format into encode buffer. */
        /* Performance: wall time = elapsed wall time during encoding. */
        /* Perforamnce: thread time = time that encoder thread run on CPU. */
        walltime = clock();
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tstart);
//        encoded_size = compress_yuyv_to_jpeg(frame, enc->buffer,
//            frame->size, 60, &cinfo);
        encoded_size = compress_rgb24_to_jpeg(frame, enc->buffer,
            frame->size, 50, &cinfo);
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tstop);
        walltime = clock() - walltime;
        DBG("Encoder %d completed encoding\n", enc->index);

        /* Requeue the grabber buffer to be filled again. */
        video_frame_t* releaseFrame = malloc(sizeof(video_frame_t));
        if (releaseFrame == NULL) {
            fprintf(stderr, "ERROR: Encoder memory alloc failed!\n");
            free(frame);
            exit(EXIT_FAILURE);
        }
        releaseFrame->buffer = frame->buffer; /* index of buffer to be freed. */
        DBG("Encoder %d releasing buffer %d\n", enc->index, releaseFrame->buffer);
        pthread_mutex_lock(&release_buf_queue_mutex);
        if (releaseBufQueue == NULL) {
            releaseBufQueue = malloc(sizeof(node_t));
            if (releaseBufQueue == NULL) {
                fprintf(stderr, "ERROR: Encoder memory alloc failed!\n");
                pthread_mutex_unlock(&release_buf_queue_mutex);
                free(releaseFrame);
                free(frame);
                exit(EXIT_FAILURE);
            }
            releaseBufQueue->ptr = releaseFrame;
            releaseBufQueue->next = NULL;
        } else {
            push(releaseBufQueue, releaseFrame);
        }
        pthread_mutex_unlock(&release_buf_queue_mutex);
        DBG("Encoder %d released buffer %d\n", enc->index, frame->buffer);

        /* Reserve output object. */
        pthread_mutex_lock(&pglobal->in[plugin_number].db);

        /* Ensure correct frame order: if our frame is not the next, wait. */
        int delay = 10000;
        int total_delay = 0;
#ifdef ENSURE_CORRECT_FRAME_ORDER
        while (frame->number != 0 && frame->number > frameNumLastSent + 1) {

            /* Release output object to give chance to other threads. */
            pthread_mutex_unlock(&pglobal->in[plugin_number].db);

            /* Wait a moment so other threads can send their frames. */
            usleep(delay);
            total_delay += delay;

            /* Reserve output object and check again... */
            pthread_mutex_lock(&pglobal->in[plugin_number].db);
        }
        if (total_delay > 0) {
            fprintf(stderr, "Encoder %d delayed sending frame %d for %d ms\n",
                enc->index, frame->number, total_delay / 1000);
        }
#endif

        /* Allocate memory for output buffer. */
        if (pglobal->in[plugin_number].buf == NULL) {
            pglobal->in[plugin_number].buf = malloc(frame->size);
            if (pglobal->in[plugin_number].buf == NULL) {
                pthread_mutex_unlock(&pglobal->in[plugin_number].db);
                fprintf(stderr, "ERROR: Encoder memory alloc failed!\n");
                free(frame);
                exit(EXIT_FAILURE);
            } else {
                DBG("Encoder %d allocated %d bytes for output buffer\n",
                    enc->index, frame->size);
            }
        }

        /* Copy image from encode buffer to output buffer. */
        memcpy(pglobal->in[plugin_number].buf, enc->buffer, encoded_size);
        pglobal->in[plugin_number].size = encoded_size;

        /* Set timestamp. */
        pglobal->in[plugin_number].timestamp = frame->timestamp;

        /* Signal fresh image to output plugins. */
        pthread_cond_broadcast(&pglobal->in[plugin_number].db_update);

        /* Update last sent frame number. */
        frameNumLastSent = frame->number;

        /* Release output object. */
        pthread_mutex_unlock(&pglobal->in[plugin_number].db);

        double time_taken = ((double)walltime) / CLOCKS_PER_SEC; /* in sec */
        struct timespec time_thread;
        timespec_diff(&tstart, &tstop, &time_thread);
        printf("Encoder %d: buf=%d fr=%d ts=%d.%d w_delta=%f, t_delta=%f, ratio=%d/%d=%2.1f%%\n",
            enc->index, frame->buffer, frame->number, frame->timestamp.tv_sec,
            frame->timestamp.tv_usec, time_taken, time_thread.tv_sec +
            (float)time_thread.tv_nsec / 1000000000L, encoded_size,
            frame->size, 100.0f * encoded_size / frame->size);

        /* Clean up. */
        free(frame);
        frame = NULL;
#ifdef ALLOW_FRAME_SIZE_CHANGE_DURING_STREAMING
        free(enc->buffer);
        enc->buffer = NULL;
        DBG("Encoder %d freed encoder buffer\n", enc->index);
        /* above: no need to free encoder buffer if next frame is same size! */
#endif
    }

    // Destroy JPEG compress struct.
    destroy_compress(&cinfo);

    IPRINT("Leaving encoder thread %d, calling cleanup function now\n", enc->index);
    pthread_cleanup_pop(1);

    return NULL;
}

/******************************************************************************
Description.: clean up resources allocated by an encoder worker thread
Input Value.: encoder configuration struct
Return Value: -
******************************************************************************/
void worker_encoder_cleanup(void *arg)
{
    encoder_config_t *enc = (encoder_config_t*)arg;
    DBG("Cleaning up resources allocated by encoder thread %d\n", enc->index);

    /* Free the encode buffer used by this thread. */
    if (enc->buffer != NULL) {
        free(enc->buffer);
        enc->buffer = NULL;
        DBG("Cleaned encoder thread %d buffer\n", enc->index);
    }

    /* Encoder cleanup done, decrement online encoder count. */
    encoders_online--;
    DBG("Encoders still online: %d\n", encoders_online);

    /* After last encoder, free the common output buffer used by this plugin. */
    if (encoders_online == 0) {
        DBG("All encoders cleaned, now cleaning common resources\n");
        pthread_mutex_lock(&pglobal->in[plugin_number].db);
        if (pglobal->in[plugin_number].buf != NULL) {
            free(pglobal->in[plugin_number].buf);
            pglobal->in[plugin_number].buf = NULL;
            DBG("Cleaned plugin output buffer\n");
        }
        pthread_mutex_unlock(&pglobal->in[plugin_number].db);
        DBG("Common resources cleaned\n");
    }
}

/******************************************************************************
Description.: print the error msg for the last failed call, and exit
Input Value.: error code
Return Value: -
******************************************************************************/
void print_error_and_exit(GENAPIC_RESULT errc)
{
    char *errMsg;
    size_t length;

    /* Print error msg and code. */
    GenApiGetLastErrorMessage(NULL, &length);
    errMsg = (char*) malloc(length);
    GenApiGetLastErrorMessage(errMsg, &length);
    fprintf(stderr, "%s (%#08x).\n", errMsg, (unsigned int) errc);
    free(errMsg);

    /* Print error details. */
    GenApiGetLastErrorDetail(NULL, &length);
    errMsg = (char*) malloc(length);
    GenApiGetLastErrorDetail(errMsg, &length);
    fprintf(stderr, "%s\n", errMsg);
    free(errMsg);

    /* Release all pylon resources and exit. */
    PylonTerminate();
    exit(EXIT_FAILURE);
}

/******************************************************************************
Description.: return min and max gray value of a monochrome 8 bit image
Input Value.: image, image width & height, min and max values (on return)
Return Value: -
******************************************************************************/
void get_min_max(const unsigned char* pImg, int32_t width, int32_t height,
               unsigned char* pMin, unsigned char* pMax)
{
    unsigned char min = 255;
    unsigned char max = 0;
    unsigned char val;
    const unsigned char *p;

    /* Iterate through each pixel of the image, updating min and max values. */
    for (p = pImg; p < pImg + width * height; p++) {
        val = *p;
        if (val > max)
           max = val;
        if (val < min)
           min = val;
    }
    *pMin = min;
    *pMax = max;
}

/******************************************************************************
Description.: calculate diff of two timespecs
             (adobted from https://gist.github.com/diabloneo/9619917)
Input Value.: start, stop, and result timespecs
Return Value: -
******************************************************************************/
void timespec_diff(struct timespec *start, struct timespec *stop,
                   struct timespec *result)
{
    if ((stop->tv_nsec - start->tv_nsec) < 0) {
        result->tv_sec = stop->tv_sec - start->tv_sec - 1;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec + 1000000000;
    } else {
        result->tv_sec = stop->tv_sec - start->tv_sec;
        result->tv_nsec = stop->tv_nsec - start->tv_nsec;
    }
}
