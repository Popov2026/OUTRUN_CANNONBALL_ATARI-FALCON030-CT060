#pragma once

/***************************************************************************
    Screenshot capture (C key) - saves the current picture as a real PNG
    file (SHOT0001.PNG, SHOT0002.PNG, ...) in the program's folder.

    Copyright (c) port authors. See license.txt for more details.
***************************************************************************/

#include "../stdint.hpp"

// Encodes `width`x`height` RGB565 pixels (stride pixels per row) as a PNG file at the next free
// SHOTnnnn.PNG name. Returns false if no free name was found (up to SHOT9999.PNG) or the file
// could not be written.
bool atari_save_screenshot(const uint16_t* rgb565, int width, int height, int stride);
