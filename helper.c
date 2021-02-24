#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"

void read_bytes(U8 *source, U8 *dest, long *cursor_p, long size)
{
	//read size bytes from source, write to dest, update cursor position
	for (long i = 0; i < size; i++)
	{
		dest[i] = source[*cursor_p];
		(*cursor_p)++;
	}
}

void write_bytes(U8 *dest, long *cursor_p, U8 *source, long size)
{
	for (long i = 0; i < size; i++)
	{
		dest[*cursor_p] = source[i];
		(*cursor_p)++;
	}
}

void set_cursor(long* cursor_p, long pos, long offset)
{
	*cursor_p = pos + offset;
}

int is_png(U8* fileBytes, long* cursor_p)
{
	/*
	 *	read first 8 bytes of file
	 *	function formats bytes into a string of hexadecimals,
	 *	then compares with PNGSIG
	 *
	 * */
	U8 *bytesRead = malloc(sizeof(U8) * PNG_SIG_SIZE);
	read_bytes(fileBytes, bytesRead, cursor_p, PNG_SIG_SIZE);

	char sig[25] = "";
	char hex[4];
	for (int i = 0; i < PNG_SIG_SIZE; i++)
	{
		sprintf(hex, "%02hhX ", bytesRead[i]);
		strcat(sig, hex);
	}
	free(bytesRead);
	return strcmp(sig, PNGSIG);
}

int get_png_data_IHDR(struct data_IHDR *out, U8* fileBytes, long* cursor_p)
{
	/* this function should always be called after is_png(), so that the file cursor has read 8 bytes
	 *
	 *	seek to the data block in ihdr
	 *	read into U8,
	 *	cast the first two blocks of 4 U8 as U32 pointers,
	 *	convert to host byte ordering
	 *
	 * */
	set_cursor(cursor_p, *cursor_p, 8);

	U8 *rawu8 = malloc(sizeof(U8) * DATA_IHDR_SIZE);
	read_bytes(fileBytes, rawu8, cursor_p, DATA_IHDR_SIZE);
	U32 *n_width_p = (U32 *)&rawu8[0];
	U32 *n_height_p = (U32 *)&rawu8[4];
	U32 w = ntohl(*n_width_p);
	U32 h = ntohl(*n_height_p);

	out->width = w;
	out->height = h;
	out->bit_depth = rawu8[8];
	out->color_type = rawu8[9];
	out->compression = rawu8[10];
	out->filter = rawu8[11];
	out->interlace = rawu8[12];

	set_cursor(cursor_p, *cursor_p, 4); //seek past crc
	free(rawu8);
	return 0;
}

int parsechunk(struct chunk *ch, U8* fileBytes, long* cursor_p)
{
	/*
	 *	reads the file and parses data into the passed chunk struct
	 * */
	//length
	U8 *raw_length = malloc(sizeof(U8) * CHUNK_LEN_SIZE);
	read_bytes(fileBytes, raw_length, cursor_p, CHUNK_LEN_SIZE);
	U32 length = ntohl( *(U32*)raw_length );
	free(raw_length);
	ch->length = length;

	//type
	U8 *raw_type = malloc(sizeof(U8) * CHUNK_TYPE_SIZE);
	read_bytes(fileBytes, raw_type, cursor_p, CHUNK_TYPE_SIZE);
	for (int i = 0; i < CHUNK_TYPE_SIZE; i++)
	{
		ch->type[i] = raw_type[i];
	}
	free(raw_type);

	//If type is IEND
	if (strcmp(ch->type, "IEND") == 0)
	{
		//passed all IDAT chunks, so break
		set_cursor(cursor_p, *cursor_p, ch->length + CHUNK_CRC_SIZE);
		return 2;
	}
	else if (strcmp(ch->type, "IDAT") != 0)
	{ //If type is not IDAT
		//seek past data and crc, then continue loop
		set_cursor(cursor_p, *cursor_p, ch->length + CHUNK_CRC_SIZE);
		return 1;
	}
	else if (strcmp(ch->type, "IDAT") == 0)
	{ //If type is IDAT
		//data
		U8 *raw_data = malloc(sizeof(U8) * ch->length);
		read_bytes(fileBytes, raw_data, cursor_p, ch->length);
		/* THIS POINTER NEEDS TO BE FREED LATER */
		ch->p_data = raw_data;
		//crc
		U8 *raw_crc = malloc(sizeof(U32));
		read_bytes(fileBytes, raw_crc, cursor_p, CHUNK_CRC_SIZE);
		U32 crc = ntohl( *(U32*)raw_crc );
		free(raw_crc);
		ch->crc = crc;
		return 0;
	}
}

int setIDATBuffer(U8 **idatBuffer_p, U64 *idatBufferSize_p, U64 *idatStart_p, U64 *idatEnd_p, U8* fileBytes, long* cursor_p, unsigned long fileSize) {
	while (1)
	{
		if (*cursor_p >= fileSize) return -1;
		struct chunk ch = {
			.length = 0,
			.type = {},
			.p_data = NULL,
			.crc = 0};
		int parsed = parsechunk(&ch, fileBytes, cursor_p);
		if (parsed != 0)
		{
			if (parsed == 1) //if not IEND chunk, skip
				continue;
			if (parsed == 2) //if IEND end loop;
				break;
		}

		//only get here if read IDAT chunk
		//note that idats have to be consecutive
		*idatStart_p = *idatStart_p == 0 ? ((*cursor_p) - CHUNK_CRC_SIZE - ch.length - CHUNK_TYPE_SIZE - CHUNK_LEN_SIZE) : *idatStart_p;
		*idatEnd_p = *cursor_p;
		(*idatBufferSize_p) += ch.length;
		*idatBuffer_p = realloc(*idatBuffer_p, (*idatBufferSize_p) * sizeof(U8));
		memcpy(*idatBuffer_p + (*idatBufferSize_p) - ch.length, ch.p_data, ch.length); //dest is idatBuffer + old size
		free(ch.p_data);
	} //at the end of this loop, the cursor is always going to be at the end of the IEND chunk
	return 0;
}

int unfilter_data(int bytesPerPixel, const int SCANLINE_LENGTH, int height, U8* inf)
{
	U8 *prevScanline = calloc(SCANLINE_LENGTH, sizeof(U8));
	for (int i = 0; i < height; i++)
	{
		U8 filter = inf[i * SCANLINE_LENGTH];
		inf[i * SCANLINE_LENGTH] = 0; //our image isn't going to have any filters for now
		U8 prevRGBA[bytesPerPixel];
		for (int i = 0; i < bytesPerPixel; i++) prevRGBA[i] = 0;
		U8 *tempScanline = calloc(SCANLINE_LENGTH, sizeof(U8));
		switch (filter)
		{
		case 0:
			//No Filter; don't change values, simply note them in the prev scanline
			for (int j = 1; j < SCANLINE_LENGTH; j++)
			{
				//c = ?
				U8 c = inf[i * SCANLINE_LENGTH + j];
				prevScanline[j] = c;
			}
			break;
		case 1:
			//Sub Filter: Each byte is replaced with the difference between it and the "corresponding byte" to its left.
			for (int j = 1; j < SCANLINE_LENGTH; j++)
			{
				//c = ? - prev
				U8 c = inf[i * SCANLINE_LENGTH + j];
				U8 unfiltered = c + prevRGBA[(j - 1) % bytesPerPixel];
				inf[i * SCANLINE_LENGTH + j] = unfiltered;
				prevRGBA[(j - 1) % bytesPerPixel] = unfiltered;
				prevScanline[j] = unfiltered;
			}
			break;
		case 2:
			//Up filter: Each byte is replaced with the difference between it and the byte above it (in the previous row, as it was before filtering).
			for (int j = 1; j < SCANLINE_LENGTH; j++)
			{
				//c = ? - prev
				U8 c = inf[i * SCANLINE_LENGTH + j];
				U8 unfiltered = c + prevScanline[j];
				inf[i * SCANLINE_LENGTH + j] = unfiltered;
				prevScanline[j] = unfiltered;
			}
			break;
		case 3:
			//Average filter: Each byte is replaced with the difference between it and the average of the corresponding bytes to its left and above it, truncating any fractional part.
			for (int j = 1; j < SCANLINE_LENGTH; j++)
			{
				//c = ? - (prevLeft + prevUp)/2
				U8 c = inf[i * SCANLINE_LENGTH + j];
				U8 unfiltered = c + (U8)(((unsigned int)prevRGBA[(j - 1) % bytesPerPixel] + (unsigned int)prevScanline[j]) / 2);
				inf[i * SCANLINE_LENGTH + j] = unfiltered;
				prevRGBA[(j - 1) % bytesPerPixel] = unfiltered;
				prevScanline[j] = unfiltered;
			}
			break;
		case 4:
			//Paeth filter: Each byte is replaced with the difference between it and the Paeth predictor of the corresponding bytes to its left, above it, and to its upper left.
			for (int j = 1; j < SCANLINE_LENGTH; j++)
			{
				//c = ? - paethPredictor
				U8 c = inf[i * SCANLINE_LENGTH + j];
				U8 left = prevRGBA[(j - 1) % bytesPerPixel];
				U8 top = prevScanline[j];
				U8 topleft = j - bytesPerPixel > 0 ? prevScanline[j - bytesPerPixel] : 0;
				int baseVal = (int)left + (int)top - (int)topleft;
				int diffL = abs((int)left - baseVal);
				int diffT = abs((int)top - baseVal);
				int diffTL = abs((int)topleft - baseVal);
				U8 predictor = (diffL <= diffT && diffL <= diffTL) ? left : (diffT <= diffTL) ? top : topleft;
				U8 unfiltered = c + predictor;
				inf[i * SCANLINE_LENGTH + j] = unfiltered;
				prevRGBA[(j - 1) % bytesPerPixel] = unfiltered;
				tempScanline[j] = unfiltered;
			}
			memcpy(prevScanline, tempScanline, sizeof(U8) * SCANLINE_LENGTH);
			break;
		default:
			printf("ERROR: invalid filter byte");
			return -1;
		}
		free(tempScanline);
	}
	free(prevScanline);
}

void encodeMessage(U8 **inf, U64 infsize, char *MESSAGE, int bytesPerSample, const int SCANLINE_LENGTH) {
	U64 i = 0;
	while (i < infsize)
	{
		int stringIndex = (i / bytesPerSample) / 7; //7 bit ASCII, so every 7 image bytes we move forwards a letter in the message string
		U8 messageChar = MESSAGE[stringIndex];
		for (int j = 0; j < ASCII_NUM_BITS; j++)
		{
			if (i % SCANLINE_LENGTH == 0)
			{
				i++;
				j--;
				continue; //filter byte
			}
			int writeBit = messageChar & (1 << (ASCII_NUM_BITS - 1 - j)) ? 1 : 0;
			//printf("%i", writeBit);
			U8 newChar = writeBit ? (*inf)[i] | 1 : (*inf)[i] & (~1);
			(*inf)[i] = newChar;
			i++;
		}
		//printf(" ");
		if (messageChar == 0)
			break;
	}
	//printf("\n");
}

int decodeMessage(U8 *inf, U64 infsize, const int SCANLINE_LENGTH) {
	U64 i = 0;
	U64 charCounter = 0;
	char* MESSAGE = calloc(infsize / 7, sizeof(U8));
	while (i < infsize)
	{
		U8 tempChar = 0;
		for (int j = 0; j < ASCII_NUM_BITS; j++) {
			if (i % SCANLINE_LENGTH == 0)
			{
				i++;
				j--;
				continue; //filter byte
			}
			U8 readBit = inf[i] & 1; //either 0 or 1
			//if it's a 1, shift then OR. if it's a zero. shift a 1 then invert then AND.
			tempChar = readBit ? (tempChar | (1 << (ASCII_NUM_BITS - 1 - j))) : (tempChar & ~(1 << (ASCII_NUM_BITS - 1 - j)));
			i++;
		}
		MESSAGE[charCounter] = tempChar;
		charCounter++;
		if (charCounter == 3) {
			if (MESSAGE[0] != 'U' || MESSAGE[1] != '*' || MESSAGE[2] != 'U') {
				//doesn't have our sequence at the beginning
				return -1;
			}
		}
	}
	printf("%s\n", &MESSAGE[0] + 3); //we know that the first 3 chars are our identifier sequence
	return 0;
}

U32 calculate_crc(U8 type[4], U8* defData, long defsize)
{
	U8 *crcbuf = malloc(sizeof(U8) * (defsize + CHUNK_TYPE_SIZE));
	memcpy(crcbuf, type, sizeof(U8) * CHUNK_TYPE_SIZE);
	memcpy(crcbuf + CHUNK_TYPE_SIZE, defData, sizeof(U8) * defsize);

	U32 crcval = crc(crcbuf, defsize + CHUNK_TYPE_SIZE);
	free(crcbuf);
	return crcval;
}

void printHex(U8 *buffer, U64 bufferSize, int rowlength) {
	for (U64 i = 0; i < bufferSize; i++)
	{
		printf("%02hhX ", buffer[i]);
		if (rowlength != 0) {
			if ((i + 1) % rowlength == 0) printf("\n");
		}
	}
	printf("\n");
}