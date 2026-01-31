#pragma once

// NOLINTBEGIN(*macro-usage)

#if SERVER_ENABLE_DEFLATE_GZIP && __has_include(<zlib.h>)
#define SERVER_HAS_ZLIB 1
#else
#define SERVER_HAS_ZLIB 0
#endif

// NOLINTEND(*macro-usage)
