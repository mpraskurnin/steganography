# Steganography
From Wikipedia: Steganography is the practice of concealing a message within another message or a physical object. In computing/electronic contexts, a computer file, message, image, or video is concealed within another file, message, image, or video.

In this fashion, this C program can encode a message string within the least significant bits of a PNG's pixel colour channels, as well as reverse that process to decode that message. Modifying the LSBs means that we change the colour values of the pixels as we encode the data, but the change is so minor that it isn't perceptible.
## PNG Basics
PNGs are composed of an 8-byte file signature, followed by a sequence of chunks. Each chunk is made up of:
- a 4 byte data length value
- a 4 byte chunk type
- the data bytes, the number of which is defined above
- a 4 byte cyclic redundancy check

The relevant chunk types for our purposes are IHDR (the header, contains image details/properties), IDAT (contains the actual pixels which make up the image), and IEND (which marks the end of the PNG).
## Implementation
To modify the pixels in the PNG, there are a couple steps needed:
1. Coallesce all data within IDAT chunks into one buffer
2. Inflate (decompress) the data
3. Remove the filters from the data
    - the PNG standard supports a couple filters that can help with compression of the data (e.g. if the image is a horizontal gradient of all colours: without a filter each pixel would be a unique colour, which can't be effectively compressed; instead, with a filter you can say that each pixel represents the difference from the last pixel. Thus with a constant gradient, almost all the bytes in the image will be the same, which compresses much more effectively.)
4. If we consider the message string as 7-bit ASCII, we can now do bitwise operations to conceal 1 character from the message string for each 7 bytes in the inflated IDAT chunk.


## Resources
For details on the PNG standard: http://www.libpng.org/pub/png/spec/1.2/PNG-Contents.html

Project inspired by [Computerphile](https://www.youtube.com/watch?v=TWEXCYQKyDc).

PNG processing was the theme of the labs in ECE252: Systems Programming and Concurrency. Thus, some code (specifically relating to zlib compression and CRC) is written by lab TAs and is labelled as such.
