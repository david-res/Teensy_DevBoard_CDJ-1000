// rekordbox_anlz_api.cpp
// Implementation file for Rekordbox ANLZ Simple API
// Place this in your src/ folder

#define ANLZ_EMBEDDED
#define REKORDBOX_API_IMPLEMENTATION      // For API functions only

#include "rekordbox_anlz_parser.h"
#include "rekordbox_anlz_api.h"

// All extractXXX function implementations are now compiled only in this file
// Parser functions come from rekordbox_anlz_parser.cpp
// This prevents "multiple definition" linker errors