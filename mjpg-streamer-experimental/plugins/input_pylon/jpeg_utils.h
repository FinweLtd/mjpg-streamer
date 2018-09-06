/* Configuration struct for an image. */
typedef struct image_config {
    int frame;
    struct timeval timestamp;
    int width;
    int height;
    int size;
    int buffer;
    unsigned char *data;
} image_config_t;

//int compress_yuyv_to_jpeg(image_config_t *img, unsigned char *buffer, int size, int quality);

int compress_yuyv_to_jpeg(image_config_t *img, unsigned char *buffer, int size, int quality,
        struct jpeg_compress_struct *cinfo);

int compress_rgb8_to_jpeg(image_config_t *img, unsigned char *buffer, int size, int quality,
        struct jpeg_compress_struct *cinfo);

void create_compress(struct jpeg_compress_struct *cinfo, struct jpeg_error_mgr *jerr);

void destroy_compress(struct jpeg_compress_struct *cinfo);


