/**
 * @brief  micros and structures for a simple PNG file 
 *
 * Copyright 2018-2020 Yiqing Huang
 *
 * This software may be freely redistributed under the terms of MIT License
 */
#pragma once

/******************************************************************************
 * INCLUDE HEADER FILES
 *****************************************************************************/
#include <stdio.h>

/******************************************************************************
 * DEFINED MACROS 
 *****************************************************************************/

#define PNG_SIG_SIZE    8 /* number of bytes of png image signature data */
#define CHUNK_LEN_SIZE  4 /* chunk length field size in bytes */          
#define CHUNK_TYPE_SIZE 4 /* chunk type field size in bytes */
#define CHUNK_CRC_SIZE  4 /* chunk CRC field size in bytes */
#define DATA_IHDR_SIZE 13 /* IHDR chunk data field size */
#define DATA_IEND_SIZE 0
#define PNGSIG "89 50 4E 47 0D 0A 1A 0A "
#define NUM_U32S_READ 1
#define NUM_U8_IN_U32 4
#define ASCII_NUM_BITS 7

/******************************************************************************
 * STRUCTURES and TYPEDEFS 
 *****************************************************************************/
typedef unsigned char U8;
typedef unsigned short U16;
typedef unsigned int  U32;
typedef unsigned long U64;

typedef struct chunk {
    U32 length;  /* length of data in the chunk, host byte order */
    U8  type[4]; /* chunk type */
    U8  *p_data; /* pointer to location where the actual data are */
    U32 crc;     /* CRC field  */
} *chunk_p;

/* note that there are 13 Bytes valid data, compiler will padd 3 bytes to make
   the structure 16 Bytes due to alignment. So do not use the size of this
   structure as the actual data size, use 13 Bytes (i.e DATA_IHDR_SIZE macro).
 */
typedef struct data_IHDR {// IHDR chunk data 
    U32 width;        /* width in pixels, big endian   */
    U32 height;       /* height in pixels, big endian  */
    U8  bit_depth;    /* num of bits per sample or per palette index.
                         valid values are: 1, 2, 4, 8, 16 */
    U8  color_type;   /* =0: Grayscale; =2: Truecolor; =3 Indexed-color
                         =4: Greyscale with alpha; =6: Truecolor with alpha */
    U8  compression;  /* only method 0 is defined for now */
    U8  filter;       /* only method 0 is defined for now */
    U8  interlace;    /* =0: no interlace; =1: Adam7 interlace */
} *data_IHDR_p;

/* A simple PNG file format, three chunks only*/
typedef struct simple_PNG {
    struct chunk *p_IHDR;
    struct chunk *p_IDAT;  /* only handles one IDAT chunk */  
    struct chunk *p_IEND;
} *simple_PNG_p;

/******************************************************************************
 * FUNCTION PROTOTYPES 
 *****************************************************************************/
void read_bytes(U8 *source, U8 *dest, long *cursor_p, long size);
void write_bytes(U8 *dest, long *cursor_p, U8 *source, long size);
void set_cursor(long* cursor_p, long pos, long offset);
int is_png(U8* fileBytes, long* cursor_p);
int get_png_data_IHDR(struct data_IHDR *out, U8* fileBytes, long* cursor_p);
int parsechunk(struct chunk *ch, U8* fileBytes, long* cursor_p);
int setIDATBuffer(U8 **idatBuffer_p, U64 *idatBufferSize_p, U64 *idatStart_p, U64 *idatEnd_p, U8* fileBytes, long* cursor_p, unsigned long fileSize);
U32 calculate_crc(U8 type[4], U8* defData, long defsize);
int unfilter_data(int bytesPerPixel, const int SCANLINE_LENGTH, int height, U8* inf);
void encodeMessage(U8 **inf, U64 infsize, char *MESSAGE, int bytesPerSample, const int SCANLINE_LENGTH);
void printHex(U8 *buffer, U64 bufferSize, int rowlength);
int decodeMessage(U8 *inf, U64 infsize, const int SCANLINE_LENGTH);