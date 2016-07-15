//
//  main.c
//  iBored Img Decompression
//
//  Created by Thomas Tempelmann on 15.07.16.
//  For unrestricted use.
//
//  Origin: https://github.com/tempelmann/iBored-compressed-disk-image
//
//  TAB = 4 spaces
//

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <zlib.h>
#include <string.h>
#include <assert.h>


#define VERSION "1.0"

typedef int boolean;
#define false 0
#define true 1


/*
 * RLE decompression
 */

typedef struct {
	char ident[4]; // $00 $52 $4C $45
	int32_t totalLength;
	int32_t uncompressedLength;
	int32_t reserved;
} RLEHeader;

typedef struct {
	char ident[4]; // $00 $52 $4C $53
	int32_t segmentLength;
	int32_t outputLength;
	int32_t patternLength;
	uint8_t pattern[];
} RLESegment;

static boolean RLEdecompress (uint8_t *inputPtr, void *outBuf)
{
	assert (sizeof(RLEHeader) == 16);
	assert (sizeof(RLESegment) == 16);

	if (memcmp((void*)inputPtr, "\x00RLE", 4) != 0) {
		return false;
	}
	
	int32_t inputRemain = ((RLEHeader*)inputPtr)->totalLength - sizeof(RLEHeader);
	int32_t outputRemain = ((RLEHeader*)inputPtr)->uncompressedLength;
	inputPtr += sizeof(RLEHeader);
	
	while (inputRemain > 0) {
		// handle one segment

		if (memcmp((void*)inputPtr, "\x00RLS", 4) != 0) {
			return false;
		}
		int32_t outLen = ((RLESegment*)inputPtr)->outputLength;
		int32_t patternLen = ((RLESegment*)inputPtr)->patternLength;
		int32_t segLen = ((RLESegment*)inputPtr)->segmentLength;
		
		if (segLen > inputRemain) {
			return false;
		}
		if (outLen > outputRemain) {
			return false;
		}
		if (patternLen > (segLen-sizeof(RLESegment))) {
			return false;
		}

		outputRemain -= outLen;
		
		// unwrap the pattern
		#define PTYPE uint64_t
		void *patternStart = inputPtr + sizeof(RLESegment);
		if (patternLen == sizeof(PTYPE)) {
			// special handling  for our default repeat pattern length
			int32_t n = (outLen / sizeof(PTYPE));
			PTYPE v = *(PTYPE*)patternStart;
			PTYPE *p = outBuf;
			for (int32_t i = 0; i < n; ++i) {
				*p++ = v;
			}
			n *= sizeof(PTYPE);
			outLen -= n;
			outBuf += n;
		}
		// handle random pattern length and any remainder from the optimized unwrap above
		while (outLen > 0) {
			int32_t n;
			if (outLen < patternLen) {
				n = outLen;
			} else {
				n = patternLen;
			}
			memcpy (outBuf, patternStart, n);
			outLen -= n;
			outBuf += n;
		}
		
		inputRemain -= segLen;
		inputPtr += segLen;
	}
	
	return (inputRemain == 0) && (outputRemain == 0);
}

/*
 * zlib inflation
 */

static boolean zlib_decompress (FILE *infile, size_t inSize, void *outBuf, size_t outSize)
{
	// If inSize==0, it means that we do not know the input length (happens with version 1);
	// it also means that we do not have a proper stream-end marker and no working checksum.
	
	const uInt chunkSize = 0x100000;
	static char inBuf[chunkSize];
	
	z_stream strm = {0};
	int ret = inflateInit (&strm);
	if (ret != Z_OK) {
		return false;
	}

	strm.avail_out = (uInt) outSize;
	strm.next_out = outBuf;
	
	do {
		size_t readLen = chunkSize;
		strm.avail_in = (uInt) fread (inBuf, 1, readLen, infile);
		if (ferror (infile) || strm.avail_in == 0) {
			break;
		}
		strm.next_in = (Bytef*)inBuf;
		ret = inflate (&strm, Z_NO_FLUSH);
		assert(ret != Z_STREAM_ERROR);
		switch (ret) {
		case Z_NEED_DICT:
		case Z_DATA_ERROR:
		case Z_MEM_ERROR:
			if (inSize == 0 && strm.avail_out == 0) {
				// Version 1 zlib streams were not properly finished, but they're still valid (we'd just not detect any checksum errors)
				ret = Z_STREAM_END;
			}
			break;
		}
	} while (ret != Z_STREAM_END);

	inflateEnd (&strm);
	
	return (ret == Z_STREAM_END) && (inSize == 0 || inSize == strm.total_in);
}

/*
 * .iboredimg file decoding
 */

typedef struct {
	char		fileid[0x5A];
	uint8_t		versionWrittenBy;
	uint8_t		versionReadableBy;
	uint8_t		compressionMethod;
	uint8_t		reserved1;
	uint16_t	headerSize;
	uint64_t	uncompressedSize;
	uint64_t	chunkSize;
	uint64_t	diskInfoOffset;
	uint32_t	diskInfoSize;
	uint32_t	physBlockSize;
	uint64_t	segmentsTableOffset;
	uint8_t		reserved2[0x78];
} FileHeader;

static void showImgInfo (FileHeader *header, FILE *infile)
{
	printf ("Written by version:     %d\n", (int)header->versionWrittenBy);
	printf ("Readable by version:    %d\n", (int)header->versionReadableBy);
	printf ("Uncompressed file size: %llu\n", header->uncompressedSize);
	printf ("Physical block size:    %u\n", header->physBlockSize);
	char *s = "";
	if (header->compressionMethod == 0) {
		s = "";
	} else if (header->compressionMethod == 1) {
		s = "zlib";
	} else if (header->compressionMethod == 2) {
		s = "RLE";
	} else {
		s = "unknown";
	}
	printf ("Compression method:     %d (%s)\n", (int)header->compressionMethod, s);
	
	if (header->diskInfoSize > 0) {
		printf ("Disk info:\n");
		off_t offs = header->diskInfoOffset;
		if (fsetpos (infile, &offs) != 0) {
			printf ("(Error - can't seek to it)\n");
		} else {
			s = malloc(header->diskInfoSize);
			if (!s) {
				printf ("(Error - can't alloc %u bytes)\n", header->diskInfoSize);
			} else {
				size_t n = fread(s, 1, header->diskInfoSize, infile);
				if (n != header->diskInfoSize) {
					printf ("(Error - can't read it)\n");
				} else {
					printf ("%s\n", s);
				}
				free (s);
			}
		}
	}
}

static boolean decodeImg (FileHeader *header, FILE *infile, FILE *outfile)
{
	if (header->versionReadableBy > 2) {
		fprintf (stderr, "File uses unsupported version %d\n", (int)header->versionReadableBy);
		return false;
	}
	
	boolean chunksHaveLengthPrefix = header->versionReadableBy >= 2;
	
	if (header->compressionMethod != 1 && header->compressionMethod != 2) {
		fprintf (stderr, "Unsupported compression method (%d)\n", (int)header->compressionMethod);
		return false;
	}
	
	// Load the segments offsets table
	size_t segOfsSize = ((header->uncompressedSize + header->chunkSize - 1) / header->chunkSize) * sizeof(uint64_t);
	uint64_t *segmentOffsets = malloc (segOfsSize);
	if (!segmentOffsets) {
		fprintf (stderr, "Can't alloc segments offsets buffer\n");
		return false;
	}
	off_t offs = header->segmentsTableOffset;
	if (fsetpos (infile, &offs) != 0) {
		fprintf (stderr, "Seek error");
		return false;
	}
	size_t n = fread (segmentOffsets, 1, segOfsSize, infile);
	if (n != segOfsSize) {
		fprintf (stderr, "Can't read the segments offsets table\n");
		return false;
	}
	
	// Prepare input buffer (RLE only)
	size_t inBufSize = header->chunkSize + 32;	// worst case for RLE
	void *inBuf = NULL;
	if (header->compressionMethod == 2) {
		inBuf = malloc(inBufSize);
		if (!inBuf) {
			fprintf (stderr, "Can't alloc input chunk buffer\n");
			return false;
		}
	}

	// Prepare output buffer
	void *outBuf = malloc(header->chunkSize);
	if (!outBuf) {
		fprintf (stderr, "Can't alloc output chunk buffer\n");
		return false;
	}

	// Read and decompress every chunk
	uint64_t remainBytes = header->uncompressedSize;
	long chunkNum = 0;
	while (remainBytes > 0) {

		// determine the chunk's output size
		size_t writeAmount = remainBytes;
		if (writeAmount > header->chunkSize) writeAmount = header->chunkSize;
		
		// seek to compressed input chunk
		off_t offs = segmentOffsets[chunkNum];
		if (fsetpos (infile, &offs) != 0) {
			fprintf (stderr, "Seek error (chunk #%ld)\n", chunkNum);
			return false;
		}
		
		size_t inSize = 0;
		if (chunksHaveLengthPrefix) {
			// fetch the compressed chunk's size
			n = fread (&inSize, 1, sizeof(inSize), infile);
			if (n != sizeof(inSize)) {
				fprintf (stderr, "Can't read header of chunk #%ld\n", chunkNum);
				return false;
			}
		}
		
		// uncompress the chunk into outBuf
		if (header->compressionMethod == 1) {
			// zlib (deflate)
			if (!zlib_decompress (infile, inSize, outBuf, writeAmount)) {
				fprintf (stderr, "Can't inflate chunk #%ld\n", chunkNum);
				return false;
			}
		} else {
			// RLE
			size_t readAmount;
			if (inSize) {
				// Version 2: we know the overall size already
				n = 0;
				readAmount = inSize;
			} else {
				// Version 1: read the RLE header first to determine the overall size
				n = fread (inBuf, 1, sizeof(RLEHeader), infile);
				if (n != sizeof(RLEHeader)) {
					fprintf (stderr, "Can't read header of chunk #%ld\n", chunkNum);
					return false;
				}
				readAmount = ((RLEHeader*)inBuf)->totalLength - n;
			}
			n = fread (inBuf+n, 1, readAmount, infile);
			if (n != readAmount) {
				fprintf (stderr, "Can't read chunk #%ld\n", chunkNum);
				return false;
			}
			if (!RLEdecompress(inBuf, outBuf)) {
				fprintf (stderr, "Can't decompress chunk #%ld", chunkNum);
				return false;
			}
		}

		// write outBuf to file
		if (fwrite (outBuf, 1, writeAmount, outfile) != writeAmount) {
			fprintf (stderr, "Write failed\n");
			return false;
		}
		
		remainBytes -= writeAmount;
		chunkNum += 1;
	}
	
	return true;
}

const char headerIdent[] = "iBored compressed disk image\rhttp://github.com/tempelmann/iBored-compressed-disk-image\r";

int main (int argc, const char* argv[])
{
	if (argc < 3 || argc > 4) {
		goto showUsage;
	}

	const char *cmd = argv[1];
	const char *inPath = argv[2];

	// Open the input file and make sure it's in the expected format
	FILE *infile = fopen (inPath, "rb");
	if (!infile) {
		fprintf (stderr, "Can't open infile.\n");
		return EXIT_FAILURE;
	}
	FileHeader header;
	size_t n = fread (&header, 1, sizeof(header), infile);
	if (n != sizeof(header) || header.headerSize != n || strcmp (header.fileid, headerIdent) != 0) {
		fprintf (stderr, "infile is not a compressed iBored image.\n");
		return EXIT_FAILURE;
	}
	
	// handle the command
	if (argc == 4 && strcmp (cmd, "--decode") == 0) {
		const char *outPath = argv[3];
		FILE *outfile = fopen (outPath, "wb");
		if (!outfile) {
			fprintf (stderr, "Can't open outfile.\n");
			return EXIT_FAILURE;
		}
		boolean ok = decodeImg (&header, infile, outfile);
		if (fclose (outfile) != 0) {
			if (ok) fprintf (stderr, "Closing outfile failed.\n");
			ok = false;
		}
		if (!ok) {
			unlink (outPath);
			return EXIT_FAILURE;
		}
	} else if (argc == 3 && strcmp (cmd, "--info") == 0) {
		showImgInfo (&header, infile);
	} else {
		goto showUsage;
	}
	
	return EXIT_SUCCESS;

showUsage:
	fprintf (stderr, "Usage: iboredimg --decode infile outfile\n");
	fprintf (stderr, "       iboredimg --info infile\n");
	fprintf (stderr, "\n");
	fprintf (stderr, "Version "VERSION"\n");
	return EXIT_FAILURE;
}
