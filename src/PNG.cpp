/*
  Copyright (c) 2015, Matthias Schiffer <mschiffer@universe-factory.net>
  All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice,
       this list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


#include "PNG.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <memory>
#include <system_error>

#include <png.h>


namespace MinedMap {
namespace PNG {

void write(const char *filename, const uint8_t *data, size_t width, size_t height, bool colored) {
	std::FILE *f = std::fopen(filename, "wb");
	if (!f)
		throw std::system_error(errno, std::generic_category(), "unable to open PNG file");

	png_structp png_ptr = png_create_write_struct (PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr)
		throw std::runtime_error("unable to create PNG write struct");

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_write_struct(&png_ptr, nullptr);
		throw std::runtime_error("unable to create PNG info struct");
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_write_struct(&png_ptr, &info_ptr);
		std::fclose(f);
		throw std::runtime_error("unable to write PNG file");
	}

	png_init_io(png_ptr, f);

	png_set_IHDR(png_ptr, info_ptr, width, height, 8, colored ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_GRAY_ALPHA,
		     PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT, PNG_FILTER_TYPE_DEFAULT);

	uint8_t *row_pointers[height];
	for (size_t i = 0; i < height; i++)
		row_pointers[i] = const_cast<uint8_t*>(&data[(colored ? 4 : 2)*i*width]);

	png_set_rows(png_ptr, info_ptr, row_pointers);
	png_write_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);

	png_destroy_write_struct(&png_ptr, &info_ptr);
	std::fclose(f);
}

void read(const char *filename, uint8_t *data, size_t width, size_t height, bool colored) {
	std::FILE *f = std::fopen(filename, "rb");
	if (!f)
		throw std::system_error(errno, std::generic_category(), "unable to open PNG file");

	png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);
	if (!png_ptr)
		throw std::runtime_error("unable to create PNG read struct");

	png_infop info_ptr = png_create_info_struct(png_ptr);
	if (!info_ptr) {
		png_destroy_read_struct(&png_ptr, nullptr, nullptr);
		throw std::runtime_error("unable to create PNG info struct");
	}

	png_infop end_info = png_create_info_struct(png_ptr);
	if (!end_info) {
		png_destroy_read_struct(&png_ptr, &info_ptr, nullptr);
		throw std::runtime_error("unable to create PNG info struct");
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
		fclose(f);
		throw std::runtime_error("unable to read PNG file");
	}

	png_init_io(png_ptr, f);

	png_read_png(png_ptr, info_ptr, PNG_TRANSFORM_IDENTITY, nullptr);

	if (png_get_image_width(png_ptr, info_ptr) != width
	    || png_get_image_height(png_ptr, info_ptr) != height
	    || png_get_bit_depth(png_ptr, info_ptr) != 8
	    || png_get_color_type(png_ptr, info_ptr) != (colored ? PNG_COLOR_TYPE_RGB_ALPHA : PNG_COLOR_TYPE_GRAY_ALPHA))
		longjmp(png_jmpbuf(png_ptr), 1);

	uint8_t **row_pointers = png_get_rows(png_ptr, info_ptr);
	for (size_t i = 0; i < height; i++)
		std::memcpy(&data[(colored ? 4 : 2)*i*width], row_pointers[i], (colored ? 4 : 2)*width);

	png_destroy_read_struct(&png_ptr, &info_ptr, &end_info);
	std::fclose(f);
}

static void readScaled(uint8_t *data, size_t offset_w, size_t offset_h, const char *file, size_t width, size_t height, bool colored) {
	if (!file)
		return;

	size_t b = (colored ? 4 : 2);

	std::unique_ptr<uint8_t[]> input(new uint8_t[b*width*height]);
	read(file, input.get(), width, height, colored);

	for (size_t h = 0; h < width/2; h++) {
		for (size_t w = 0; w < width/2; w++) {
			for (size_t c = 0; c < b; c++) {
				size_t i = 2*b*(width*h + w) + c;
				data[b*(width*(offset_h+h) + offset_w+w) + c] = (input[i] + input[i+b] + input[i+b*width] + input[i+b*width+b])/4;
			}
		}
	}
}

void mipmap(const char *output, size_t width, size_t height, bool colored, const char *nw, const char *ne, const char *sw, const char *se) {
	size_t size = (colored ? 4 : 2)*width*height;
	std::unique_ptr<uint8_t[]> data(new uint8_t[size]);
	std::memset(data.get(), 0, size);

	readScaled(data.get(), 0, 0, nw, width, height, colored);
	readScaled(data.get(), width/2, 0, ne, width, height, colored);
	readScaled(data.get(), 0, height/2, sw, width, height, colored);
	readScaled(data.get(), width/2, height/2, se, width, height, colored);

	write(output, data.get(), width, height, colored);
}

}
}
