/*******************************************************************************
#                                                                              #
#      MJPG-streamer allows to stream JPG frames from an input-plugin          #
#      to several output plugins                                               #
#                                                                              #
#      Copyright (C) 2007 Tom Stöveken                                         #
#                                                                              #
#      Input plugin for Basler cameras via their Pylon software suite          #
#      Copyright (C) 2018 Tapani Rantakokko                                    #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; version 2 of the License.                      #
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

void printErrorAndExit(GENAPIC_RESULT errc);

#define CHECK( errc ) if ( GENAPI_E_OK != errc ) printErrorAndExit( errc )

/* Private functions and variables to this plugin. */
static pthread_t   worked_grabber;
static pthread_t   worker_encoder_1;
static pthread_t   worker_encoder_2;
static pthread_t   worker_encoder_3;
static pthread_t   worker_encoder_4;
static globals     *pglobal;
static pthread_mutex_t controls_mutex;
static pthread_mutex_t encode_queue_mutex;
static pthread_mutex_t release_buf_queue_mutex;
static pthread_cond_t encode_cond;
static int plugin_number;
static int current_buffer_index;

void *worker_thread(void *);
void worker_cleanup(void *);
void *worker_encoder(void *);
void worker_encoder_cleanup(void *);
void help(void);

static int delay = 1000;

/* details of converted JPG pictures */
struct pic {
    const unsigned char *data;
    const int size;
};

/* return value of pylon methods */
GENAPIC_RESULT res;

/* number of available devices */
size_t numDevices;

/* handle for the pylon device */
PYLON_DEVICE_HANDLE hDev;

/* handle for the pylon stream grabber */
PYLON_STREAMGRABBER_HANDLE hGrabber;

/* handle used for waiting for a grab to be finished */
PYLON_WAITOBJECT_HANDLE hWait;

/* size of an image frame in bytes */
int32_t payloadSize;

/* buffers used for grabbing */
unsigned char *buffers[NUM_BUFFERS];

/* handles for the buffers */
PYLON_STREAMBUFFER_HANDLE bufHandles[NUM_BUFFERS];

int frameWidths[NUM_BUFFERS];
int frameHeights[NUM_BUFFERS];
int frameSizes[NUM_BUFFERS];

/* stores the result of a grab operation */
PylonGrabResult_t grabResult;

/* the number of streams the device provides */
size_t nStreams;

/* used for checking feature availability */
_Bool isAvail;

/* used as an output parameter */
_Bool isReady;

/* counter */
size_t i;

/* Linked list node. */
typedef struct node {
    void *ptr;
    struct node *next;
} node_t;


/* push to list end */
void push(node_t *head, void* ptr) {
    node_t *current = head;
    while(current->next != NULL) {
        current = current->next;
    }
    current->next = malloc(sizeof(node_t));
    current->next->ptr = ptr;
    current->next->next = NULL;
}

/* pop from list head */
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

/* destroy list */
void destroy(node_t **head) {
    if (*head == NULL) {
        return;
    } else {
        destroy(&((*head)->next));
        free(*head);
    }
}

unsigned int frameNumGrabbed = -1;
unsigned int frameNumLastSent = -1;

int used_buffers = 0;

//int getFreeBufsCount(node_t *head) {
//    int count = 0;
//    node_t *current = head;
//
//    while(current != NULL) {
//        count++;
//        current = current->next;
//    }
//
//    return NUM_BUFFERS - count;
//}

/* Debug print encode queue contents. */
void print_encode_queue(node_t *head) {
    node_t *current = head;

    while(current != NULL) {
        struct image_config* image = (struct image_config*) current->ptr;
        DBG("frame=%d, width=%d, height=%d, size=%d\n, buffer=%d, ptr=%p\n",
            image->frame, image->width, image->height,
            image->size, image->buffer, (void*)&image->data);
        current = current->next;
    }
}

/* queue of buffer indexes that have a frame waiting to be encoded. */
node_t *encodeQueue = NULL;

/* queue of buffer indexes that are no longer used by encoder threads. */
node_t *releaseBufQueue = NULL;

/* Configuration struct for encoder thread. */
typedef struct encoder_config {
    int index;
    unsigned char *buffer;
} encoder_config_t;

struct encoder_config enc1;
struct encoder_config enc2;
struct encoder_config enc3;
struct encoder_config enc4;

/* The number of encoder threads currently online. */
int encoders_online = 0;

/*** plugin interface functions ***/

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
    pthread_cancel(worker_encoder_1);
    pthread_cancel(worker_encoder_2);
    pthread_cancel(worked_grabber);

    return 0;
}

/******************************************************************************
Description.: start the worker threads
Input Value.: plugin id
Return Value: 0
******************************************************************************/
int input_run(int id)
{
    current_buffer_index = -1;

    DBG("Creating frame grabber thread 1\n");
    if (pthread_create(&worked_grabber, 0, worker_thread, NULL) != 0) {
        free(pglobal->in[id].buf);
        fprintf(stderr, "ERROR: Failed to start grabber thread 1!\n");
        exit(EXIT_FAILURE);
    }
    pthread_detach(worked_grabber);

    DBG("Creating JPEG encoder thread 1\n");
    enc1.index = 1;
    enc1.buffer = NULL;
    if (pthread_create(&worker_encoder_1, 0, worker_encoder, (void*)&enc1) != 0) {
        fprintf(stderr, "ERROR: Could not start encoder thread 1\n");
        exit(EXIT_FAILURE);
    }
    pthread_detach(worker_encoder_1);
    encoders_online++;

    DBG("Creating JPEG encoder thread 2\n");
    enc2.index = 2;
    enc2.buffer = NULL;
    if (pthread_create(&worker_encoder_2, 0, worker_encoder, (void*)&enc2) != 0) {
        fprintf(stderr, "ERROR: Could not start encoder thread 2\n");
        exit(EXIT_FAILURE);
    }
    pthread_detach(worker_encoder_2);
    encoders_online++;

//    DBG("Creating JPEG encoder thread 3\n");
//    enc3.index = 3;
//    enc3.buffer = NULL;
//    if (pthread_create(&worker_encoder_3, 0, worker_encoder, (void*)&enc3) != 0) {
//        fprintf(stderr, "ERROR: Could not start encoder thread 3\n");
//        exit(EXIT_FAILURE);
//    }
//    pthread_detach(worker_encoder_3);
//    encoders_online++;
//
//    DBG("Creating JPEG encoder thread 4\n");
//    enc4.index = 4;
//    enc4.buffer = NULL;
//    if (pthread_create(&worker_encoder_4, 0, worker_encoder, (void*)&enc4) != 0) {
//        fprintf(stderr, "ERROR: Could not start encoder thread 4\n");
//        exit(EXIT_FAILURE);
//    }
//    pthread_detach(worker_encoder_4);
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
Description.: acquire a picture from camera, encode to JPEG format, and signal
              this to all output plugins
Input Value.: arg is not used
Return Value: NULL
******************************************************************************/
void* worker_thread(void *arg)
{
    int i = 0;
    NODEMAP_HANDLE      hNodeMap;
    NODE_HANDLE         hNode;
    const char          * pFeatureName;
    _Bool                val, val_read, val_write;

    /* set cleanup handler to cleanup allocated resources */
    pthread_cleanup_push(worker_cleanup, NULL);

        /* initialize pylon runtime */
    PylonInitialize();

    /* enumerate all camera devices */
    res = PylonEnumerateDevices(&numDevices);
    CHECK(res);
    if(0 == numDevices) {
        fprintf(stderr, "No devices found.\n");
        PylonTerminate();
        exit(EXIT_FAILURE);
    } else {
        IPRINT("cameras found.....: %i\n", numDevices);
    }

    /* get a handle for the first device found */
    res = PylonCreateDeviceByIndex(0, &hDev);
    CHECK(res);

    /* open it for configuring parameters and for grabbing images */
    res = PylonDeviceOpen(hDev,
        PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM);
    CHECK(res);

    /* print out the name of the camera */
    {
        char buf[256];
        size_t siz = sizeof(buf);
        _Bool isReadable;

        isReadable = PylonDeviceFeatureIsReadable(hDev, "DeviceModelName");
        if(isReadable) {
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
    if(isAvail) {
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
    /* Write OffsetX. */
    pFeatureName = "OffsetX";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if(GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 272);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 272);
    }
    else
    {
        /* Node does not exist --> feature is not implemented. */
        val = 0;
    }

    /* Write OffsetY. */
    pFeatureName = "OffsetY";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if(GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 204);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 204);
    }
    else
    {
        /* Node does not exist --> feature is not implemented. */
        val = 0;
    }


    /* Write width. */
    pFeatureName = "Width";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if(GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 2048);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 2048);
    }
    else
    {
        /* Node does not exist --> feature is not implemented. */
        val = 0;
    }
//    printf("The '%s' feature %s implemented\n", pFeatureName, val ? "is" : "is not");
//
    /* Write height. */
    pFeatureName = "Height";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if(GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiIntegerSetValue(hNode, 1536);
        CHECK(res);
        printf("The '%s' is now %d\n", pFeatureName, 1536);
    }
    else
    {
        /* Node does not exist --> feature is not implemented. */
        val = 0;
    }
    printf("The '%s' feature %s implemented\n", pFeatureName, val ? "is" : "is not");

    /* Write acquisition frame rate. */
    pFeatureName = "AcquisitionFrameRate";
    res = GenApiNodeMapGetNode(hNodeMap, pFeatureName, &hNode);
    CHECK(res);
    if(GENAPIC_INVALID_HANDLE != hNode)
    {
        res = GenApiFloatSetValue(hNode, 7.5f);
        CHECK(res);
        printf("The '%s' is now %f\n", pFeatureName, 7.5f);
    }
    else
    {
        /* Node does not exist --> feature is not implemented. */
        val = 0;
    }
//    printf("The '%s' feature %s implemented\n", pFeatureName, val ? "is" : "is not");


    /* disable acquisition start trigger if available */
    isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_TriggerSelector_AcquisitionStart");
    if(isAvail) {
        res = PylonDeviceFeatureFromString(hDev, "TriggerSelector",
            "AcquisitionStart");
        CHECK(res);
        res = PylonDeviceFeatureFromString(hDev, "TriggerMode", "Off");
        CHECK(res);
    }

    /* disable frame burst start trigger if available */
    isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_TriggerSelector_FrameBurstStart");
    if(isAvail) {
        res = PylonDeviceFeatureFromString(hDev, "TriggerSelector",
            "FrameBurstStart");
        CHECK(res);
        res = PylonDeviceFeatureFromString(hDev, "TriggerMode", "Off");
        CHECK(res);
    }

    /* disable frame start trigger if available */
    isAvail = PylonDeviceFeatureIsAvailable(hDev,
        "EnumEntry_TriggerSelector_FrameStart");
    if(isAvail) {
        res = PylonDeviceFeatureFromString(hDev, "TriggerSelector",
            "FrameStart");
        CHECK(res);
        res = PylonDeviceFeatureFromString(hDev, "TriggerMode", "Off");
        CHECK(res);
    }

    /* use the Continuous frame acquisition mode, i.e., the camera delivers
       images continuously */
    res = PylonDeviceFeatureFromString(hDev, "AcquisitionMode", "Continuous");
    CHECK(res);

    /* for GigE cameras, increase the packet size for better performance:
       when the network adapter supports jumbo frames, set the packet
       size to a value > 1500, e.g., to 8192 */
    isAvail = PylonDeviceFeatureIsWritable(hDev, "GevSCPSPacketSize");
    if(isAvail) {
        res = PylonDeviceSetIntegerFeature(hDev, "GevSCPSPacketSize", 1500);
        CHECK(res);
    }

    /* number of streams supported by the device and the transport layer */
    res = PylonDeviceGetNumStreamGrabberChannels(hDev, &nStreams);
    CHECK(res);
    if(nStreams < 1) {
        fprintf(stderr, "The transport layer doesn't support image streams\n");
        PylonTerminate();
        exit(EXIT_FAILURE);
    }

    /* create and open a stream grabber for the first channel */
    res = PylonDeviceGetStreamGrabber(hDev, 0, &hGrabber);
    CHECK(res);
    res = PylonStreamGrabberOpen(hGrabber);
    CHECK(res);

    /* get a handle for the stream grabber's wait object */
    res = PylonStreamGrabberGetWaitObject(hGrabber, &hWait);
    CHECK(res);

    /* determine the required size of the grab buffer */
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
        if(GENAPIC_INVALID_HANDLE == hNode) {
            fprintf(stderr, "There is no PayloadSize parameter.\n");
            PylonTerminate();
            exit(EXIT_FAILURE);
        }
        res = GenApiIntegerGetValue(hNode, &i64payloadSize);
        CHECK(res);
        payloadSize = (int32_t) i64payloadSize;
    }

    /* allocate memory for grabbing */
    for(i = 0; i < NUM_BUFFERS; ++i) {
        buffers[i] = (unsigned char*) malloc(payloadSize);
        if(NULL == buffers[i]) {
            fprintf(stderr, "Out of memory!\n");
            PylonTerminate();
            exit(EXIT_FAILURE);
        }
    }

    /* tell the stream grabber the number and size of the buffers */
    res = PylonStreamGrabberSetMaxNumBuffer(hGrabber, NUM_BUFFERS);
    //res = PylonStreamGrabberSetMaxNumBuffer(hGrabber, 1);
    CHECK(res);
    res = PylonStreamGrabberSetMaxBufferSize(hGrabber, payloadSize);
    //res = PylonStreamGrabberSetMaxBufferSize(hGrabber, 263168); // works with 262144!
    CHECK(res);

    IPRINT("payload...........: %i\n", payloadSize);

    /* allocate the resources required for grabbing */
    res = PylonStreamGrabberPrepareGrab(hGrabber);
    CHECK(res);

    /* register buffers at the stream grabber */
    for(i = 0; i < NUM_BUFFERS; ++i) {
        res = PylonStreamGrabberRegisterBuffer(hGrabber, buffers[i],
            payloadSize, &bufHandles[i]);
        CHECK(res);
    }

    /* feed the buffers into the stream grabber's input queue */
    for(i = 0; i < NUM_BUFFERS; ++i) {
        res = PylonStreamGrabberQueueBuffer(hGrabber, bufHandles[i], (void*)i);
        CHECK(res);
    }

    /* now the stream grabber is prepared and as soon as the camera starts to
       acquire images, the image data will be grabbed into the buffers provided */

    /* let the camera acquire images */
    res = PylonDeviceExecuteCommandFeature(hDev, "AcquisitionStart");
    CHECK(res);

    while(!pglobal->stop) {

        size_t bufferIndex;
        //unsigned char min, max;

        pthread_mutex_lock(&release_buf_queue_mutex);

        while(releaseBufQueue != NULL) {
            DBG("About to pop from grabber buffer...\n");
            image_config_t *img = pop(&releaseBufQueue);
            DBG("Popped from grabber buffer %d\n", img->buffer);
            res = PylonStreamGrabberQueueBuffer(hGrabber, bufHandles[img->buffer],
                (void*) img->buffer);
            CHECK(res);
            DBG("Freed grabber buffer %d\n", img->buffer);
            --used_buffers;
            free(img);
            img = NULL;
        }

        DBG("Used buffers: %d\n", used_buffers);

        pthread_mutex_unlock(&release_buf_queue_mutex);

        DBG("Waiting for frame from camera...\n");

        /* ensure we have free buffers */
        if(used_buffers > 4) {
            usleep(70000);
            continue;
        }

        /* wait for the next buffer to be filled (up to 2000 ms) */
        res = PylonWaitObjectWait(hWait, 2000, &isReady);
        CHECK(res);
        if(!isReady) {
            /* timeout occurred */
            fprintf(stderr, "Grab timeout occurred\n");
            break; /* stop grabbing */
        }

        DBG("Retrieving new frame from camera\n");

//        /* wait until we have free buffers */
//        while (used_buffers >= (NUM_BUFFERS)) {
//            fprintf(stderr, "Grabber waiting for FREE BUFFER (used: %d)\n", used_buffers);
//            usleep(10000);
//        }

        /* since the wait operation was successful, the result of at least
           one grab operation is available, retrieve it */
        res = PylonStreamGrabberRetrieveResult(hGrabber, &grabResult,
            &isReady);
        CHECK(res);
        if(!isReady) {
            /* should never come here... */
            fprintf(stderr, "Failed to retrieve a grab result\n");
            break;
        }

        /* get the buffer index from the context information */
        bufferIndex = (size_t) grabResult.Context;

        /* check to see if the image was grabbed successfully */
        if(grabResult.Status == Grabbed) {

            DBG("Successfully grabbed frame to buffer:%d\n", bufferIndex);

            //unsigned char* buffer;
            //buffer = (unsigned char*) grabResult.pBuffer;
            //getMinMax(buffer, grabResult.SizeX, grabResult.SizeY, &min, &max);
            //printf("Min. gray value = %3u, Max. gray value = %3u\n", min, max);


            //current_buffer_index = bufferIndex;

            struct timeval tv;
            gettimeofday(&tv, 0);
            image_config_t* image = malloc(sizeof(image_config_t));
            image->frame = ++frameNumGrabbed;
            image->timestamp = tv;
            image->width = grabResult.SizeX;
            image->height = grabResult.SizeY;
            image->size = payloadSize;
            image->buffer = bufferIndex;
            image->data = buffers[bufferIndex];

            pthread_mutex_lock(&encode_queue_mutex);
            if(encodeQueue == NULL) {
                encodeQueue = malloc(sizeof(node_t));
                if (encodeQueue == NULL) {
                    fprintf(stderr, "Out of memory!\n");
                    exit(EXIT_FAILURE);
                }
                encodeQueue->ptr = image;
                encodeQueue->next = NULL;
            } else {
                push(encodeQueue, image);
            }

            ++used_buffers;

//            print_encode_queue(encodeQueue);
//            frameWidths[bufferIndex] = grabResult.SizeX;
//            frameHeights[bufferIndex] = grabResult.SizeY;
//            DBG("Current buffer index: %d\n", bufferIndex);
            pthread_mutex_unlock(&encode_queue_mutex);
            pthread_cond_signal(&encode_cond);

//            fprintf(stderr, "Used buffers: %d\n", used_buffers);

        } else if (grabResult.Status == Failed) {
            fprintf(stderr, "Frame wasn't grabbed successfully. Error code = 0x%08X, bufs=%d\n",
                grabResult.ErrorCode, used_buffers);

            /* requeue the buffer to be filled again */
            res = PylonStreamGrabberQueueBuffer(hGrabber, grabResult.hBuffer,
                (void*) bufferIndex);
            CHECK(res);
        }

        /* requeue the buffer to be filled again */
//        res = PylonStreamGrabberQueueBuffer(hGrabber, grabResult.hBuffer,
//            (void*) bufferIndex);
//        CHECK(res);
    }

    IPRINT("leaving input thread, calling cleanup function now\n");
    pthread_cleanup_pop(1);

    return NULL;
}

/******************************************************************************
Description.: this functions cleans up allocated resources
Input Value.: arg is unused
Return Value: -
******************************************************************************/
void worker_cleanup(void *arg)
{
    static unsigned char first_run = 1;

    if(!first_run) {
        DBG("already cleaned up ressources\n");
        return;
    }

    first_run = 0;
    DBG("cleaning up resources allocated by input thread\n");

    /* stop the camera */
    res = PylonDeviceExecuteCommandFeature(hDev, "AcquisitionStop");
    CHECK(res);

    /* issue a cancel call to ensure that all pending buffers are put into the
       stream grabber's output queue */
    res = PylonStreamGrabberCancelGrab(hGrabber);
    CHECK(res);

    /* the buffers can now be retrieved from the stream grabber */
    do {
        res = PylonStreamGrabberRetrieveResult(hGrabber, &grabResult, &isReady);
        CHECK(res);
    } while (isReady);

    /* deregister buffers and free the memory */
    pthread_mutex_lock(&encode_queue_mutex);
    for(i = 0; i < NUM_BUFFERS; ++i) {
        res = PylonStreamGrabberDeregisterBuffer(hGrabber, bufHandles[i]);
        CHECK(res);
        free(buffers[i]);
    }
    pthread_mutex_lock(&encode_queue_mutex);

    /* release grabbing related resources */
    res = PylonStreamGrabberFinishGrab(hGrabber);
    CHECK(res);

    /* close the stream grabber */
    res = PylonStreamGrabberClose(hGrabber);
    CHECK(res);

    /* close and release the pylon device */
    res = PylonDeviceClose(hDev);
    CHECK(res);

    /* destroy the device */
    res = PylonDestroyDevice(hDev);
    CHECK(res);

    /* shut down the pylon runtime system */
    PylonTerminate();

    destroy(&encodeQueue);
}

/******************************************************************************
Description.: encode raw image to JPEG format and signal to output plugins
Input Value.: encoder configuration struct
Return Value: NULL
******************************************************************************/
void *worker_encoder(void *arg)
{
    int encoded_size = -1;
    clock_t t;
    struct timespec tstart;
    struct timespec tstop;
    image_config_t *image = NULL;
    encoder_config_t *enc = (encoder_config_t*)arg;
    DBG("Allocating resources for encoder thread %d\n", enc->index);

    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;

    create_compress(&cinfo, &jerr);

    /* Set cleanup handler to cleanup allocated resources. */
    pthread_cleanup_push(worker_encoder_cleanup, arg);

    /* Loop until worker is stopped. */
    while (!pglobal->stop) {

        /* Get a raw image from the encode queue. If queue is empty, wait. */
        encoded_size = -1;
        image = NULL;
        pthread_mutex_lock(&encode_queue_mutex);
        if (encodeQueue == NULL) {
            DBG("Encoder %d is waiting for image from grabber\n", enc->index);
            pthread_cond_wait(&encode_cond, &encode_queue_mutex);
            if (encodeQueue == NULL) {
                fprintf(stderr, "ERROR: Image signaled but queue is empty!\n");
                pthread_mutex_unlock(&encode_queue_mutex);
                continue; /* Maybe the next image works? */
            }
        }

        image = pop(&encodeQueue); /* Gain ownership of image object. */
        print_encode_queue(encodeQueue);
        pthread_mutex_unlock(&encode_queue_mutex);

        /* Notify which image we picked from the queue (for debugging). */
        if (image != NULL) {
            DBG("Encoder %d now compressing image from buffer %d\n",
                enc->index, image->buffer);
        } else {
            fprintf(stderr, "ERROR: Image to encode is NULL!\n");
            continue;
        }

        /* Allocate memory for encode buffer. */
        if (enc->buffer == NULL) {
            enc->buffer = malloc(image->size);
            if (enc->buffer == NULL) {
                fprintf(stderr, "ERROR: Encoder memory alloc failed!\n");
                free(image);
                exit(EXIT_FAILURE);
            } else {
                DBG("Encoder %d allocated %d bytes for encoder buffer %p\n",
                    enc->index, image->size, (void*)&enc->buffer);
            }
        }

        /* Encode raw image to JPEG format into encode buffer. */
        t = clock();
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tstart);
//        encoded_size = compress_yuyv_to_jpeg(image, enc->buffer,
//            image->size, 60, &cinfo);
        encoded_size = compress_rgb8_to_jpeg(image, enc->buffer,
            image->size, 50, &cinfo);
        t = clock() - t;
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &tstop);

        DBG("Encoder %d completed encoding\n", enc->index);

        /* Requeue the grabber buffer to be filled again. */
        pthread_mutex_lock(&release_buf_queue_mutex);

        DBG("Encoder %d releasing buffer\n", enc->index);

        if(releaseBufQueue == NULL) {
            releaseBufQueue = malloc(sizeof(node_t));
            if (releaseBufQueue == NULL) {
                fprintf(stderr, "Out of memory!\n");
                pthread_mutex_unlock(&release_buf_queue_mutex);
                exit(EXIT_FAILURE);
            }
            releaseBufQueue->ptr = image;
            releaseBufQueue->next = NULL;
        } else {
            push(releaseBufQueue, image);
        }
        pthread_mutex_unlock(&release_buf_queue_mutex);

        DBG("Encoder %d released buffer\n", enc->index);

        /* Reserve output object. */
        pthread_mutex_lock(&pglobal->in[plugin_number].db);

        /* Check if our frame is the next one to be sent, else wait. */
        int delay = 10000;
        int total_delay = 0;
//        while (image->frame != 0 && image->frame > frameNumLastSent + 1) {
//
//            /* Release output object to give chance to other threads. */
//            pthread_mutex_unlock(&pglobal->in[plugin_number].db);
//
//            /* Wait a moment so other thread can send its frame. */
//            usleep(delay);
//            total_delay += delay;
//
//            /* Reserve output object and check again... */
//            pthread_mutex_lock(&pglobal->in[plugin_number].db);
//        }
//        if (total_delay > 0) {
//            fprintf(stderr, "Encoder %d delayed sending frame %d for %d ms\n",
//                enc->index, image->frame, total_delay / 1000);
//        }

        /* Allocate memory for output buffer. */
        if (pglobal->in[plugin_number].buf == NULL) {
            pglobal->in[plugin_number].buf = malloc(image->size);
            if (pglobal->in[plugin_number].buf == NULL) {
                fprintf(stderr, "ERROR: Plugin memory alloc failed!\n");
                free(image);
                exit(EXIT_FAILURE);
            } else {
                DBG("Encoder %d allocated %d bytes for output buffer\n",
                    enc->index, image->size);
            }
        }

        /* Copy image from encode buffer to output buffer. */
        memcpy(pglobal->in[plugin_number].buf, enc->buffer, encoded_size);
        pglobal->in[plugin_number].size = encoded_size;

        /* Set timestamp. */
        pglobal->in[plugin_number].timestamp = image->timestamp;

        /* Update last sent frame number. */
        frameNumLastSent = image->frame;

        /* Signal fresh image to output plugins. */
        pthread_cond_broadcast(&pglobal->in[plugin_number].db_update);

        /* Release output object. */
        pthread_mutex_unlock(&pglobal->in[plugin_number].db);

        double time_taken = ((double)t)/CLOCKS_PER_SEC; /* in seconds */
        struct timespec time_thread;
        timespec_diff(&tstart, &tstop, &time_thread);
        printf("Encoder %d: buf=%d fr=%d time=%d.%d w_delta=%f, t_delta=%f, ratio=%d/%d=%2.1f%%\n",
            enc->index, image->buffer, image->frame, image->timestamp.tv_sec,
            image->timestamp.tv_usec, time_taken, time_thread.tv_sec +
            (float)time_thread.tv_nsec / 1000000000L, encoded_size,
            image->size, 100.0f * encoded_size / image->size);

        /* Clean up. */
        /*
        free(image);
        image = NULL;*/
        /*free(enc->buffer);
        enc->buffer = NULL;
        DBG("Encoder %d freed encoder buffer\n", enc->index);*/
        /* above: no need to free buffer if next image is same size! */

        //if (delay != 0) usleep(1000 * delay);
    }

    destroy_compress(&cinfo);

    IPRINT("leaving input thread, calling cleanup function now\n");
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
void printErrorAndExit(GENAPIC_RESULT errc)
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
void getMinMax(const unsigned char* pImg, int32_t width, int32_t height,
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

