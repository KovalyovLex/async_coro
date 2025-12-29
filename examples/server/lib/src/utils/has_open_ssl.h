#pragma once

#if __has_include(<openssl/ssl.h>)
#define SERVER_HAS_SSL 1
#else
#define SERVER_HAS_SSL 0
#endif
