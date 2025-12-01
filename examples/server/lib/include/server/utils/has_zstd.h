#pragma once

#if SERVER_ENABLE_ZSTD && __has_include(<zstd.h>)
#define SERVER_HAS_ZSTD 1
#else
#define SERVER_HAS_ZSTD 0
#endif
