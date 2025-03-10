#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <zlib.h>

#define PNGdecoder_IMPORT
#include <PNGdecoder/PNGdecoder.h>




//  Useful shorthands
#define G8      uint8_t
#define G16     uint16_t
#define G8A     PNGdecoder_grayscale8a_t
#define G16A    PNGdecoder_grayscale16a_t
#define RGB8    PNGdecoder_RGB8_t
#define RGB16   PNGdecoder_RGB16_t
#define RGBA8   PNGdecoder_RGBA8_t
#define RGBA16  PNGdecoder_RGBA16_t

#define RASTER_G8 PNGdecoder_raster_grayscale8_t
#define RASTER_G16 PNGdecoder_raster_grayscale16_t

#define RASTER_RGB8 PNGdecoder_raster_RGB8_t
#define RASTER_RGB16 PNGdecoder_raster_RGB16_t

#define RASTER_G8A PNGdecoder_raster_grayscale8a_t
#define RASTER_G16A PNGdecoder_raster_grayscale16a_t

#define RASTER_RGBA8 PNGdecoder_raster_RGBA8_t
#define RASTER_RGBA16 PNGdecoder_raster_RGBA16_t



/*      COMMON DECLARATIONS/DEFINITIONS         */


typedef enum {
    CHUNK_ANCILLARY,
    CHUNK_PRIVATE,
    CHUNK_RESERVED,
    CHUNK_SAFE_TO_COPY
} property_bits;    //Chunk properties bits

typedef enum {
    tRNS_INDEXED,
    tRNS_GRAYSCALE, //To implement
    tRNS_TRUECOLOR  //Idem
} tRNS_types;       //Simple transparency chunk types

typedef struct _chunk {
    uint32_t length;
    uint8_t type[4];
    bool properties[4];     //See above
    uint8_t * data;         //Raw data
    uint32_t CRC_data;      //CRC in the data
    uint32_t CRC_computed;  //CRC computed from the data aka type + data
} chunk;            //Generic chunk

typedef struct _chunk_IHDR {
    uint32_t width;
    uint32_t height;
    uint8_t bit_depth;              //1, 2, 4, 8, 16
    uint8_t color_type;             //0, 2, 3, 4, 6
    uint8_t compression_method;     //Only 0
    uint8_t filter_method;          //Only 0
    uint8_t interlace_method;       //0, 1(Adam7)
} chunk_IHDR;               //Critical IHDR chunk, contains basic information about the PNG

typedef struct _chunk_PLTE {
    uint16_t entries_n;
    uint8_t * entries;  //3 bytes each, (R, G, B)
} chunk_PLTE;           //Ancillary chunk except for color type 3, contains palette entries

typedef struct _chunk_tRNS {
    tRNS_types type;
    uint16_t entries_n;
    uint8_t * entries;
} chunk_tRNS;           //Simple transparency chunk, ancillary

typedef struct _PNGdecoder_PNG{
    uint32_t file_size;
    uint8_t * raw_file;

    uint8_t chunk_n;
    chunk ** chunks;
    chunk_IHDR * IHDR;

    uint8_t * rawIDATs;
    uint32_t rawIDATs_size;

    //uint8_t * pixel_data;
    //uint32_t pixel_data_size;

    chunk_PLTE * PLTE;
    chunk_tRNS * tRNS;

    void * raster_struct;
    void * raster;
    PNGdecoder_raster_types raster_type;
} PNGdecoder_PNG;           //Main type for this module, contains all necessary information to produce a raster


/*      LIBPNG UTILS        */

//Standard routines to produce CRC code from the given vectors
extern unsigned long update_crc(unsigned long, unsigned char *, int);
extern unsigned long crc(unsigned char *, int);

//Filter type 04
extern uint8_t paeth_predictor(uint8_t, uint8_t, uint8_t);


/*      PRIVATE DECLARATIONS/DEFINITIONS        */


static const uint8_t PNG_magic[8] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};   //Must have initial 8 bytes
static const uint8_t chunk_block_size = 5;      //Number of initial chunks allocated, total is unknown, more are allocated as necessary
static const char * chunk_types_essential[4] = {
    "IHDR",
    "PLTE",
    "IDAT",
    "IEND"
};          //Must have chunks, PLTE only for color type 3
static const char * chunk_types_ancillary[1] = {
    "tRNS"
};          //Ancillary chunks handled by the module

static const char * result_strings[12] = {
    "Consistent PNG",
    "Invalid argument",
    "Error opening file",
    "Bad PNG signature",
    "CRC different from file CRC",
    "Missing critical IHDR chunk",
    "Missing critical PLTE chunk",
    "Missing critical IDAT chunk",
    "Missing critical IEND chunk",
    "Invalid IHDR",
    "Invalid PLTE",
    "ZLib deflate error"
};  //Human readable error strings

static const uint8_t Adam7[7*4] = {     //Offset x, offset y, step x, step y
    0, 0, 8, 8,
    4, 0, 8, 8,
    0, 4, 4, 8,
    2, 0, 4, 4,
    0, 2, 2, 4,
    1, 0, 2, 2,
    0, 1, 1, 2
};              //Handles Adam7 interlacing, each row is a step 1 to 7 containing the necessary information


/*      PRIVATE FUNCTIONS DECLARATIONS        */

//Swaps big and little endian 4 byte string, returns as unsigned integer
static uint32_t swapped_uint32(uint8_t *);

//Checks whether a PLTE chunk is present, if so, allocate proper structure: required or color type 3, hence
//return value informs the caller
static PNGdecoder_result check_chunk_PLTE(PNGdecoder_PNG *);

//Checks whether a tRNS chunk is present, if so, allocate proper structure
static void check_chunk_tRNS(PNGdecoder_PNG *);

//Checks the consistency of the png, such as the presence of critical chunks and their consistency, CRC value; for
//IHDR chunk checks PNG type consistency, such as bit depth, color type, etc....
static PNGdecoder_result check_consistency(PNGdecoder_PNG *);

//Calls functions dealing with the ancillary chunks supported by the module
static void check_ancillary_chunks(PNGdecoder_PNG *);

//Frees the structures and vectors allocated for each chunk detected and handled by the module
//Argument 1: chunks pointer, Argument 2: number of chunks
static void free_chunks(chunk **, uint16_t);

//Allocates a new chunk structure from the data pointed by the first argument(expects a 4 byte integer with the chunk length)
static chunk * new_chunk(uint8_t *);

//Allocates and handles critical IHDR chunk from a raw chunk with IHDR signature
static chunk_IHDR * new_chunk_IHDR(chunk *);

//Takes a list of chunks and concatenates IDAT type chunks into a new vector, whose final size is written
//in the uint32_t pointer provided by the caller; returns concatenated vector
//Argument 1: chunks pointer, Argument 2: number of chunks, Argument 3: pointer to integer into which final size is written
static uint8_t * concatenateIDAT(chunk **, uint16_t, uint32_t *);

//Handles ZLib inflation algorithm, takes a vector of compressed data and its size as input(first 2 arguments)
// and returns the inflated data of size provided by the caller as 4th argument
static int8_t zlib_inflate(uint8_t *, uint32_t, uint8_t *, uint32_t);


//Computes and returns the number of bytes required to store a series of lines each containing a filter byte plus a multiple
//of the given pixel size in bits; required for bit depths not divisible by 8
//Argument 1: pixel size in bits, Argument 2: number of columns in each line(minus the filter byte), Argument 3: number of lines
//Example: to store 3 pixels of 4 bits each over 2 lines we need 3*4 = 12 bits per line, padded to 16 bits aka 2 bytes
//plus 1 filter byte per row, hence (2 + 1)*2 = 6 bytes total
static uint32_t padded_size(uint8_t, uint32_t, uint32_t);

//Computes the size of the byte array required to store the inflated IDAT data, similar to the above padded_size function
//except it handles the more complex interlaced case requiring variable sizes for each step depending on the size of the input image
//Arguments: same as above plus one byte indicating whether the image is interlaced(1) or not(0)
static uint32_t calc_decoded_size(uint8_t, uint32_t, uint32_t, uint8_t);

//Computes the vector "coordinates" of the given Adam7 step row and column in the image of given width and height
//Argument 1: image/raster width, Argument 2: image/raster height, Argument 3: Adam7 step, Argument 4: pixel number for given line
//Argument 5: line number; note: step is in [1, 7], but column and raw start from 0
//Example: given an image of 11x9 pixels, for step 3 of Adam7, the first line, third pixel value will be the 9-th column,
//5-th row pixel, hence (starting from 0)(4 * 11) + 8 will be returned
static uint32_t adam7_to_raster(uint32_t, uint32_t, uint8_t, uint32_t, uint32_t);

//Performs the required byte operation on the given byte specified by the given filter
//Argument 1: filter byte, 0 to 4, Argument 2: byte to unfilter, Argument 3: unfiltered byte to the left of the given byte
//to filter, left by as many bytes as the pixel size e.g for 8 bit RGB, this will be the byte 3 steps before,
//Argument 4: unfiltered byte from the previous row, Argument 5: same as 3 except from the previous row
//Filter byte types: 0(no filter operation), 1(raw + left), 2(raw + up), 3(raw + floor(mean(left + up))), 4(raw + Paeth function)
static uint8_t unfilter(uint8_t, uint8_t, uint16_t, uint16_t, uint16_t);

//Converts the concatenated IDAT data from the given png into the appropriate raster
static PNGdecoder_raster_types IDATs_to_raster(PNGdecoder_PNG *);

//Functions to free allocated resources, used by PNGdecoder_free
static void free_chunk_PLTE(chunk_PLTE *);
static void free_chunk_tRNS(chunk_tRNS *);
static void free_raster(void *, void *);


/*      PUBLIC INTERFACE        */


PNGdecoder_result PNGdecoder_openPNG(const char * file_name, PNGdecoder_PNG ** result){
    uint32_t i;

    if(file_name == NULL)
        return PNGDECODER_INVALID_ARGUMENT;

    FILE * png_file = fopen(file_name, "r");
    if(png_file == NULL)
        return PNGDECODER_FILE_OPEN_ERROR;

    uint32_t file_size = 0;
    fseek(png_file, 0, SEEK_END);
    file_size = ftell(png_file);
    fseek(png_file, 0, SEEK_SET);

    uint8_t * bytes = calloc(file_size, 1);
    for(i = 0; i < file_size; i++)
        bytes[i] = fgetc(png_file);
    fclose(png_file);

    uint8_t * current_byte = bytes;
    if(memcmp(current_byte, PNG_magic, 8)){
        free(bytes);
        return PNGDECODER_BAD_PNG;
    }

    current_byte += 8;

    chunk ** chunks = (chunk **)malloc(sizeof(chunk *)*chunk_block_size);
    uint16_t chunk_n = 0;
    do{
        if(((chunk_n % chunk_block_size) == 0) && (chunk_n != 0))
            chunks = (chunk **)realloc(chunks, sizeof(chunk *) * (chunk_n + chunk_block_size));

        chunks[chunk_n] = new_chunk(current_byte);
        current_byte += (12 + chunks[chunk_n++]->length); //length + type + CRC + length
    }while((current_byte - bytes) < file_size);

    if(memcmp(chunks[0]->type, chunk_types_essential[0], 4)){
        free_chunks(chunks, chunk_n);
        free(bytes);
        return PNGDECODER_MISSING_IHDR;
    }
    if(memcmp(chunks[chunk_n - 1]->type, chunk_types_essential[3], 4)){
        free_chunks(chunks, chunk_n);
        free(bytes);
        return PNGDECODER_MISSING_IEND;
    }

    chunk_IHDR * IHDR = new_chunk_IHDR(chunks[0]);

    PNGdecoder_PNG * png = (PNGdecoder_PNG *) malloc(sizeof(PNGdecoder_PNG));
    png->file_size = file_size;
    png->raw_file = bytes;
    png->chunk_n = chunk_n;
    png->chunks = chunks;
    png->IHDR = IHDR;
    png->rawIDATs = NULL;
    png->rawIDATs_size = 0;
    //png->pixel_data = NULL;
    //png->pixel_data_size = 0;
    png->PLTE = NULL;
    png->tRNS = NULL;
    png->raster_struct = NULL;
    png->raster = NULL;
    png->raster_type = PNGDECODER_RASTER_INVALID;

    PNGdecoder_result consistent = check_consistency(png);
    if(consistent != PNGDECODER_OK){
        PNGdecoder_free(png);
        return consistent;
    }

    check_ancillary_chunks(png);
    png->rawIDATs = concatenateIDAT(chunks, chunk_n, &png->rawIDATs_size);
    png->raster_type = IDATs_to_raster(png);
    //IDATs_to_pixel_data(png);
    //png->raster_type = call_raster_method(png);

    *result = png;
    return PNGDECODER_OK;
}

void PNGdecoder_free(PNGdecoder_PNG * png){
    if(png == NULL)
        return;

    if(png->raw_file != NULL)
        free(png->raw_file);
    if(png->chunks != NULL)
        free_chunks(png->chunks, png->chunk_n);
    if(png->IHDR != NULL)
        free(png->IHDR);
    if(png->rawIDATs != NULL)
        free(png->rawIDATs);
    //if(png->pixel_data != NULL)
        //free(png->pixel_data);
    if(png->PLTE != NULL)
        free_chunk_PLTE(png->PLTE);
    if(png->tRNS != NULL)
        free_chunk_tRNS(png->tRNS);
    if(png->raster_struct != NULL)
        free_raster(png->raster_struct, png->raster);
}

const char * PNGdecoder_strerror(PNGdecoder_result result){
    if((result >= 0) && (result < PNGDECODER_RESULTS_COUNT))
        return result_strings[result];

    return NULL;
}

PNGdecoder_raster_types PNGdecoder_get_raster_type(PNGdecoder_PNG * png){
    if(png != NULL)
        return png->raster_type;

    return PNGDECODER_RASTER_INVALID;
}

const void * PNGdecoder_get_raster(PNGdecoder_PNG * png){
    if(png != NULL)
        return png->raster_struct;

    return NULL;
}

const uint8_t PNGdecoder_get_depth(PNGdecoder_PNG * png){
    if(png != NULL)
        return png->IHDR->bit_depth;

    return 0;
}

const uint32_t PNGdecoder_get_width(PNGdecoder_PNG * png){
    if(png != NULL)
        return png->IHDR->width;

    return 0;
}

const uint32_t PNGdecoder_get_height(PNGdecoder_PNG * png){
    if(png != NULL)
        return png->IHDR->height;

    return 0;
}

PNGdecoder_raster_RGBA8_t * PNGdecoder_as_RGBA8(PNGdecoder_PNG * png){
    if(png == NULL)
        return NULL;

    uint32_t i, j;
    uint8_t level, alpha;

    if(png->IHDR->bit_depth > 8)
        return NULL;

    PNGdecoder_raster_RGBA8_t * raster_rgba8 = (PNGdecoder_raster_RGBA8_t *) malloc(sizeof(PNGdecoder_raster_RGBA8_t));

    raster_rgba8->width = png->IHDR->width;
    raster_rgba8->height = png->IHDR->height;
    raster_rgba8->raster = (PNGdecoder_RGBA8_t *) calloc(raster_rgba8->width * raster_rgba8->height, sizeof(PNGdecoder_RGBA8_t));

    for(i = 0; i < raster_rgba8->height; i++)
        for(j = 0; j < raster_rgba8->width; j++){
            switch(png->raster_type){
                case PNGDECODER_RASTER_GRAYSCALE_8:
                    level = ((uint8_t *) png->raster)[(i * raster_rgba8->width) + j];
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].R = level;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].G = level;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].B = level;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].A = 0xFF;
                    break;
                case PNGDECODER_RASTER_GRAYSCALE_8A:
                    level = ((PNGdecoder_grayscale8a_t *) png->raster)[(i * raster_rgba8->width) + j].level;
                    alpha = ((PNGdecoder_grayscale8a_t *) png->raster)[(i * raster_rgba8->width) + j].alpha;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].R = level;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].G = level;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].B = level;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].A = alpha;
                    break;
                case PNGDECODER_RASTER_RGB_8:
                    level = ((PNGdecoder_RGB8_t *) png->raster)[(i * raster_rgba8->width) + j].R;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].R = level;
                    level = ((PNGdecoder_RGB8_t *) png->raster)[(i * raster_rgba8->width) + j].G;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].G = level;
                    level = ((PNGdecoder_RGB8_t *) png->raster)[(i * raster_rgba8->width) + j].B;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].B = level;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].A = 0xFF;
                    break;
                case PNGDECODER_RASTER_RGBA_8:
                    level = ((PNGdecoder_RGBA8_t *) png->raster)[(i * raster_rgba8->width) + j].R;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].R = level;
                    level = ((PNGdecoder_RGBA8_t *) png->raster)[(i * raster_rgba8->width) + j].G;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].G = level;
                    level = ((PNGdecoder_RGBA8_t *) png->raster)[(i * raster_rgba8->width) + j].B;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].B = level;
                    alpha = ((PNGdecoder_RGBA8_t *) png->raster)[(i * raster_rgba8->width) + j].A;
                    raster_rgba8->raster[(i * raster_rgba8->width) + j].A = alpha;
                    break;
                default:
                    break;
            }
        }

    return raster_rgba8;
}

PNGdecoder_raster_RGBA16_t * PNGdecoder_as_RGBA16(PNGdecoder_PNG * png){
    if(png == NULL)
        return NULL;

    uint32_t i, j;
    uint16_t level, alpha;

    if(png->IHDR->bit_depth <= 8)
        return NULL;

    PNGdecoder_raster_RGBA16_t * raster_rgba16 = (PNGdecoder_raster_RGBA16_t *) malloc(sizeof(PNGdecoder_raster_RGBA16_t));

    raster_rgba16->width = png->IHDR->width;
    raster_rgba16->height = png->IHDR->height;
    raster_rgba16->raster = (PNGdecoder_RGBA16_t *) calloc(raster_rgba16->width * raster_rgba16->height, sizeof(PNGdecoder_RGBA16_t));

    for(i = 0; i < raster_rgba16->height; i++)
        for(j = 0; j < raster_rgba16->width; j++){
            switch(png->raster_type){
                case PNGDECODER_RASTER_GRAYSCALE_16:
                    level = ((uint16_t *) png->raster)[(i * raster_rgba16->width) + j];
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].R = level;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].G = level;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].B = level;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].A = 0xFFFF;
                    break;
                case PNGDECODER_RASTER_GRAYSCALE_16A:
                    level = ((PNGdecoder_grayscale16a_t *) png->raster)[(i * raster_rgba16->width) + j].level;
                    alpha = ((PNGdecoder_grayscale16a_t *) png->raster)[(i * raster_rgba16->width) + j].alpha;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].R = level;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].G = level;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].B = level;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].A = alpha;
                    break;
                case PNGDECODER_RASTER_RGB_16:
                    level = ((PNGdecoder_RGB16_t *) png->raster)[(i * raster_rgba16->width) + j].R;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].R = level;
                    level = ((PNGdecoder_RGB16_t *) png->raster)[(i * raster_rgba16->width) + j].G;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].G = level;
                    level = ((PNGdecoder_RGB16_t *) png->raster)[(i * raster_rgba16->width) + j].B;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].B = level;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].A = 0xFFFF;
                    break;
                case PNGDECODER_RASTER_RGBA_16:
                    level = ((PNGdecoder_RGBA16_t *) png->raster)[(i * raster_rgba16->width) + j].R;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].R = level;
                    level = ((PNGdecoder_RGBA16_t *) png->raster)[(i * raster_rgba16->width) + j].G;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].G = level;
                    level = ((PNGdecoder_RGBA16_t *) png->raster)[(i * raster_rgba16->width) + j].B;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].B = level;
                    alpha = ((PNGdecoder_RGBA16_t *) png->raster)[(i * raster_rgba16->width) + j].A;
                    raster_rgba16->raster[(i * raster_rgba16->width) + j].A = alpha;
                    break;
                default:
                    break;
            }
        }

    return raster_rgba16;
}

void PNGdecoder_raster_free(void * raster_struct, PNGdecoder_raster_types type){
    if(raster_struct == NULL) return;

    if(type == PNGDECODER_RASTER_RGBA_8)
        free(((PNGdecoder_raster_RGBA8_t *)raster_struct)->raster);
    else if(type == PNGDECODER_RASTER_RGBA_16)
        free(((PNGdecoder_raster_RGBA16_t *)raster_struct)->raster);

    free(raster_struct);
    return;
}



/*      PRIVATE FUNCTIONS IMPLEMENTATION        */


static uint32_t swapped_uint32(uint8_t * pointer){
    uint32_t raw = *((uint32_t *)pointer);
    return ((raw>>24)&0xff) | ((raw<<8)&0xff0000) | ((raw>>8)&0xff00) | ((raw<<24)&0xff000000);
}

static PNGdecoder_result check_chunk_PLTE(PNGdecoder_PNG * png){
    uint16_t i;
    chunk * rawPLTE = NULL;
    for(i = 0; i < png->chunk_n; i++)
        if(!memcmp(png->chunks[i]->type, chunk_types_essential[1], 4)) rawPLTE = png->chunks[i];

    if(rawPLTE != NULL){
        if(rawPLTE->length % 3)
            return PNGDECODER_INVALID_PLTE;

        uint16_t entries_n = rawPLTE->length / 3;
        if(entries_n > (2 << (png->IHDR->bit_depth - 1)))
            return PNGDECODER_INVALID_PLTE;

        chunk_PLTE * PLTE = (chunk_PLTE *) malloc(sizeof(chunk_PLTE));
        PLTE->entries_n = entries_n;
        PLTE->entries = (uint8_t *) calloc(entries_n * 3, sizeof(uint8_t));
        for(i = 0; i < entries_n; i++)
            memcpy(&PLTE->entries[i*3], &rawPLTE->data[i*3], 3);
        png->PLTE = PLTE;

        return PNGDECODER_OK;
    }
    return PNGDECODER_MISSING_PLTE;
}

static void check_chunk_tRNS(PNGdecoder_PNG * png){
    uint16_t i;
    chunk * raw_tRNS = NULL;
    chunk_PLTE * PLTE = NULL;
    chunk_tRNS * tRNS = NULL;

    for(i = 0; i < png->chunk_n; i++)
        if(!memcmp(png->chunks[i]->type, chunk_types_ancillary[0], 4)) raw_tRNS = png->chunks[i];

    if(raw_tRNS != NULL){
        switch(png->IHDR->color_type){
            case 0:
                break;
            case 2:
                break;
            case 3:
                PLTE = png->PLTE;
                if(PLTE == NULL) return;

                uint16_t entries_n = raw_tRNS->length;
                if(entries_n > PLTE->entries_n) return;

                tRNS = (chunk_tRNS *) malloc(sizeof(chunk_tRNS));
                tRNS->type = tRNS_INDEXED;
                tRNS->entries_n = entries_n;
                tRNS->entries = (uint8_t *) calloc(entries_n, sizeof(uint8_t));
                memcpy(tRNS->entries, raw_tRNS->data, entries_n);

                png->tRNS = tRNS;
                return;
                break;
        }
    }
    return;
}

static PNGdecoder_result check_consistency(PNGdecoder_PNG * png){
    uint8_t i;
    for(i = 0; i < png->chunk_n; i++)
        if(png->chunks[i]->CRC_data != png->chunks[i]->CRC_computed) return PNGDECODER_MISMATCHING_CRC;

    uint32_t IDAT_count = 0;
    for(i = 0; i < png->chunk_n; i++)
        if(memcmp(png->chunks[i]->type, chunk_types_essential[2], 4)) IDAT_count++;

    if(IDAT_count == 0) return PNGDECODER_MISSING_IDAT;
    if((png->IHDR->width == 0) || (png->IHDR->height == 0)) return PNGDECODER_INVALID_IHDR;

    if(png->IHDR->compression_method != 0) return PNGDECODER_INVALID_IHDR;
    if(png->IHDR->filter_method != 0) return PNGDECODER_INVALID_IHDR;
    if((png->IHDR->interlace_method != 0) && (png->IHDR->interlace_method != 1)) return PNGDECODER_INVALID_IHDR;

    uint8_t ctype = png->IHDR->color_type;
    uint8_t bit_depth = png->IHDR->bit_depth;
    switch(ctype){
        case 0:
            if((bit_depth != 1) && (bit_depth != 2) && (bit_depth != 4) && (bit_depth != 8) && (bit_depth != 16))
                return PNGDECODER_INVALID_IHDR;
            break;
        case 2:
            if((bit_depth != 8) && (bit_depth != 16))
                return PNGDECODER_INVALID_IHDR;
            break;
        case 3:
            if((bit_depth != 1) && (bit_depth != 2) && (bit_depth != 4) && (bit_depth != 8))
                return PNGDECODER_INVALID_IHDR;
            return check_chunk_PLTE(png);
            break;
        case 4:
            if((bit_depth != 8) && (bit_depth != 16))
                return PNGDECODER_INVALID_IHDR;
            break;
        case 6:
            if((bit_depth != 8) && (bit_depth != 16))
                return PNGDECODER_INVALID_IHDR;
            break;
        default:
            return PNGDECODER_INVALID_IHDR;
    }

    return PNGDECODER_OK;
}

static void check_ancillary_chunks(PNGdecoder_PNG * png){
    check_chunk_tRNS(png);
    return;
}

static void free_chunks(chunk ** chunks, uint16_t chunk_n){
    uint8_t i;
    if(chunks != NULL){
        for(i = 0; i < chunk_n; i++)
            if(chunks[i] != NULL)
                free(chunks[i]);

        free(chunks);
    }
    return;
}

static chunk * new_chunk(uint8_t * data){
    chunk * c = (chunk *) calloc(1, sizeof(chunk));
    c->length = swapped_uint32(data);
    memcpy(c->type, &data[4], 4);
    c->properties[CHUNK_ANCILLARY] = (c->type[0] & 0x20) > 0;
    c->properties[CHUNK_PRIVATE] = (c->type[1] & 0x20) > 0;
    c->properties[CHUNK_RESERVED] = (c->type[2] & 0x20) > 0;
    c->properties[CHUNK_SAFE_TO_COPY] = (c->type[3] & 0x20) > 0;
    c->data = &data[8];
    c->CRC_data = *((uint32_t *) (c->data + c->length));

    uint32_t CRC_raw = crc((unsigned char *)&data[4], c->length + 4);
    c->CRC_computed = swapped_uint32((uint8_t *)&CRC_raw);

    return c;
}

static chunk_IHDR * new_chunk_IHDR(chunk * chunk) {
    chunk_IHDR * cIHDR = (chunk_IHDR *) calloc(1, sizeof(chunk_IHDR));

    cIHDR->width = swapped_uint32(&chunk->data[0]);
    cIHDR->height = swapped_uint32(&chunk->data[4]);
    cIHDR->bit_depth = chunk->data[8];
    cIHDR->color_type = chunk->data[9];
    cIHDR->compression_method = chunk->data[10];
    cIHDR->filter_method = chunk->data[11];
    cIHDR->interlace_method = chunk->data[12];

    return cIHDR;
}

static uint8_t * concatenateIDAT(chunk ** chunks, uint16_t chunk_n, uint32_t * final_size){
    uint16_t i;
    uint8_t * IDATdata = NULL;
    uint32_t tot_size = 0;
    for(i = 0; i < chunk_n; i++){
        if(!memcmp(chunks[i]->type, chunk_types_essential[2], 4))
            tot_size += chunks[i]->length;
    }
    *final_size = tot_size;

    IDATdata = (uint8_t *) malloc(tot_size);
    uint8_t * current_pos = IDATdata;
    for(i = 0; i < chunk_n; i++){
        if(!memcmp(chunks[i]->type, chunk_types_essential[2], 4)){
            memcpy(current_pos, chunks[i]->data, chunks[i]->length);
            current_pos += chunks[i]->length;
        }
    }

    return IDATdata;
}

static int8_t zlib_inflate(uint8_t * in, uint32_t in_size, uint8_t * out, uint32_t out_size){
    z_stream infstream;

    infstream.zalloc = Z_NULL;
    infstream.zfree = Z_NULL;
    infstream.opaque = Z_NULL;
    infstream.avail_in = in_size;
    infstream.next_in = in;
    infstream.avail_out = out_size;
    infstream.next_out = out;

    inflateInit(&infstream);
    int8_t result = inflate(&infstream, Z_NO_FLUSH);
    inflateEnd(&infstream);

    return result;
}

static uint32_t padded_size(uint8_t pixel_bitsize, uint32_t ncols, uint32_t nrows){
    uint32_t row_bitsize_raw = 0;
    uint32_t row_bitsize = 0;
    uint32_t row_size = 0;

    row_bitsize_raw = ncols * pixel_bitsize;
    row_bitsize = row_bitsize_raw + (((row_bitsize_raw % 8) > 0) ? 8 - (row_bitsize_raw % 8) : 0);
    row_size = row_bitsize / 8;

    return ((nrows * row_size) + nrows);
}

static uint32_t calc_decoded_size(uint8_t pixel_bitsize, uint32_t width, uint32_t height, uint8_t interlaced){
    uint32_t i, j, step;
    uint32_t size = 0;

    if(!interlaced){
        size = padded_size(pixel_bitsize, width, height);
    } else {
        for(step = 0; step < 7; step++){
            i = Adam7[(step * 4)]; while(i < width) i+=Adam7[(step * 4) + 2]; i = (i - Adam7[(step * 4)]) / Adam7[(step * 4) + 2];
            j = Adam7[(step * 4) + 1]; while(j < height) j+=Adam7[(step * 4) + 3]; j = (j - Adam7[(step * 4) + 1]) / Adam7[(step * 4) + 3];
            if((i*j) != 0)
                size += padded_size(pixel_bitsize, i, j);
        }
    }

    return size;
}

static uint32_t adam7_to_raster(uint32_t raster_width, uint32_t raster_height, uint8_t step, uint32_t a7_col, uint32_t a7_row){
    uint8_t step_adj = step - 1;    //Step in [1, 7]
    uint32_t i, j;

    i = Adam7[(step_adj * 4)] + (a7_col * Adam7[(step_adj * 4) + 2]);
    j = Adam7[(step_adj * 4) + 1] + (a7_row * Adam7[(step_adj * 4) + 3]);

    return (raster_width * j) + i;
}

static uint8_t unfilter(uint8_t OP, uint8_t byte, uint16_t left, uint16_t up, uint16_t left_up){    //16 bits for operations
    switch(OP){
        case 0:
            return byte;
        case 1:
            return byte + left;
        case 2:
            return byte + up;
        case 3:
            return byte + (uint8_t) floor((left + up)/2);
        case 4:
            return byte + paeth_predictor(left, up, left_up);
    }
    return 0;
}

static PNGdecoder_raster_types IDATs_to_raster(PNGdecoder_PNG * png){
    chunk_IHDR * IHDR = png->IHDR;
    chunk_tRNS * tRNS = png->tRNS;
    chunk_PLTE * PLTE = png->PLTE;

    bool simple_transparency = false;
    if(tRNS != NULL)
        if(tRNS->type == tRNS_INDEXED)
            simple_transparency = true;

    uint8_t color_type = IHDR->color_type;
    uint8_t bit_depth = IHDR->bit_depth;
    uint32_t width = IHDR->width;
    uint32_t height = IHDR->height;
    bool interlaced = IHDR->interlace_method;

    uint8_t pixel_bitsize = 0;

    uint8_t * rawIDATs = png->rawIDATs;
    uint32_t rawIDATs_size = png->rawIDATs_size;

    uint8_t * decoded = NULL;
    uint32_t decoded_size = 0;

    //Raster choice
    G8 * raster_g8 = NULL;
    G16 * raster_g16 = NULL;
    G8A * raster_g8a = NULL;
    G16A * raster_g16a = NULL;
    RGB8 * raster_rgb8 = NULL;
    RGB16 * raster_rgb16 = NULL;
    RGBA8 * raster_rgba8 = NULL;
    RGBA16 * raster_rgba16 = NULL;
    void * raster = NULL;
    PNGdecoder_raster_types raster_type = PNGDECODER_RASTER_INVALID;
    void * raster_struct = NULL;

    switch(color_type){
        case 0:
            pixel_bitsize = bit_depth;
            raster = calloc(height * width, (bit_depth < 16) ? sizeof(G8) : sizeof(G16));
            raster_type = (bit_depth < 16) ? PNGDECODER_RASTER_GRAYSCALE_8 : PNGDECODER_RASTER_GRAYSCALE_16;

            raster_struct = malloc((bit_depth < 16) ? sizeof(RASTER_G8) : sizeof(RASTER_G16));
            if(bit_depth < 16){
                raster_g8 = (G8 *) raster;
                ((RASTER_G8 *)raster_struct)->width = width;
                ((RASTER_G8 *)raster_struct)->height = height;
                ((RASTER_G8 *)raster_struct)->raster = raster;
            } else {
                raster_g16 = (G16 *) raster;
                ((RASTER_G16 *)raster_struct)->width = width;
                ((RASTER_G16 *)raster_struct)->height = height;
                ((RASTER_G16 *)raster_struct)->raster = raster;
            }

            break;
        case 2:
            pixel_bitsize = bit_depth * 3;
            raster = calloc(height * width, (bit_depth < 16) ? sizeof(RGB8) : sizeof(RGB16));
            raster_type = (bit_depth < 16) ? PNGDECODER_RASTER_RGB_8 : PNGDECODER_RASTER_RGB_16;

            raster_struct = malloc((bit_depth < 16) ? sizeof(RASTER_RGB8) : sizeof(RASTER_RGB16));
            if(bit_depth < 16){
                raster_rgb8 = (RGB8 *) raster;
                ((RASTER_RGB8 *)raster_struct)->width = width;
                ((RASTER_RGB8 *)raster_struct)->height = height;
                ((RASTER_RGB8 *)raster_struct)->raster = raster;
            } else {
                raster_rgb16 = (RGB16 *) raster;
                ((RASTER_RGB16 *)raster_struct)->width = width;
                ((RASTER_RGB16 *)raster_struct)->height = height;
                ((RASTER_RGB16 *)raster_struct)->raster = raster;
            }

            break;
        case 3:
            pixel_bitsize = bit_depth;
            raster = calloc(height * width, (!simple_transparency) ? sizeof(RGB8) : sizeof(RGBA8));
            raster_type = (!simple_transparency) ? PNGDECODER_RASTER_RGB_8 : PNGDECODER_RASTER_RGBA_8;

            raster_struct = malloc((!simple_transparency) ? sizeof(RASTER_RGB8) : sizeof(RASTER_RGBA8));
            if(!simple_transparency){
                raster_rgb8 = (RGB8 *) raster;
                ((RASTER_RGB8 *)raster_struct)->width = width;
                ((RASTER_RGB8 *)raster_struct)->height = height;
                ((RASTER_RGB8 *)raster_struct)->raster = raster;
            } else {
                raster_rgba8 = (RGBA8 *) raster;
                ((RASTER_RGBA8 *)raster_struct)->width = width;
                ((RASTER_RGBA8 *)raster_struct)->height = height;
                ((RASTER_RGBA8 *)raster_struct)->raster = raster;
            }

            break;
        case 4:
            pixel_bitsize = bit_depth * 2;
            raster = calloc(height * width, (bit_depth < 16) ? sizeof(G8A) : sizeof(G16A));
            raster_type = (bit_depth < 16) ? PNGDECODER_RASTER_GRAYSCALE_8A : PNGDECODER_RASTER_GRAYSCALE_16A;

            raster_struct = malloc((bit_depth < 16) ? sizeof(RASTER_G8A) : sizeof(RASTER_G16A));
            if(bit_depth < 16){
                raster_g8a = (G8A *) raster;
                ((RASTER_G8A *)raster_struct)->width = width;
                ((RASTER_G8A *)raster_struct)->height = height;
                ((RASTER_G8A *)raster_struct)->raster = raster;
            } else {
                raster_g16a = (G16A *) raster;
                ((RASTER_G16A *)raster_struct)->width = width;
                ((RASTER_G16A *)raster_struct)->height = height;
                ((RASTER_G16A *)raster_struct)->raster = raster;
            }

            break;
        case 6:
            pixel_bitsize = bit_depth * 4;
            raster = calloc(height * width, (bit_depth < 16) ? sizeof(RGBA8) : sizeof(RGBA16));
            raster_type = (bit_depth < 16) ? PNGDECODER_RASTER_RGBA_8 : PNGDECODER_RASTER_RGBA_16;

            raster_struct = malloc((bit_depth < 16) ? sizeof(RASTER_RGBA8) : sizeof(RASTER_RGBA16));
            if(bit_depth < 16){
                raster_rgba8 = (RGBA8 *) raster;
                ((RASTER_RGBA8 *)raster_struct)->width = width;
                ((RASTER_RGBA8 *)raster_struct)->height = height;
                ((RASTER_RGBA8 *)raster_struct)->raster = raster;
            } else {
                raster_rgba16 = (RGBA16 *) raster;
                ((RASTER_RGBA16 *)raster_struct)->width = width;
                ((RASTER_RGBA16 *)raster_struct)->height = height;
                ((RASTER_RGBA16 *)raster_struct)->raster = raster;
            }

            break;
    }

    decoded_size = calc_decoded_size(pixel_bitsize, width, height, interlaced);
    decoded = (uint8_t *) calloc(decoded_size, sizeof(uint8_t));

    int32_t result = zlib_inflate(rawIDATs, rawIDATs_size, decoded, decoded_size);
    if(result != Z_STREAM_END){
        free(decoded);
        free(raster_struct);
        free(raster);
        return PNGDECODER_RASTER_INVALID;
    }

    uint32_t i = 0;                 //Byte index within decoded
    uint32_t j = 0;                 //Row index between two filter operations
    uint8_t k = 0;                  //Sub-byte index
    uint32_t row_n = 0;             //Absolute row count
    uint32_t col_n = 0;
    uint32_t raster_pos = 0;
    uint32_t row_size = 0;          //Row size, mutable if interlaced
    uint32_t row_size_non_interlaced = padded_size(pixel_bitsize, width, 1) - 1;    //Fixed when not interlaced
    uint8_t OP = 0;                 //Filter operation
    uint8_t current_byte = 0;       //Current byte within decoded, to unfilter
    uint8_t unfiltered_byte = 0;    //Unfiltered
    uint8_t pixel_byte = 0;         //Pixel value of 1 byte
    uint8_t byte_mask = ((1 << bit_depth) - 1) << (8 - bit_depth);  //To extract sub-byte values, not used for depths >= 8
    uint8_t pixel_multiplier = 0xFF / ((1 << bit_depth) - 1);       //Multiplier to scale pixel_bytes of bit_depth < 8 on a [0-255] scale
    uint8_t pixel_bytesize = padded_size(pixel_bitsize, 1, 1) - 1;  //Pixel size in bytes, padded to 1 for sub-byte cases
    uint16_t left, up, left_up;     //Element pixel_bytesize to the left of current, above from previous row, above and left
    left = up = left_up = 0;

    uint8_t * previous_row_buf = (uint8_t *) calloc(row_size_non_interlaced, sizeof(uint8_t));  //Track previous row
    uint8_t * current_row_buf = (uint8_t *) calloc(row_size_non_interlaced, sizeof(uint8_t));   //Track current row


    uint8_t a7_step = 1;                        //Current Adam7 step
    uint32_t a7_ncols, a7_nrows, a7_row_n = 0;  //For current Adam7 step, number of elements in each row, number of rows, relative row number
    uint32_t row_size_interlaced = 0;           //For current Adam7 step, row size in bytes
    a7_ncols = 0; while(a7_ncols < width) a7_ncols+=8; a7_ncols = a7_ncols / 8;
    a7_nrows = 0; while(a7_nrows < height) a7_nrows+=8; a7_nrows = a7_nrows / 8;
    row_size_interlaced = padded_size(pixel_bitsize, a7_ncols, 1) - 1;

    //FILE * f = fopen("log", "w");
    while(i < decoded_size){
        OP = decoded[i]; //fprintf(f, "i: %d, row_n: %d, OP: %d, a7_ncols: %d, a7_nrows: %d\n", i, row_n, OP, a7_ncols, a7_nrows);
        row_size = (!interlaced) ? row_size_non_interlaced : row_size_interlaced;

        for(j = i + 1; j < i + row_size + 1; j++){
            current_byte = decoded[j];

            if((OP == 1) || (OP > 2))
                left = (j >= i + 1 + pixel_bytesize) ? current_row_buf[j - pixel_bytesize - (i + 1)] : 0;
            if(OP >= 2){
                if(!interlaced)
                    up = (row_n > 0) ? previous_row_buf[j - (i + 1)] : 0;
                else
                    up = (a7_row_n > 0) ? previous_row_buf[j - (i + 1)] : 0;
            }
            if(OP == 4){
                if(!interlaced)
                    left_up = (row_n > 0) && (j >= i + 1 + pixel_bytesize) ? previous_row_buf[j - pixel_bytesize - (i + 1)] : 0;
                else
                    left_up = (a7_row_n > 0) && (j >= i + 1 + pixel_bytesize) ? previous_row_buf[j - pixel_bytesize - (i + 1)] : 0;
            }

            unfiltered_byte = unfilter(OP, current_byte, left, up, left_up);
            current_row_buf[j - (i + 1)] = unfiltered_byte;

            //fprintf(f, "decoded[%d]: %d => %d\n", j, current_byte, unfiltered_byte);

            if(bit_depth < 8){
                for(k = 0; k < 8; k+=bit_depth){
                    pixel_byte = (unfiltered_byte & (byte_mask >> k)) >> ((8 - bit_depth) - k);
                    col_n = ((j - (i + 1)) * (8 / bit_depth)) + (k/bit_depth);

                    if((!interlaced) && (col_n >= width)) continue;
                    if(interlaced && (adam7_to_raster(0, 0, a7_step, col_n, a7_row_n) >= width)) continue;

                    if(!interlaced)
                        raster_pos = (row_n * width) + col_n;
                    else
                        raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                    //fprintf(f, "At %d => %d\n", raster_pos, pixel_byte * pixel_multiplier);
                    if(color_type == 0){    //Sub-byte grayscales
                        raster_g8[raster_pos] = pixel_byte * pixel_multiplier;
                    } else {                //Sub-byte indices
                        if(!simple_transparency){
                            raster_rgb8[raster_pos].R = PLTE->entries[(pixel_byte * 3)];
                            raster_rgb8[raster_pos].G = PLTE->entries[(pixel_byte * 3) + 1];
                            raster_rgb8[raster_pos].B = PLTE->entries[(pixel_byte * 3) + 2];
                        } else {
                            raster_rgba8[raster_pos].R = PLTE->entries[(pixel_byte * 3)];
                            raster_rgba8[raster_pos].G = PLTE->entries[(pixel_byte * 3) + 1];
                            raster_rgba8[raster_pos].B = PLTE->entries[(pixel_byte * 3) + 2];
                            if(pixel_byte < tRNS->entries_n)
                                raster_rgba8[raster_pos].A = tRNS->entries[pixel_byte];
                            else
                                raster_rgba8[raster_pos].A = 0xFF;
                        }
                    }
                }
            } else if(bit_depth == 8){
                if((color_type == 0) || (color_type == 3)){
                    col_n = (j - (i + 1));
                    if(!interlaced)
                        raster_pos = (row_n * width) + col_n;
                    else
                        raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                    if(color_type == 0){
                        raster_g8[raster_pos] = unfiltered_byte;
                    } else {
                        if(!simple_transparency){
                            raster_rgb8[raster_pos].R = PLTE->entries[(unfiltered_byte * 3)];
                            raster_rgb8[raster_pos].G = PLTE->entries[(unfiltered_byte * 3) + 1];
                            raster_rgb8[raster_pos].B = PLTE->entries[(unfiltered_byte * 3) + 2];
                        } else {
                            raster_rgba8[raster_pos].R = PLTE->entries[(unfiltered_byte * 3)];
                            raster_rgba8[raster_pos].G = PLTE->entries[(unfiltered_byte * 3) + 1];
                            raster_rgba8[raster_pos].B = PLTE->entries[(unfiltered_byte * 3) + 2];
                            if(unfiltered_byte < tRNS->entries_n)
                                raster_rgba8[raster_pos].A = tRNS->entries[unfiltered_byte];
                            else
                                raster_rgba8[raster_pos].A = 0xFF;
                        }
                    }
                } else if(color_type == 2){
                    col_n = (j - (i + 1)) / 3;
                    if(!interlaced)
                        raster_pos = (row_n * width) + col_n;
                    else
                        raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                    switch((j - (i + 1)) % 3){
                        case 0: raster_rgb8[raster_pos].R = unfiltered_byte; break;
                        case 1: raster_rgb8[raster_pos].G = unfiltered_byte; break;
                        case 2: raster_rgb8[raster_pos].B = unfiltered_byte; break;
                    }
                } else if(color_type == 4){
                    col_n = (j - (i + 1)) / 2;
                    if(!interlaced)
                        raster_pos = (row_n * width) + col_n;
                    else
                        raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                    switch((j - (i + 1)) % 2){
                        case 0: raster_g8a[raster_pos].level = unfiltered_byte; break;
                        case 1: raster_g8a[raster_pos].alpha = unfiltered_byte; break;
                    }
                } else if(color_type == 6){
                    col_n = (j - (i + 1)) / 4;
                    if(!interlaced)
                        raster_pos = (row_n * width) + col_n;
                    else
                        raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                    switch((j - (i + 1)) % 4){
                        case 0: raster_rgba8[raster_pos].R = unfiltered_byte; break;
                        case 1: raster_rgba8[raster_pos].G = unfiltered_byte; break;
                        case 2: raster_rgba8[raster_pos].B = unfiltered_byte; break;
                        case 3: raster_rgba8[raster_pos].A = unfiltered_byte; break;
                    }
                }
            } else if(bit_depth == 16){
                switch(color_type){
                    case 0:
                        col_n = (j - (i + 1)) / 2;
                        if(!interlaced)
                            raster_pos = (row_n * width) + col_n;
                        else
                            raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                        switch((j - (i + 1)) % 2){
                            case 0: raster_g16[raster_pos] = unfiltered_byte * 0x100; break;
                            case 1: raster_g16[raster_pos] += unfiltered_byte; break;
                        }
                        break;
                    case 2:
                        col_n = (j - (i + 1)) / 6;
                        if(!interlaced)
                            raster_pos = (row_n * width) + col_n;
                        else
                            raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                        switch((j - (i + 1)) % 6){
                            case 0: raster_rgb16[raster_pos].R = unfiltered_byte * 0x100; break;
                            case 1: raster_rgb16[raster_pos].R += unfiltered_byte; break;
                            case 2: raster_rgb16[raster_pos].G = unfiltered_byte * 0x100; break;
                            case 3: raster_rgb16[raster_pos].G += unfiltered_byte; break;
                            case 4: raster_rgb16[raster_pos].B = unfiltered_byte * 0x100; break;
                            case 5: raster_rgb16[raster_pos].B += unfiltered_byte; break;
                        }
                        break;
                    case 4:
                        col_n = (j - (i + 1)) / 4;
                        if(!interlaced)
                            raster_pos = (row_n * width) + col_n;
                        else
                            raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                        switch((j - (i + 1)) % 4){
                            case 0: raster_g16a[raster_pos].level = unfiltered_byte * 0x100; break;
                            case 1: raster_g16a[raster_pos].level += unfiltered_byte; break;
                            case 2: raster_g16a[raster_pos].alpha = unfiltered_byte * 0x100; break;
                            case 3: raster_g16a[raster_pos].alpha += unfiltered_byte; break;
                        }
                        break;
                    case 6:
                        col_n = (j - (i + 1)) / 8;
                        if(!interlaced)
                            raster_pos = (row_n * width) + col_n;
                        else
                            raster_pos = adam7_to_raster(width, height, a7_step, col_n, a7_row_n);

                        switch((j - (i + 1)) % 8){
                            case 0: raster_rgba16[raster_pos].R = unfiltered_byte * 0x100; break;
                            case 1: raster_rgba16[raster_pos].R += unfiltered_byte; break;
                            case 2: raster_rgba16[raster_pos].G = unfiltered_byte * 0x100; break;
                            case 3: raster_rgba16[raster_pos].G += unfiltered_byte; break;
                            case 4: raster_rgba16[raster_pos].B = unfiltered_byte * 0x100; break;
                            case 5: raster_rgba16[raster_pos].B += unfiltered_byte; break;
                            case 6: raster_rgba16[raster_pos].A = unfiltered_byte * 0x100; break;
                            case 7: raster_rgba16[raster_pos].A += unfiltered_byte; break;
                        }
                        break;
                }
            }
        }
        for(j = 0; j < row_size; j++) previous_row_buf[j] = current_row_buf[j];

        i += row_size + 1;
        row_n += 1;
        if(interlaced){
            a7_row_n += 1;
            if(a7_row_n >= a7_nrows){
                a7_row_n = 0;

                if(a7_step < 7){
                    do{
                        a7_ncols = Adam7[(a7_step * 4)]; while(a7_ncols < width) a7_ncols+=Adam7[(a7_step * 4) + 2]; a7_ncols = (a7_ncols - Adam7[(a7_step * 4)]) / Adam7[(a7_step * 4) + 2];
                        a7_nrows = Adam7[(a7_step * 4) + 1]; while(a7_nrows < height) a7_nrows+=Adam7[(a7_step * 4) + 3]; a7_nrows = (a7_nrows - Adam7[(a7_step * 4) + 1]) / Adam7[(a7_step * 4) + 3];
                        row_size_interlaced = padded_size(pixel_bitsize, a7_ncols, 1) - 1;
                        a7_step++;
                    }while((a7_ncols*a7_nrows) == 0);
                }
            }
        }
    }
    //fclose(f);
    free(previous_row_buf);
    free(current_row_buf);
    free(decoded);

    png->raster_struct = raster_struct;
    png->raster = raster;

    return raster_type;
}


static void free_chunk_PLTE(chunk_PLTE * PLTE){
    if(PLTE != NULL){
        free(PLTE->entries);
        free(PLTE);
    }
    return;
}

static void free_chunk_tRNS(chunk_tRNS * tRNS){
    if(tRNS != NULL){
        free(tRNS->entries);
        free(tRNS);
    }
    return;
}

static void free_raster(void * raster_struct, void * raster){
    if(raster != NULL)
        free(raster);
    if(raster_struct != NULL)
        free(raster_struct);

    return;
}
