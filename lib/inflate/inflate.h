/*
 * zlib-deflate-nostdlib
 *
 * Copyright 2021 Birte Kristina Friesel
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stdint.h>
#define DEFLATE_WITH_LUT

#define DEFLATE_ERR_INPUT_LENGTH (-1)
#define DEFLATE_ERR_METHOD (-2)
#define DEFLATE_ERR_FDICT (-3)
#define DEFLATE_ERR_BLOCK (-4)
#define DEFLATE_ERR_CHECKSUM (-5)
#define DEFLATE_ERR_OUTPUT_LENGTH (-6)
#define DEFLATE_ERR_FCHECK (-7)
#define DEFLATE_ERR_NLEN (-8)
#define DEFLATE_ERR_HUFFMAN (-9)

int64_t inflate(const unsigned char *input_buf, uint64_t input_len,
		unsigned char *output_buf, uint64_t output_len);
int64_t inflate_zlib(const unsigned char *input_buf, uint64_t input_len,
		     unsigned char *output_buf, uint64_t output_len);
