/*

QOI - The "Quite OK Image" format for fast, lossless image compression

Dominic Szablewski - https://phoboslab.org


-- LICENSE: The MIT License(MIT)

Copyright(c) 2021 Dominic Szablewski

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files(the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and / or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions :
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.


-- About

QOI encodes and decodes images in a lossless format. Compared to stb_image and
stb_image_write QOI offers 20x-50x faster encoding, 3x-4x faster decoding and
20% better compression.


-- Synopsis

// Define `QOI_IMPLEMENTATION` in *one* C/C++ file before including this
// library to create the implementation.

#define QOI_IMPLEMENTATION
#include "qoi.h"

// Encode and store an RGBA buffer to the file system. The qoi_desc describes
// the input pixel data.
qoi_write("image_new.qoi", rgba_pixels, &(qoi_desc){
	.width = 1920,
	.height = 1080,
	.channels = 4,
	.colorspace = QOI_SRGB
});

// Load and decode a QOI image from the file system into a 32bbp RGBA buffer.
// The qoi_desc struct will be filled with the width, height, number of channels
// and colorspace read from the file header.
qoi_desc desc;
void *rgba_pixels = qoi_read("image.qoi", &desc, 4);



-- Documentation

This library provides the following functions;
- qoi_read    -- read and decode a QOI file
- qoi_decode  -- decode the raw bytes of a QOI image from memory
- qoi_write   -- encode and write a QOI file
- qoi_encode  -- encode an rgba buffer into a QOI image in memory

See the function declaration below for the signature and more information.

If you don't want/need the qoi_read and qoi_write functions, you can define
QOI_NO_STDIO before including this library.

This library uses malloc() and free(). To supply your own malloc implementation
you can define QOI_MALLOC and QOI_FREE before including this library.

This library uses memset() to zero-initialize the index. To supply your own
implementation you can define QOI_ZEROARR before including this library.


-- Data Format

A QOI file has a 14 byte header, followed by any number of data "chunks" and an
8-byte end marker.

struct qoi_header_t {
	char     magic[4];   // magic bytes "qoif"
	uint32_t width;      // image width in pixels (BE)
	uint32_t height;     // image height in pixels (BE)
	uint8_t  channels;   // 3 = RGB, 4 = RGBA
	uint8_t  colorspace; // 0 = sRGB with linear alpha, 1 = all channels linear
};

Images are encoded row by row, left to right, top to bottom. The decoder and
encoder start with {r: 0, g: 0, b: 0, a: 255} as the previous pixel value. An
image is complete when all pixels specified by width * height have been covered.

Pixels are encoded as
 - a run of the previous pixel
 - an index into an array of previously seen pixels
 - a difference to the previous pixel value in r,g,b
 - full r,g,b or r,g,b,a values

The color channels are assumed to not be premultiplied with the alpha channel
("un-premultiplied alpha").

A running array[64] (zero-initialized) of previously seen pixel values is
maintained by the encoder and decoder. Each pixel that is seen by the encoder
and decoder is put into this array at the position formed by a hash function of
the color value. In the encoder, if the pixel value at the index matches the
current pixel, this index position is written to the stream as QOI_OP_INDEX.
The hash function for the index is:

	index_position = (r * 3 + g * 5 + b * 7 + a * 11) % 64

Each chunk starts with a 2- or 8-bit tag, followed by a number of data bits. The
bit length of chunks is divisible by 8 - i.e. all chunks are byte aligned. All
values encoded in these data bits have the most significant bit on the left.

The 8-bit tags have precedence over the 2-bit tags. A decoder must check for the
presence of an 8-bit tag first.

The byte stream's end is marked with 7 0x00 bytes followed a single 0x01 byte.


The possible chunks are:


.- QOI_OP_INDEX ----------.
|         Byte[0]         |
|  7  6  5  4  3  2  1  0 |
|-------+-----------------|
|  0  0 |     index       |
`-------------------------`
2-bit tag b00
6-bit index into the color index array: 0..63

A valid encoder must not issue 7 or more consecutive QOI_OP_INDEX chunks to the
index 0, to avoid confusion with the 8 byte end marker.


.- QOI_OP_DIFF -----------.
|         Byte[0]         |
|  7  6  5  4  3  2  1  0 |
|-------+-----+-----+-----|
|  0  1 |  dr |  dg |  db |
`-------------------------`
2-bit tag b01
2-bit   red channel difference from the previous pixel between -2..1
2-bit green channel difference from the previous pixel between -2..1
2-bit  blue channel difference from the previous pixel between -2..1

The difference to the current channel values are using a wraparound operation,
so "1 - 2" will result in 255, while "255 + 1" will result in 0.

Values are stored as unsigned integers with a bias of 2. E.g. -2 is stored as
0 (b00). 1 is stored as 3 (b11).

The alpha value remains unchanged from the previous pixel.


.- QOI_OP_LUMA -------------------------------------.
|         Byte[0]         |         Byte[1]         |
|  7  6  5  4  3  2  1  0 |  7  6  5  4  3  2  1  0 |
|-------+-----------------+-------------+-----------|
|  1  0 |  green diff     |   dr - dg   |  db - dg  |
`---------------------------------------------------`
2-bit tag b10
6-bit green channel difference from the previous pixel -32..31
4-bit   red channel difference minus green channel difference -8..7
4-bit  blue channel difference minus green channel difference -8..7

The green channel is used to indicate the general direction of change and is
encoded in 6 bits. The red and blue channels (dr and db) base their diffs off
of the green channel difference and are encoded in 4 bits. I.e.:
	dr_dg = (last_px.r - cur_px.r) - (last_px.g - cur_px.g)
	db_dg = (last_px.b - cur_px.b) - (last_px.g - cur_px.g)

The difference to the current channel values are using a wraparound operation,
so "10 - 13" will result in 253, while "250 + 7" will result in 1.

Values are stored as unsigned integers with a bias of 32 for the green channel
and a bias of 8 for the red and blue channel.

The alpha value remains unchanged from the previous pixel.


.- QOI_OP_RUN ------------.
|         Byte[0]         |
|  7  6  5  4  3  2  1  0 |
|-------+-----------------|
|  1  1 |       run       |
`-------------------------`
2-bit tag b11
6-bit run-length repeating the previous pixel: 1..62

The run-length is stored with a bias of -1. Note that the run-lengths 63 and 64
(b111110 and b111111) are illegal as they are occupied by the QOI_OP_RGB and
QOI_OP_RGBA tags.


.- QOI_OP_RGB ------------------------------------------.
|         Byte[0]         | Byte[1] | Byte[2] | Byte[3] |
|  7  6  5  4  3  2  1  0 | 7 .. 0  | 7 .. 0  | 7 .. 0  |
|-------------------------+---------+---------+---------|
|  1  1  1  1  1  1  1  0 |   red   |  green  |  blue   |
`-------------------------------------------------------`
8-bit tag b11111110
8-bit   red channel value
8-bit green channel value
8-bit  blue channel value

The alpha value remains unchanged from the previous pixel.


.- QOI_OP_RGBA ---------------------------------------------------.
|         Byte[0]         | Byte[1] | Byte[2] | Byte[3] | Byte[4] |
|  7  6  5  4  3  2  1  0 | 7 .. 0  | 7 .. 0  | 7 .. 0  | 7 .. 0  |
|-------------------------+---------+---------+---------+---------|
|  1  1  1  1  1  1  1  1 |   red   |  green  |  blue   |  alpha  |
`-----------------------------------------------------------------`
8-bit tag b11111111
8-bit   red channel value
8-bit green channel value
8-bit  blue channel value
8-bit alpha channel value

*/


/* -----------------------------------------------------------------------------
Header - Public functions */

#ifndef QOI_H
#define QOI_H

#ifdef __cplusplus
extern "C" {
#endif

/* A pointer to a qoi_desc struct has to be supplied to all of qoi's functions.
It describes either the input format (for qoi_write and qoi_encode), or is
filled with the description read from the file header (for qoi_read and
qoi_decode).

The colorspace in this qoi_desc is an enum where
	0 = sRGB, i.e. gamma scaled RGB channels and a linear alpha channel
	1 = all channels are linear
You may use the constants QOI_SRGB or QOI_LINEAR. The colorspace is purely
informative. It will be saved to the file header, but does not affect
en-/decoding in any way. */

#define QOI_SRGB   0
#define QOI_LINEAR 1

typedef struct {
	unsigned int width;
	unsigned int height;
	unsigned char channels;
	unsigned char colorspace;
} qoi_desc;

#ifndef QOI_NO_STDIO

/* Encode raw RGB or RGBA pixels into a QOI image and write it to the file
system. The qoi_desc struct must be filled with the image width, height,
number of channels (3 = RGB, 4 = RGBA) and the colorspace.

The function returns 0 on failure (invalid parameters, or fopen or malloc
failed) or the number of bytes written on success. */

int qoi_write(const char* filename, const void* data, const qoi_desc* desc);


/* Read and decode a QOI image from the file system. If channels is 0, the
number of channels from the file header is used. If channels is 3 or 4 the
output format will be forced into this number of channels.

The function either returns NULL on failure (invalid data, or malloc or fopen
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
will be filled with the description from the file header.

The returned pixel data should be free()d after use. */

void* qoi_read(const char* filename, qoi_desc* desc, int channels);

#endif /* QOI_NO_STDIO */


/* Encode raw RGB or RGBA pixels into a QOI image in memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the encoded data on success. On success the out_len
is set to the size in bytes of the encoded data.

The returned qoi data should be free()d after use. */

void* qoi_encode(const void* data, const qoi_desc* desc, int* out_len);


/* Decode a QOI image from memory.

The function either returns NULL on failure (invalid parameters or malloc
failed) or a pointer to the decoded pixels. On success, the qoi_desc struct
is filled with the description from the file header.

The returned pixel data should be free()d after use. */

void* qoi_decode(const void* data, int size, qoi_desc* desc, int channels);


#ifdef __cplusplus
}
#endif
#endif /* QOI_H */


/* -----------------------------------------------------------------------------
Implementation */

#ifdef QOI_IMPLEMENTATION
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>


#ifndef QOI_MALLOC
	#define QOI_MALLOC(sz) malloc(sz)
	#define QOI_FREE(p)    free(p)
#endif
#ifndef QOI_ZEROARR
	#define QOI_ZEROARR(a) memset((a),0,sizeof(a))
#endif

#define QOI_OP_INDEX  0x00 /* 00xxxxxx */
#define QOI_OP_DIFF   0x40 /* 01xxxxxx */
#define QOI_OP_LUMA   0x80 /* 10xxxxxx */
#define QOI_OP_RUN    0xc0 /* 11xxxxxx */
#define QOI_OP_RGB    0xfe /* 11111110 */
#define QOI_OP_RGBA   0xff /* 11111111 */

#define QOI_MASK_2    0xc0 /* 11000000 */

#define QOI_COLOR_HASH(C) (C.rgba.r*3 + C.rgba.g*5 + C.rgba.b*7 + C.rgba.a*11)
#define QOI_MAGIC \
	(((unsigned int)'q') << 24 | ((unsigned int)'o') << 16 | \
	 ((unsigned int)'i') <<  8 | ((unsigned int)'f'))
#define QOI_HEADER_SIZE 14

#define QOI_HUFF_MAX_BIT_NUM 32
#define QOI_HUFF_ENCODED_BIT 0x80
#define QOI_HUFF_DECODING_TABLE_WIDTH 11

/* 2GB is the max file size that this implementation can safely handle. We guard
against anything larger than that, assuming the worst case with 5 bytes per
pixel, rounded down to a nice clean value. 400 million pixels ought to be
enough for anybody. */
#define QOI_PIXELS_MAX ((unsigned int)400000000)

typedef union {
	struct { unsigned char r, g, b, a; } rgba;
	unsigned int v;
} qoi_rgba_t;

static const unsigned char qoi_padding[8] = { 0,0,0,0,0,0,0,1 };

void qoi_write_16(unsigned char* bytes, int* p, unsigned int v) {
	bytes[(*p)++] = (0x0000ff00 & v) >> 8;
	bytes[(*p)++] = (0x000000ff & v);
}

void qoi_write_24(unsigned char* bytes, int* p, unsigned int v) {
	bytes[(*p)++] = (0x00ff0000 & v) >> 16;
	bytes[(*p)++] = (0x0000ff00 & v) >> 8;
	bytes[(*p)++] = (0x000000ff & v);
}

void qoi_write_32(unsigned char* bytes, int* p, unsigned int v) {
	bytes[(*p)++] = (0xff000000 & v) >> 24;
	bytes[(*p)++] = (0x00ff0000 & v) >> 16;
	bytes[(*p)++] = (0x0000ff00 & v) >> 8;
	bytes[(*p)++] = (0x000000ff & v);
}

unsigned int qoi_read_16(const unsigned char* bytes, int* p) {
	unsigned int c = bytes[(*p)++];
	unsigned int d = bytes[(*p)++];
	return c << 8 | d;
}

unsigned int qoi_read_24(const unsigned char* bytes, int* p) {
	unsigned int b = bytes[(*p)++];
	unsigned int c = bytes[(*p)++];
	unsigned int d = bytes[(*p)++];
	return b << 16 | c << 8 | d;
}

unsigned int qoi_read_32(const unsigned char* bytes, int* p) {
	unsigned int a = bytes[(*p)++];
	unsigned int b = bytes[(*p)++];
	unsigned int c = bytes[(*p)++];
	unsigned int d = bytes[(*p)++];
	return a << 24 | b << 16 | c << 8 | d;
}

__forceinline void qoi_write_32_histo(unsigned char* bytes, int* p, unsigned int* histo, unsigned int v) {
	bytes[(*p)++] = (0xff000000 & v) >> 24;
	bytes[(*p)++] = (0x00ff0000 & v) >> 16;
	bytes[(*p)++] = (0x0000ff00 & v) >> 8;
	bytes[(*p)++] = (0x000000ff & v);

	histo[(0xff000000 & v) >> 24]++;
	histo[(0x00ff0000 & v) >> 16]++;
	histo[(0x0000ff00 & v) >> 8]++;
	histo[(0x000000ff & v)]++;
}

__forceinline void qoi_write_8_histo(unsigned char* bytes, int* p, unsigned int* histo, unsigned char v) {
	bytes[(*p)++] = v;
	histo[v]++;
}


void* qoi_encode(const void* data, const qoi_desc* desc, int* out_len) {
	int i, max_size, p, run;
	int px_len, px_end, px_pos, channels;
	unsigned char* bytes;
	const unsigned char* pixels;
	qoi_rgba_t index[64];
	qoi_rgba_t px, px_prev;

	if (
		data == NULL || out_len == NULL || desc == NULL ||
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 1 ||
		desc->height >= QOI_PIXELS_MAX / desc->width
		) {
		return NULL;
	}

	max_size =
		desc->width * desc->height * (desc->channels + 1) +
		QOI_HEADER_SIZE + sizeof(qoi_padding);

	p = 0;
	bytes = (unsigned char*)QOI_MALLOC(max_size);
	if (!bytes) {
		return NULL;
	}

	qoi_write_32(bytes, &p, QOI_MAGIC);
	qoi_write_32(bytes, &p, desc->width);
	qoi_write_32(bytes, &p, desc->height);
	bytes[p++] = desc->channels;
	bytes[p++] = desc->colorspace;


	pixels = (const unsigned char*)data;

	QOI_ZEROARR(index);

	run = 0;
	px_prev.rgba.r = 0;
	px_prev.rgba.g = 0;
	px_prev.rgba.b = 0;
	px_prev.rgba.a = 255;
	px = px_prev;

	px_len = desc->width * desc->height * desc->channels;
	px_end = px_len - desc->channels;
	channels = desc->channels;

	for (px_pos = 0; px_pos < px_len; px_pos += channels) {
		if (channels == 4) {
			px = *(qoi_rgba_t*)(pixels + px_pos);
		}
		else {
			px.rgba.r = pixels[px_pos + 0];
			px.rgba.g = pixels[px_pos + 1];
			px.rgba.b = pixels[px_pos + 2];
		}

		if (px.v == px_prev.v) {
			run++;
			if (run == 62 || px_pos == px_end) {
				bytes[p++] = QOI_OP_RUN | (run - 1);
				run = 0;
			}
		}
		else {
			int index_pos;

			if (run > 0) {
				bytes[p++] = QOI_OP_RUN | (run - 1);
				run = 0;
			}

			index_pos = QOI_COLOR_HASH(px) % 64;

			if (index[index_pos].v == px.v) {
				bytes[p++] = QOI_OP_INDEX | index_pos;
			}
			else {
				index[index_pos] = px;

				if (px.rgba.a == px_prev.rgba.a) {
					signed char vr = px.rgba.r - px_prev.rgba.r;
					signed char vg = px.rgba.g - px_prev.rgba.g;
					signed char vb = px.rgba.b - px_prev.rgba.b;

					signed char vg_r = vr - vg;
					signed char vg_b = vb - vg;

					if (
						vr > -3 && vr < 2 &&
						vg > -3 && vg < 2 &&
						vb > -3 && vb < 2
						) {
						bytes[p++] = QOI_OP_DIFF | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2);
					}
					else if (
						vg_r > -9 && vg_r <  8 &&
						vg   > -33 && vg   < 32 &&
						vg_b >  -9 && vg_b < 8
						) {
						bytes[p++] = QOI_OP_LUMA | (vg + 32);
						bytes[p++] = (vg_r + 8) << 4 | (vg_b + 8);
					}
					else {
						bytes[p++] = QOI_OP_RGB;
						bytes[p++] = px.rgba.r;
						bytes[p++] = px.rgba.g;
						bytes[p++] = px.rgba.b;
					}
				}
				else {
					bytes[p++] = QOI_OP_RGBA;
					bytes[p++] = px.rgba.r;
					bytes[p++] = px.rgba.g;
					bytes[p++] = px.rgba.b;
					bytes[p++] = px.rgba.a;
				}
			}
		}
		px_prev = px;
	}

	for (i = 0; i < (int)sizeof(qoi_padding); i++) {
		bytes[p++] = qoi_padding[i];
	}

	*out_len = p;
	return bytes;
}

void* qoi_decode(const void* data, int size, qoi_desc* desc, int channels) {
	const unsigned char* bytes;
	unsigned int header_magic;
	unsigned char* pixels;
	qoi_rgba_t index[64];
	qoi_rgba_t px;
	int px_len, chunks_len, px_pos;
	int p = 0, run = 0;

	if (
		data == NULL || desc == NULL ||
		(channels != 0 && channels != 3 && channels != 4) ||
		size < QOI_HEADER_SIZE + (int)sizeof(qoi_padding)
		) {
		return NULL;
	}

	bytes = (const unsigned char*)data;

	header_magic = qoi_read_32(bytes, &p);
	desc->width = qoi_read_32(bytes, &p);
	desc->height = qoi_read_32(bytes, &p);
	desc->channels = bytes[p++];
	desc->colorspace = bytes[p++];

	if (
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 1 ||
		header_magic != QOI_MAGIC ||
		desc->height >= QOI_PIXELS_MAX / desc->width
		) {
		return NULL;
	}

	if (channels == 0) {
		channels = desc->channels;
	}

	px_len = desc->width * desc->height * channels;
	pixels = (unsigned char*)QOI_MALLOC(px_len);
	if (!pixels) {
		return NULL;
	}

	QOI_ZEROARR(index);
	px.rgba.r = 0;
	px.rgba.g = 0;
	px.rgba.b = 0;
	px.rgba.a = 255;

	chunks_len = size - (int)sizeof(qoi_padding);
	for (px_pos = 0; px_pos < px_len; px_pos += channels) {
		if (run > 0) {
			run--;
		}
		else if (p < chunks_len) {
			int b1 = bytes[p++];

			if (b1 == QOI_OP_RGB) {
				px.rgba.r = bytes[p++];
				px.rgba.g = bytes[p++];
				px.rgba.b = bytes[p++];
			}
			else if (b1 == QOI_OP_RGBA) {
				px.rgba.r = bytes[p++];
				px.rgba.g = bytes[p++];
				px.rgba.b = bytes[p++];
				px.rgba.a = bytes[p++];
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
				px = index[b1];
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
				px.rgba.r += ((b1 >> 4) & 0x03) - 2;
				px.rgba.g += ((b1 >> 2) & 0x03) - 2;
				px.rgba.b += (b1 & 0x03) - 2;
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
				int b2 = bytes[p++];
				int vg = (b1 & 0x3f) - 32;
				px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
				px.rgba.g += vg;
				px.rgba.b += vg - 8 + (b2 & 0x0f);
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
				run = (b1 & 0x3f);
			}

			index[QOI_COLOR_HASH(px) % 64] = px;
		}

		if (channels == 4) {
			*(qoi_rgba_t*)(pixels + px_pos) = px;
		}
		else {
			pixels[px_pos + 0] = px.rgba.r;
			pixels[px_pos + 1] = px.rgba.g;
			pixels[px_pos + 2] = px.rgba.b;
		}
	}

	return pixels;
}




typedef struct {
	unsigned int count;
	short left;
	short right;
} encoding_tree_t;

typedef struct {
	unsigned int count;
	unsigned int bits;
	unsigned char len;
} encoding_table_t;


__forceinline void swap_min_heap(unsigned short* heap, unsigned short a, unsigned short b) {
	unsigned short temp = heap[a];
	heap[a] = heap[b];
	heap[b] = temp;
}

void insert_min_heap(unsigned short* heap, int* heapSize, encoding_tree_t* tree, unsigned short item) {
	heap[*heapSize] = item;
	int idx = *heapSize;
	(*heapSize)++;

	assert(*heapSize <= 256);

	// Heapify up
	while (idx) {
		int upper = (idx - 1) / 2;
		if (tree[heap[upper]].count < tree[heap[idx]].count) {
			break;
		}

		swap_min_heap(heap, idx, upper);

		idx = upper;
	}
}

unsigned short pop_min_heap(unsigned short heap[], int* heapSize, encoding_tree_t* tree) {
	assert(*heapSize > 0);

	unsigned short item = heap[0];
	(*heapSize)--;

	if (!*heapSize) {
		return item;
	}

	// Move the last element to the top
	heap[0] = heap[*heapSize];

	// Heapify down
	unsigned short idx = 0;
	while (idx * 2 + 1 < *heapSize) {
		unsigned short cur = heap[idx];
		unsigned short idxLeft  = idx * 2 + 1;
		unsigned short idxRight = (idx+1) * 2;
		unsigned short left = heap[idxLeft];

		if (idxRight < *heapSize) {
			unsigned short right = heap[idxRight];

			// Smaller than left and right -> Stop
			if (tree[cur].count < tree[left].count && tree[cur].count < tree[right].count) {
				break;
			}

			// Swap with the smaller child
			if (tree[right].count < tree[left].count) {
				swap_min_heap(heap, idxRight, idx);
				idx = idxRight;
			}
			else {
				swap_min_heap(heap, idxLeft, idx);
				idx = idxLeft;
			}
		}
		else {
			// Smaller than only left child -> Swap
			if (tree[left].count < tree[cur].count) {
				swap_min_heap(heap, idxLeft, idx);
			}
			break;
		}
	}

	return item;
}


void make_encoded_word( encoding_tree_t* tree, encoding_table_t* table, unsigned short node, unsigned int bits, unsigned char len ) {
	assert(node >= 0);

	if (node < 256) {
		table[node] = (encoding_table_t){
			.count = tree[node].count,
			.bits = bits,
			.len = len
		};
		return;
	}

	make_encoded_word(tree, table, tree[node].left, bits, len + 1);
	make_encoded_word(tree, table, tree[node].right, bits | (1 << len), len + 1);
}



void* qoi_huff_encode(const void* data, const qoi_desc* desc, int* out_len) {
	int i, max_size, p, run;
	int px_len, px_end, px_pos, channels;
	unsigned char* bytes;
	const unsigned char* pixels;
	qoi_rgba_t index[64];
	qoi_rgba_t px, px_prev;

	unsigned int histo[256];
	memset(&histo, 0, 256* sizeof(unsigned int));

	if (
		data == NULL || out_len == NULL || desc == NULL ||
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 1 ||
		desc->height >= QOI_PIXELS_MAX / desc->width
		) {
		return NULL;
	}

	max_size =
		desc->width * desc->height * (desc->channels + 1) +
		QOI_HEADER_SIZE + sizeof(qoi_padding);

	p = 0;
	bytes = (unsigned char*)QOI_MALLOC(max_size);
	if (!bytes) {
		return NULL;
	}

	qoi_write_32(bytes, &p, QOI_MAGIC);
	qoi_write_32(bytes, &p, desc->width);
	qoi_write_32(bytes, &p, desc->height);
	bytes[p++] = desc->channels;
	bytes[p++] = desc->colorspace & ~QOI_HUFF_ENCODED_BIT;

	pixels = (const unsigned char*)data;

	QOI_ZEROARR(index);

	run = 0;
	px_prev.rgba.r = 0;
	px_prev.rgba.g = 0;
	px_prev.rgba.b = 0;
	px_prev.rgba.a = 255;
	px = px_prev;

	px_len = desc->width * desc->height * desc->channels;
	px_end = px_len - desc->channels;
	channels = desc->channels;

	for (px_pos = 0; px_pos < px_len; px_pos += channels) {
		if (channels == 4) {
			px = *(qoi_rgba_t*)(pixels + px_pos);
		}
		else {
			px.rgba.r = pixels[px_pos + 0];
			px.rgba.g = pixels[px_pos + 1];
			px.rgba.b = pixels[px_pos + 2];
		}

		if (px.v == px_prev.v) {
			run++;
			if (run == 62 || px_pos == px_end) {
				qoi_write_8_histo(bytes, &p, histo, QOI_OP_RUN | (run - 1));
				run = 0;
			}
		}
		else {
			int index_pos;

			if (run > 0) {
				qoi_write_8_histo(bytes, &p, histo, QOI_OP_RUN | (run - 1));
				run = 0;
			}

			index_pos = QOI_COLOR_HASH(px) % 64;

			if (index[index_pos].v == px.v) {
				qoi_write_8_histo(bytes, &p, histo, QOI_OP_INDEX | index_pos);
			}
			else {
				index[index_pos] = px;

				if (px.rgba.a == px_prev.rgba.a) {
					signed char vr = px.rgba.r - px_prev.rgba.r;
					signed char vg = px.rgba.g - px_prev.rgba.g;
					signed char vb = px.rgba.b - px_prev.rgba.b;

					signed char vg_r = vr - vg;
					signed char vg_b = vb - vg;

					if (
						vr > -3 && vr < 2 &&
						vg > -3 && vg < 2 &&
						vb > -3 && vb < 2
						) {
						qoi_write_8_histo(bytes, &p, histo, QOI_OP_DIFF | (vr + 2) << 4 | (vg + 2) << 2 | (vb + 2));
					}
					else if (
						vg_r > -9 && vg_r <  8 &&
						vg   > -33 && vg   < 32 &&
						vg_b >  -9 && vg_b < 8
						) {
						qoi_write_8_histo(bytes, &p, histo, QOI_OP_LUMA | (vg + 32));
						qoi_write_8_histo(bytes, &p, histo, (vg_r + 8) << 4 | (vg_b + 8));
					}
					else {
						qoi_write_8_histo(bytes, &p, histo, QOI_OP_RGB);
						qoi_write_8_histo(bytes, &p, histo, px.rgba.r);
						qoi_write_8_histo(bytes, &p, histo, px.rgba.g);
						qoi_write_8_histo(bytes, &p, histo, px.rgba.b);
					}
				}
				else {
					qoi_write_8_histo(bytes, &p, histo, QOI_OP_RGBA);
					qoi_write_8_histo(bytes, &p, histo, px.rgba.r);
					qoi_write_8_histo(bytes, &p, histo, px.rgba.g);
					qoi_write_8_histo(bytes, &p, histo, px.rgba.b);
					qoi_write_8_histo(bytes, &p, histo, px.rgba.a);
				}
			}
		}
		px_prev = px;
	}

	for (i = 0; i < (int)sizeof(qoi_padding); i++) {
		qoi_write_8_histo(bytes, &p, histo, qoi_padding[i]);
	}

	unsigned int qoiByteCount= *out_len = p;

	encoding_table_t table[256];

	{
		encoding_tree_t tree[512];
		unsigned short nextFreeNode = 256;

		unsigned short minHeap[256];
		int heapSize = 0;

		memset(minHeap, -1, sizeof(short) * 256);

		for (int i = 0; i != 256; i++) {
			tree[i] = (encoding_tree_t){
				.count = histo[i],
				.left = -1,
				.right = -1
			};

			insert_min_heap(minHeap, &heapSize, tree, i);
		}

		int j = 0;
		while (heapSize > 1) {
			short left = pop_min_heap(minHeap, &heapSize, tree);
			short right = pop_min_heap(minHeap, &heapSize, tree);

			if (tree[left].count > tree[right].count) {
				printf("inv");
			}

			tree[nextFreeNode] = (encoding_tree_t){
				.count = tree[left].count + tree[right].count,
				.left = left,
				.right = right
			};

			insert_min_heap(minHeap, &heapSize, tree, nextFreeNode);
			nextFreeNode++;
		}


		make_encoded_word(tree, table, pop_min_heap(minHeap, &heapSize, tree), 0, 0);
	}

	/*for (int i = 0; i != 256; i++) {
		printf("- %d: (%d) ", i, table[i].len);

		int j = table[i].len;
		while (j--) {
			printf("%c", (table[i].bits& (1 << j)) ? '1' : '0');
		}

		printf(" (%04x)\n", table[i].bits);
	}*/


	{
		unsigned int stats[20];
		memset(stats, 0, sizeof(stats));
		for (int i = 0; i != 256; i++) {
			stats[table[i].len] ++;
		}

		/*for (int i = 0; i != 20; i++) {
			if (stats[i]) {
				printf("- %d bits: %d \n", i, stats[i]);
			}
		}*/
	}

	// Maximum dictonary size
	unsigned int expectedSize = (1024 + 256) * 8;
	for (int i = 0; i != 256; i++) {
		expectedSize += table[i].count * table[i].len;

		// Words with more than 32 bits cannot be coded
		if (table[i].len > QOI_HUFF_MAX_BIT_NUM) {
			//printf("Words with more than 32 bits (%d) cannot be coded\n", table[i].len);
			return bytes;
		}
	}
	expectedSize /= 8;

	// Don't bother, we won't even save 3% space
	if (expectedSize > 10 * 1024 && expectedSize > p * 0.97) {
		//printf("Don't bother, we won't even save 3%% space\n");
		return bytes;
	}

	expectedSize = expectedSize + (4 - expectedSize % 4) % 4 + 8;
	unsigned int *huffwords = (unsigned int*) malloc( expectedSize );
	if (!huffwords) {
		return NULL;
	}

	p = 0;
	unsigned char* huffbytes = (unsigned char*)huffwords;
	qoi_write_32(huffbytes, &p, QOI_MAGIC);
	qoi_write_32(huffbytes, &p, desc->width);
	qoi_write_32(huffbytes, &p, desc->height);
	huffbytes[p++] = desc->channels;
	huffbytes[p++] = desc->colorspace | QOI_HUFF_ENCODED_BIT;

	for (int i = 0; i != 256; i++) {
		huffbytes[p++] = table[i].len;
		if (table[i].len > 24) {
			qoi_write_32(huffbytes, &p, table[i].bits);
		}
		else if(table[i].len > 16) {
			qoi_write_24(huffbytes, &p, table[i].bits);
		}
		else {
			qoi_write_16(huffbytes, &p, table[i].bits);
		}
	}


	// Round up to the next full word boundry
	unsigned int wordIdx = (p / 4) + (p % 4 ? 1 : 0); unsigned int huffOffset = wordIdx * 4;
	unsigned int bitIdx = 0;
	huffwords[wordIdx] = 0;
	for (int i = QOI_HEADER_SIZE; i != qoiByteCount; i++) {
		unsigned int bits= table[bytes[i]].bits;
		unsigned int len= table[bytes[i]].len;

		huffwords[wordIdx] |= (bits << bitIdx) & ~( (~0) << 32 );

		unsigned int newBitIdx= bitIdx + len;

		bitIdx = newBitIdx % 32;

		// Move to the next word and clear it
		if (newBitIdx >= 32) {
			huffwords[++wordIdx] = 0;
		}

		// Store the overflown bits in the new word
		if (newBitIdx > 32) {
			huffwords[wordIdx] |= (bits >> (len- bitIdx));
		}
	}


	//for (int i = 0; i != 64; i++) {
	//	printf("huffbyte [%d]: %x\n", i, huffbytes[i+ huffOffset]);
	//}

	huffwords[++wordIdx] = 0;
	*out_len = wordIdx * 4 + 4;
	//printf("size: %d\n", *out_len);

	free(bytes);

	return huffbytes;
}






void qoi_decode_buffer( const unsigned char* bytes, int p, unsigned char* pixels, int size, int px_len, int channels ) {
	int chunks_len, px_pos;
	int run = 0;
	qoi_rgba_t index[64];
	qoi_rgba_t px;

	QOI_ZEROARR(index);
	px.rgba.r = 0;
	px.rgba.g = 0;
	px.rgba.b = 0;
	px.rgba.a = 255;

	chunks_len = size - (int)sizeof(qoi_padding);
	for (px_pos = 0; px_pos < px_len; px_pos += channels) {
		if (run > 0) {
			run--;
		}
		else if (p < chunks_len) {
			int b1 = bytes[p++];

			if (b1 == QOI_OP_RGB) {
				px.rgba.r = bytes[p++];
				px.rgba.g = bytes[p++];
				px.rgba.b = bytes[p++];
			}
			else if (b1 == QOI_OP_RGBA) {
				px.rgba.r = bytes[p++];
				px.rgba.g = bytes[p++];
				px.rgba.b = bytes[p++];
				px.rgba.a = bytes[p++];
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
				px = index[b1];
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
				px.rgba.r += ((b1 >> 4) & 0x03) - 2;
				px.rgba.g += ((b1 >> 2) & 0x03) - 2;
				px.rgba.b += (b1 & 0x03) - 2;
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
				int b2 = bytes[p++];
				int vg = (b1 & 0x3f) - 32;
				px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
				px.rgba.g += vg;
				px.rgba.b += vg - 8 + (b2 & 0x0f);
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
				run = (b1 & 0x3f);
			}

			index[QOI_COLOR_HASH(px) % 64] = px;
		}

		if (channels == 4) {
			*(qoi_rgba_t*)(pixels + px_pos) = px;
		}
		else {
			pixels[px_pos + 0] = px.rgba.r;
			pixels[px_pos + 1] = px.rgba.g;
			pixels[px_pos + 2] = px.rgba.b;
		}
	}
}

typedef struct {
	union {
		struct {
			//unsigned int bits;
			unsigned char len;
			unsigned char byteValue;
		} leaf;

		struct {
			short left;
			short right;
		} node;
	} data;
	unsigned char isLeaf;
} decoding_tree_t;

__forceinline unsigned char huff_decode_next_byte(const unsigned int* words, unsigned int* wordIdx, unsigned int* bitIdx, int size, const unsigned short* table, const decoding_tree_t* tree) {
	if (*wordIdx + 1 >= size / 4) {
		//printf("unexpected eof\n", size);
		return 0;
	}

	uint64_t bits = *((uint64_t*)(words + *wordIdx));
	bits = bits >> *bitIdx;

	unsigned int leadingBits = (bits & ~((~0) << QOI_HUFF_DECODING_TABLE_WIDTH));
	unsigned int trailingBits = bits >> QOI_HUFF_DECODING_TABLE_WIDTH;

	unsigned char byteValue;
	unsigned int len;

	// Table entry is either the decoded value or a decision tree
	unsigned short entry = table[leadingBits];
	assert(entry);
	if (!(entry & (1 << 15))) {
		byteValue= entry & 0xff;
		len = entry >> 8;
	}
	else {
		// Walk the tree until decoded value is found
		unsigned short node = entry & ~(1 << 15);
		while (!tree[node].isLeaf) {
			node = trailingBits & 1 ? tree[node].data.node.right : tree[node].data.node.left;
			trailingBits = trailingBits >> 1;
		}

		byteValue = tree[node].data.leaf.byteValue;
		len = tree[node].data.leaf.len;
	}

	// Move to next word on overflow
	*bitIdx += len;
	if (*bitIdx >= 32) {
		(*wordIdx)++;
	}
	*bitIdx %= 32;

	return byteValue;
}

int qoi_huff_decode_buffer(const unsigned char* bytes, int p, unsigned char* pixels, int size, int px_len, int channels) {
	int px_pos;
	int run = 0;
	qoi_rgba_t index[64];
	qoi_rgba_t px;
	
	unsigned short table[1 << QOI_HUFF_DECODING_TABLE_WIDTH];		// 11bit lookup table
	decoding_tree_t treeNodes[512];									// decision tree nodes
	int nextFreeNode= 0;											// basic arena allocator


	QOI_ZEROARR(table);

	
	// Read 256 dictonary entries
	for (int byteValue = 0; byteValue != 256; byteValue++) {
		// Get word length and word bits
		if (p >= size) {
			return -1;
		}
		unsigned len = bytes[p++];
		unsigned int bits;
		if (len > 24) {
			if (p + 4 > size) {
				return -1;
			}

			bits = qoi_read_32(bytes, &p);
		}
		else if (len > 16) {
			if (p + 3 > size) {
				return -1;
			}

			bits = qoi_read_24(bytes, &p);

		}
		else {
			if (p + 2 > size) {
				return -1;
			}

			bits = qoi_read_16(bytes, &p);
		}

		// Make entry for short code words
		if (len <= QOI_HUFF_DECODING_TABLE_WIDTH) {
			unsigned int paddingCount = QOI_HUFF_DECODING_TABLE_WIDTH - len;
			unsigned int spanLength = 1 << paddingCount;

			for (int i = 0; i != spanLength; i++) {
				unsigned int idx = (i << len) | bits;

				assert(table[idx] == 0);
				table[idx] = ((len & 0x3f) << 8) | byteValue;
			}
		}
		// Make a lookup tree for long code words
		else {
			unsigned int leadingLength = len - QOI_HUFF_DECODING_TABLE_WIDTH;
			unsigned int truncatedBits = bits & ~( (~0) << QOI_HUFF_DECODING_TABLE_WIDTH);
			unsigned int leadingBits = bits >> QOI_HUFF_DECODING_TABLE_WIDTH;


			// Allocate new root node
			unsigned short treeIdx = table[truncatedBits];
			assert(treeIdx == 0 || treeIdx & (1 << 15));
			if (!treeIdx) {
				treeIdx = nextFreeNode++;
				table[truncatedBits] = treeIdx | (1 << 15);

				treeNodes[treeIdx] = (decoding_tree_t){
					.data = {
						.node = {
							.left= -1,
							.right= -1
						}
					},
					.isLeaf = 0
				};
			}
			// Get root node index
			else {
				treeIdx = treeIdx & ~(1 << 15);
			}

			// Build / Walk tree for all remaining leading bits that don't fit into the 11bit table index
			for (int i = 0; i != leadingLength; i++) {
				unsigned int bit = leadingBits & 1;
				leadingBits >>= 1;

				// Create leaf node at the end of the tree
				if (i == leadingLength - 1) {
					// Alloc node
					short entryIdx = nextFreeNode++;
					treeNodes[entryIdx] = (decoding_tree_t){
						.data = {
							.leaf = {
								//.bits = bits,
								.len = (unsigned char)len,
								.byteValue = (unsigned char)byteValue
							}
						},
						.isLeaf = 1
					};

					// Attach to the tree
					if (bit) {
						assert(!treeNodes[treeIdx].isLeaf && treeNodes[treeIdx].data.node.right < 0);
						treeNodes[treeIdx].data.node.right = entryIdx;
					}
					else {
						assert(!treeNodes[treeIdx].isLeaf && treeNodes[treeIdx].data.node.left < 0);
						treeNodes[treeIdx].data.node.left = entryIdx;
					}
				}
				// Move to the next node down
				else {
					// Allocate new node if none is present yet
					assert(!treeNodes[treeIdx].isLeaf);
					short nextNode = bit ? treeNodes[treeIdx].data.node.right : treeNodes[treeIdx].data.node.left;
					if (nextNode < 0) {
						nextNode = nextFreeNode++;
						treeNodes[nextNode] = (decoding_tree_t){
							.data = {
								.node = {
									.left= -1,
									.right= -1
								}
							},
							.isLeaf = 0
						};

						// Attach newly allocated node
						if (bit) {
							assert(treeNodes[treeIdx].data.node.right < 0);
							treeNodes[treeIdx].data.node.right = nextNode;
						}
						else {
							assert(treeNodes[treeIdx].data.node.left < 0);
							treeNodes[treeIdx].data.node.left = nextNode;
						}
					}

					treeIdx = nextNode;
				}
			}
		}
	}

	const unsigned int* words = (const unsigned int*)bytes;
	unsigned int wordIdx = (p / 4) + (p % 4 ? 1 : 0);
	unsigned int bitIdx = 0;

	// Decode qoi from the stram of huff decoded bytes
	QOI_ZEROARR(index);
	px.rgba.r = 0;
	px.rgba.g = 0;
	px.rgba.b = 0;
	px.rgba.a = 255;

	for (px_pos = 0; px_pos < px_len; px_pos += channels) {
		if (run > 0) {
			run--;
		}
		else {
			int b1 = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
			if (wordIdx * 4 >= size) {
				continue;
			}

			if (b1 == QOI_OP_RGB) {
				px.rgba.r = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
				px.rgba.g = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
				px.rgba.b = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
			}
			else if (b1 == QOI_OP_RGBA) {
				px.rgba.r = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
				px.rgba.g = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
				px.rgba.b = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
				px.rgba.a = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_INDEX) {
				px = index[b1];
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_DIFF) {
				px.rgba.r += ((b1 >> 4) & 0x03) - 2;
				px.rgba.g += ((b1 >> 2) & 0x03) - 2;
				px.rgba.b += (b1 & 0x03) - 2;
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_LUMA) {
				int b2 = huff_decode_next_byte(words, &wordIdx, &bitIdx, size, table, treeNodes);
				int vg = (b1 & 0x3f) - 32;
				px.rgba.r += vg - 8 + ((b2 >> 4) & 0x0f);
				px.rgba.g += vg;
				px.rgba.b += vg - 8 + (b2 & 0x0f);
			}
			else if ((b1 & QOI_MASK_2) == QOI_OP_RUN) {
				run = (b1 & 0x3f);
			}

			index[QOI_COLOR_HASH(px) % 64] = px;
		}

		if (channels == 4) {
			*(qoi_rgba_t*)(pixels + px_pos) = px;
		}
		else {
			pixels[px_pos + 0] = px.rgba.r;
			pixels[px_pos + 1] = px.rgba.g;
			pixels[px_pos + 2] = px.rgba.b;
		}
	}

	return 0;
}



void* qoi_huff_decode(const void* data, int size, qoi_desc* desc, int channels) {
	const unsigned char* bytes;
	unsigned int header_magic;
	unsigned char* pixels;
	int p = 0;

	if (
		data == NULL || desc == NULL ||
		(channels != 0 && channels != 3 && channels != 4) ||
		size < QOI_HEADER_SIZE + (int)sizeof(qoi_padding)
		) {
		return NULL;
	}

	bytes = (const unsigned char*)data;

	header_magic = qoi_read_32(bytes, &p);
	desc->width = qoi_read_32(bytes, &p);
	desc->height = qoi_read_32(bytes, &p);
	desc->channels = bytes[p++];
	desc->colorspace = bytes[p++];

	int isHuffEncoded = desc->colorspace & QOI_HUFF_ENCODED_BIT;
	desc->colorspace &= ~QOI_HUFF_ENCODED_BIT;

	if (
		desc->width == 0 || desc->height == 0 ||
		desc->channels < 3 || desc->channels > 4 ||
		desc->colorspace > 1 ||
		header_magic != QOI_MAGIC ||
		desc->height >= QOI_PIXELS_MAX / desc->width
		) {
		return NULL;
	}

	if (channels == 0) {
		channels = desc->channels;
	}

	int px_len = desc->width * desc->height * channels;
	pixels = (unsigned char*)QOI_MALLOC(px_len);
	if (!pixels) {
		return NULL;
	}

	if (!isHuffEncoded) {
		qoi_decode_buffer( bytes, p, pixels, size, px_len, channels );
		return pixels;
	}

	qoi_huff_decode_buffer(bytes, p, pixels, size, px_len, channels);
	return pixels;
}




#ifndef QOI_NO_STDIO
#include <stdio.h>

int qoi_write(const char* filename, const void* data, const qoi_desc* desc) {
	FILE* f = fopen(filename, "wb");
	int size;
	void* encoded;

	if (!f) {
		return 0;
	}

	encoded = qoi_encode(data, desc, &size);
	if (!encoded) {
		fclose(f);
		return 0;
	}

	fwrite(encoded, 1, size, f);
	fclose(f);

	QOI_FREE(encoded);
	return size;
}

void* qoi_read(const char* filename, qoi_desc* desc, int channels) {
	FILE* f = fopen(filename, "rb");
	int size, bytes_read;
	void* pixels, * data;

	if (!f) {
		return NULL;
	}

	fseek(f, 0, SEEK_END);
	size = ftell(f);
	if (size <= 0) {
		fclose(f);
		return NULL;
	}
	fseek(f, 0, SEEK_SET);

	data = QOI_MALLOC(size);
	if (!data) {
		fclose(f);
		return NULL;
	}

	bytes_read = fread(data, 1, size, f);
	fclose(f);

	pixels = qoi_decode(data, bytes_read, desc, channels);
	QOI_FREE(data);
	return pixels;
}

#endif /* QOI_NO_STDIO */
#endif /* QOI_IMPLEMENTATION */
