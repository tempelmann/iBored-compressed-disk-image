#iBored compressed image format

[iBored](http://apps.tempel.org/iBored/) is a disk sector editor for Mac OS X, Windows and Linux.

This project describes the compressed image file format and includes C code to decompress it.

##Downloads

[The original (current) version of this file](http://apps.tempel.org/iBored/iBoredImg.html)

[Sample code](http://apps.tempel.org/iBored/iBoredImg.zip)


##Format specification

**Note:** All values are in little-endian format.

Current version: 2

The file extension should be: .iboredimg

The file shall be considered read-only after creation, because the format currently does not provide an efficient way of updating data inside the image as there's not enough information in it to manage reallocation of overwritten data. Therefore, this is primarily a format for archiving.

The file format consists of several sections:

1. *Header*, which is always located at the start of the file.
2. Compressed *Chunk* segments.
3. Table with file offsets for each segment (*Segments Table*).
4. *Disk Information* (optional).

###File Header section

**Note:** Values starting with "$" mean hex values.

Offset $00-$57: File Identifier

	69 42 6F 72 65 64 20 63 6F 6D 70 72 65 73 73 65	64 20 64 69 73 6B 20 69 6D 61 67 65 0D 68 74 74	70 3A 2F 2F 67 69 74 68 75 62 2E 63 6F 6D 2F 74	65 6D 70 65 6C 6D 61 6E 6E 2F 69 42 6F 72 65 64	2D 63 6F 6D 70 72 65 73 73 65 64 2D 64 69 73 6B	2D 69 6D 61 67 65 0D 00 
The ASCII representation is:
	iBored compressed disk image<CR>
	http://github.com/tempelmann/iBored-compressed-disk-image<CR><NUL>

* Offset $58-$59: Reserved (=0), ignored by reader
* Offset $5A: (uint8) Written By Version (ignored by reader)
* Offset $5B: (uint8) Readable By Version (reader must check this)
* Offset $5C: (uint8) Compression Method (see below)
* Offset $5D: Reserved (=0), ignored by reader
* Offset $5E-$5F: (uint16) Header Size (=256)
* Offset $60-$67: (uint64) Uncompressed Disk Image Size
* Offset $68-$6F: (uint64) Chunk Size (shall be less than 2^31)
* Offset $70-$77: (uint64) Offset To Disk Information
* Offset $78-$7B: (uint32) Length Of Disk Information
* Offset $7C-$7F: (uint32) Physical Block Size
* Offset $80-$87: (uint64) Offset To Segments Table
* Offset $88-$FF: Unused (=0), ignored by reader

###Disk Information

Optional. JSON formatted non-critical information text, e.g. about the source disk.

###Chunks

A Chunk is a segment from the original disk image, with length of *Chunk Size*, starting at the offset of a whole multiple of *Chunk Size*. As the disk image's size is not necessarily a multiple of *Chunk Size*, the last chunk may be shorter.

The compressed chunks are stored inside this file anywhere past the header and in any order.

Starting with version 2, each compressed chunk starts with a uint32 value specifying the size of the compressed chunk data that follows. In version 1, this value is missing and the compressed data starts right away.

###Compression Method

Value 1: Every chunk is compressed using using [zlib](http://zlib.net/) "deflate" (windowBits=15). The data includes the 2-byte header and 4-byte Adler checksum.

Value 2: Every chunk is "Run Length" (RLE) compressed.

###Run Length compression

This is an [RLE](https://en.wikipedia.org/wiki/Run-length_encoding) compression.

####Header

* Offset $00-$03: $00 $52 $4C $45
* Offset $04-$07: (int32) Total length of this compressed chunk
* Offset $08-$0B: (int32) Uncompressed data length (equals Chunk Size)
* Offset $0C-$0F: Reserved (=0), ignored by reader
* Offset $10-...: Segments

####Segment

* Offset $00-$03: $00 $52 $4C $53
* Offset $04-$07: (int32) Segment length (rounded up to 4 or 8 byte boundaries)
* Offset $08-$0B: (int32) Output pattern length
* Offset $0C-$0F: (int32) Length of pattern that follows
* Offset $10-...: Pattern

If a chunk can not be RLE-compressed at all, it'll be 32 bytes more in length than the original data.

###Segments Table

This is a simple array of uint64 values, each specifying the file offset in this file to a compressed chunk. The number of array elements equals the formula:

	element count = (Uncompressed Disk Image Size + Chunk Size - 1) / Chunk Size

The array elements are in order of the original disk image's chunks, so that the first array element identifies the first disk chunk and the last element identifies the last chunk.
