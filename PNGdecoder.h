#ifndef PNGdecoder_H
#define PNGdecoder_H

#ifdef PNGdecoder_IMPORT
    #define EXTERN
#else
    #define EXTERN extern
#endif // PNGdecoder_IMPORT

#include <stdint.h>

/*      COMMON CONSTANTS        */


typedef enum _PNGdecoder_result {
    PNGDECODER_OK,
    PNGDECODER_INVALID_ARGUMENT,
    PNGDECODER_FILE_OPEN_ERROR,
    PNGDECODER_BAD_PNG,
    PNGDECODER_MISMATCHING_CRC,
    PNGDECODER_MISSING_IHDR,
    PNGDECODER_MISSING_PLTE,
    PNGDECODER_MISSING_IDAT,
    PNGDECODER_MISSING_IEND,
    PNGDECODER_INVALID_IHDR,
    PNGDECODER_INVALID_PLTE,
    PNGDECODER_ZLIB_ERROR,
    PNGDECODER_RESULTS_COUNT
} PNGdecoder_result;

typedef enum _PNGdecoder_raster_types {
    PNGDECODER_RASTER_INVALID = -1,
    PNGDECODER_RASTER_GRAYSCALE_8,
    PNGDECODER_RASTER_GRAYSCALE_16,
    PNGDECODER_RASTER_RGB_8,
    PNGDECODER_RASTER_RGB_16,
    PNGDECODER_RASTER_GRAYSCALE_8A,
    PNGDECODER_RASTER_GRAYSCALE_16A,
    PNGDECODER_RASTER_RGBA_8,
    PNGDECODER_RASTER_RGBA_16
} PNGdecoder_raster_types;


/*      MAIN DATA TYPES     */


//      Main structure handled by the module
typedef struct _PNGdecoder_PNG PNGdecoder_PNG;


/*      RASTER PIXEL TYPES        */


typedef struct _PNGdecoder_grayscale8a_t {
    uint8_t level;
    uint8_t alpha;
} PNGdecoder_grayscale8a_t;

typedef struct _PNGdecoder_grayscale16a_t {
    uint16_t level;
    uint16_t alpha;
} PNGdecoder_grayscale16a_t;

typedef struct _PNGdecoder_RGB8_t {
    uint8_t R;
    uint8_t G;
    uint8_t B;
} PNGdecoder_RGB8_t;

typedef struct _PNGdecoder_RGB16_t {
    uint16_t R;
    uint16_t G;
    uint16_t B;
} PNGdecoder_RGB16_t;

typedef struct _PNGdecoder_RGBA8_t {
    uint8_t R;
    uint8_t G;
    uint8_t B;
    uint8_t A;
} PNGdecoder_RGBA8_t;

typedef struct _PNGdecoder_RGBA16_t {
    uint16_t R;
    uint16_t G;
    uint16_t B;
    uint16_t A;
} PNGdecoder_RGBA16_t;


/*      RASTER TYPES        */


typedef struct _PNGdecoder_raster_grayscale8_t {
    uint32_t width;
    uint32_t height;
    uint8_t * raster;
} PNGdecoder_raster_grayscale8_t;

typedef struct _PNGdecoder_raster_grayscale16_t {
    uint32_t width;
    uint32_t height;
    uint16_t * raster;
} PNGdecoder_raster_grayscale16_t;

typedef struct _PNGdecoder_raster_grayscale8a_t {
    uint32_t width;
    uint32_t height;
    PNGdecoder_grayscale8a_t * raster;
} PNGdecoder_raster_grayscale8a_t;

typedef struct _PNGdecoder_raster_grayscale16a_t {
    uint32_t width;
    uint32_t height;
    PNGdecoder_grayscale16a_t * raster;
} PNGdecoder_raster_grayscale16a_t;

typedef struct _PNGdecoder_raster_RGB8_t {
    uint32_t width;
    uint32_t height;
    PNGdecoder_RGB8_t * raster;
} PNGdecoder_raster_RGB8_t;

typedef struct _PNGdecoder_raster_RGB16_t {
    uint32_t width;
    uint32_t height;
    PNGdecoder_RGB16_t * raster;
} PNGdecoder_raster_RGB16_t;

typedef struct _PNGdecoder_raster_RGBA8_t {
    uint32_t width;
    uint32_t height;
    PNGdecoder_RGBA8_t * raster;
} PNGdecoder_raster_RGBA8_t;

typedef struct _PNGdecoder_raster_RGBA16_t {
    uint32_t width;
    uint32_t height;
    PNGdecoder_RGBA16_t * raster;
} PNGdecoder_raster_RGBA16_t;


/*      MODULE INTERFACE        */


EXTERN PNGdecoder_result PNGdecoder_openPNG(char *, PNGdecoder_PNG **);
EXTERN void PNGdecoder_free(PNGdecoder_PNG *);
EXTERN const char * PNGdecoder_strerror(PNGdecoder_result);

EXTERN PNGdecoder_raster_types PNGdecoder_get_raster_type(PNGdecoder_PNG *);
EXTERN const void * PNGdecoder_get_raster(PNGdecoder_PNG *);
EXTERN const uint8_t PNGdecoder_get_depth(PNGdecoder_PNG *);
EXTERN const uint32_t PNGdecoder_get_width(PNGdecoder_PNG *);
EXTERN const uint32_t PNGdecoder_get_height(PNGdecoder_PNG *);

EXTERN PNGdecoder_raster_RGBA8_t * PNGdecoder_as_RGBA8(PNGdecoder_PNG *);
EXTERN PNGdecoder_raster_RGBA16_t * PNGdecoder_as_RGBA16(PNGdecoder_PNG *);
EXTERN void PNGdecoder_raster_free(void *, PNGdecoder_raster_types);


#undef PNGdecoder_IMPORT
#undef EXTERN
#endif // PNGdecoder_H
