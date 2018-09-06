/* Video frame struct. */
typedef struct video_frame {
    int number;               /* Running number of video frame, starts from 0 */
    struct timeval timestamp; /* Time when the frame was grabbed from camera */
    int width;                /* Video frame width in pixels */
    int height;               /* Video frame height in pixels */
    int size;                 /* Video frame size in bytes */
    int buffer;               /* Grabber buffer index where data was captured */
    unsigned char *data;      /* Raw uncompressed video frame data */
} video_frame_t;

/* Initialize JPEG compress struct. */
void create_compress(struct jpeg_compress_struct *cinfo,
    struct jpeg_error_mgr *jerr);

/* Compress raw YUYV video frame to a JPEG image. */
int compress_yuyv_to_jpeg(video_frame_t *frame, unsigned char *buffer,
    int size, int quality, struct jpeg_compress_struct *cinfo);

/* Compress raw RGB24 video frame to a JPEG image. */
int compress_rgb24_to_jpeg(video_frame_t *frame, unsigned char *buffer,
    int size, int quality, struct jpeg_compress_struct *cinfo);

void destroy_compress(struct jpeg_compress_struct *cinfo);
