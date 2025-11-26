#pragma once

#if SERVER_ENABLE_BROTLI && __has_include(<brotli/encode.h>)
#define SERVER_HAS_BROTLI 1
#else
#define SERVER_HAS_BROTLI 0
#endif
