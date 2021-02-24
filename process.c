#include <stdio.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include "lab_png.h"
#include "crc.h"
#include "zutil.h"

const int CHANNELS[7] = {1, 0, 3, 0, 2, 0, 4};

int main(int argc, char **argv)
{
	if (argc < 2 || argc > 3) {
		printf("Usage:\t%s <png file name> <message string (for encoding only)>\ne.g.\t%s test.png\n\t%s test.png \"hello world\"\n\t%s test.png \"$(< input.txt)\"\n", argv[0], argv[0], argv[0], argv[0]);
		return -1;
	}
	//Open file, handle initial errors
	FILE *f = fopen(argv[1], "r+b");

	if (f == NULL)
	{
		printf("File not found!\n");
		return -1;
	}
	fseek(f, 0, SEEK_END);
	unsigned long fileSize = ftell(f);
	rewind(f);
	U8 *fileBytes = calloc(sizeof(U8), fileSize);
	fread(fileBytes, sizeof(U8), fileSize, f);
	unsigned long cursor = 0;
	//file loaded into memory at fileBytes
	if (is_png(fileBytes, &cursor) != 0)
	{
		printf("%s: Not a PNG file\n", argv[1]);
		free(fileBytes);
		fclose(f);
		return -1;
	}

	//At this point, we've seeked past the PNG Signature
	//First chunk will always be the header
	struct data_IHDR ihdr;
	get_png_data_IHDR(&ihdr, fileBytes, &cursor);
	if (ihdr.color_type == 3 || ihdr.bit_depth != 8 || ihdr.interlace == 1) //Pallete index || bit depth isn't 8 || adam7 interlace
	{
		printf("Unsupported PNG file\n");
		free(fileBytes);
		fclose(f);
		return -1;
	}

	//Next, seek through the file until we get to an IDAT chunk; read all IDAT data from multiple chunks into a single buffer.
	//At the top of this loop, the file cursor should always be at the end of the previous chunk. ie next 8 bytes are length, type.
	U8 *idatBuffer = NULL; //needs to be freed
	U64 idatBufferSize = 0;
	U64 idatStart = 0; //start of the first idat chunk
	U64 idatEnd = 0; //end of last idat chunk
	if (setIDATBuffer(&idatBuffer, &idatBufferSize, &idatStart, &idatEnd, fileBytes, &cursor, fileSize) != 0) {
		printf("Error getting IDAT chunk\n");
		free(idatBuffer);
		free(fileBytes);
		return -1;
	}

	//have compressed IDAT bytes, need to inflate them
	const int SCANLINE_LENGTH = ihdr.width * CHANNELS[ihdr.color_type] * (ihdr.bit_depth / 8) + 1; //pixels per row * samples per pixel * bytes per sample + 1 filter byte
	U64 infsize = (U64)ihdr.height * (SCANLINE_LENGTH);
	U8 *inf = calloc(sizeof(U8), infsize);
	mem_inf(inf, &infsize, idatBuffer, idatBufferSize);
	free(idatBuffer);
	//raw data freed, inflated data in inf

	//unfilter data
	int bytesPerSample = ihdr.bit_depth / 8;
	int bytesPerPixel = CHANNELS[ihdr.color_type] * bytesPerSample;
	unfilter_data(bytesPerPixel, SCANLINE_LENGTH, ihdr.height, inf);

	if (argc == 2) {
		//decode message in png
		//png data is currently in inf
		//look at the last bit in each byte
		if (decodeMessage(inf, infsize, SCANLINE_LENGTH) != 0) {
			printf("Error: No encoded message found in the png.\n");
			free(inf);
			free(fileBytes);
			return 1;
		} else {
			free(inf);
			free(fileBytes);
			return 0;
		}
	}


	//Only continue if we're encoding a message into the png.

	char MESSAGE[4 + strlen(argv[2])];
	strcpy(MESSAGE, "U*U"); //arbitrary sequence to characterize the start of encoded string
	strcat(MESSAGE, argv[2]);
	//first check if we have enough bytes to encode the message in
	if ((infsize - (U64)ihdr.height) / 7 <  strlen(MESSAGE) + 1) {
		printf("Error: the message string to conceal is too large for this image.\n");
		free(inf);
		free(fileBytes);
		return -1;
	}
	//have IDAT bytes in memory, now want to modify them and write them back
	encodeMessage(&inf, infsize, MESSAGE, bytesPerSample, SCANLINE_LENGTH);
	//printHex(inf, infsize, SCANLINE_LENGTH);

	//inf is now contains the final inflated data, which we need to deflate (compress)
	U8 *def = malloc(sizeof(U8) * infsize); //deflated won't be larger than inflated data
	U64 defsize = 0;
	mem_def(def, &defsize, inf, infsize, Z_DEFAULT_COMPRESSION); 
	free(inf);


	//def contains deflated data; need to replace old deflated data with ours
	set_cursor(&cursor, idatStart, 0); //cursor at start of first idat chunk
	U32 newlength = htonl(defsize);
	U8 *newlength_p = (U8 *)&newlength;
	write_bytes(fileBytes, &cursor, newlength_p, NUM_U8_IN_U32);
	//realloc to remove space prev taken up by idat chunks, add space for new deflated size + chunk overhead
	fileBytes = realloc(fileBytes, fileSize - (idatEnd - idatStart) + (CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + defsize + CHUNK_CRC_SIZE));
	set_cursor(&cursor, idatEnd, 0); //cursor at end of prev last idat chunk
	//move the rest of the file forwards to make space for new deflated data
	//dest: end of new single idat chunk. source: idatEnd. size: num bytes from idatEnd to eof.
	memmove((void *)(fileBytes + sizeof(U8)*(idatStart + CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + defsize + CHUNK_CRC_SIZE)), (void *)(fileBytes + sizeof(U8)*cursor), sizeof(U8) * (fileSize - cursor));
	//copy def to start of data
	memcpy((void *)fileBytes + idatStart + CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE, (void *)def, defsize * sizeof(U8));
	fileSize = fileSize - (idatEnd - idatStart) + (CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + defsize + CHUNK_CRC_SIZE);
	//cursor at end of old deflated data. move to end of new deflated data
	set_cursor(&cursor, idatStart, CHUNK_LEN_SIZE + CHUNK_TYPE_SIZE + defsize);
	//calculate new crc, write to next 4 bytes
	U8 type[4] = {'I', 'D', 'A', 'T'};
	U32 ncrc = htonl(calculate_crc(type, def, defsize));
	U8 *ncrc_p = (U8 *)&ncrc;
	free(def);
	write_bytes(fileBytes, &cursor, ncrc_p, NUM_U8_IN_U32);
	//new crc written, cursor at end of chunk


	FILE *toWrite = fopen("stega.png", "wb");
	if (toWrite == NULL) {
		printf("error opening write file\n");
		return -1;
	}
	fwrite(fileBytes, sizeof(U8), fileSize, toWrite);
	fclose(toWrite);
	printf("stega.png created.\n");
	free(fileBytes);
	fclose(f);
	return 0;
}
