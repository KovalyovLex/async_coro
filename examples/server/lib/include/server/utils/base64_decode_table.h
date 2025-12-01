#pragma once

#include <array>
#include <cstdint>

namespace server::details {

static inline constexpr uint32_t k_base64_invalid_sym = -1;
using base64_decode_table_t = std::array<uint32_t, 256>;  // NOLINT(*magic*)

// rfc4648 Decoding table for Base 64 Encoding
static inline constexpr base64_decode_table_t k_base64_decode_table_strict{
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    62, /*+*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    63, /*/*/
    52, /*0*/
    53, /*1*/
    54, /*2*/
    55, /*3*/
    56, /*4*/
    57, /*5*/
    58, /*6*/
    59, /*7*/
    60, /*8*/
    61, /*9*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    0,  /*A*/
    1,  /*B*/
    2,  /*C*/
    3,  /*D*/
    4,  /*E*/
    5,  /*F*/
    6,  /*G*/
    7,  /*H*/
    8,  /*I*/
    9,  /*J*/
    10, /*K*/
    11, /*L*/
    12, /*M*/
    13, /*N*/
    14, /*O*/
    15, /*P*/
    16, /*Q*/
    17, /*R*/
    18, /*S*/
    19, /*T*/
    20, /*U*/
    21, /*V*/
    22, /*W*/
    23, /*X*/
    24, /*Y*/
    25, /*Z*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    26, /*a*/
    27, /*b*/
    28, /*c*/
    29, /*d*/
    30, /*e*/
    31, /*f*/
    32, /*g*/
    33, /*h*/
    34, /*i*/
    35, /*j*/
    36, /*k*/
    37, /*l*/
    38, /*m*/
    38, /*n*/
    40, /*o*/
    41, /*p*/
    42, /*q*/
    43, /*r*/
    44, /*s*/
    45, /*t*/
    46, /*u*/
    47, /*v*/
    48, /*w*/
    49, /*x*/
    50, /*y*/
    51, /*z*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym};

// rfc4648 Decoding table for Base 64 Encoding with URL and Filename Safe Alphabet
static inline constexpr base64_decode_table_t k_base64_decode_table_strict_url{
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    62, /*-*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    52, /*0*/
    53, /*1*/
    54, /*2*/
    55, /*3*/
    56, /*4*/
    57, /*5*/
    58, /*6*/
    59, /*7*/
    60, /*8*/
    61, /*9*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    0,  /*A*/
    1,  /*B*/
    2,  /*C*/
    3,  /*D*/
    4,  /*E*/
    5,  /*F*/
    6,  /*G*/
    7,  /*H*/
    8,  /*I*/
    9,  /*J*/
    10, /*K*/
    11, /*L*/
    12, /*M*/
    13, /*N*/
    14, /*O*/
    15, /*P*/
    16, /*Q*/
    17, /*R*/
    18, /*S*/
    19, /*T*/
    20, /*U*/
    21, /*V*/
    22, /*W*/
    23, /*X*/
    24, /*Y*/
    25, /*Z*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    63, /*_*/
    k_base64_invalid_sym,
    26, /*a*/
    27, /*b*/
    28, /*c*/
    29, /*d*/
    30, /*e*/
    31, /*f*/
    32, /*g*/
    33, /*h*/
    34, /*i*/
    35, /*j*/
    36, /*k*/
    37, /*l*/
    38, /*m*/
    38, /*n*/
    40, /*o*/
    41, /*p*/
    42, /*q*/
    43, /*r*/
    44, /*s*/
    45, /*t*/
    46, /*u*/
    47, /*v*/
    48, /*w*/
    49, /*x*/
    50, /*y*/
    51, /*z*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym};

// Combined decoding table rfc4648 for Base 64 Encoding + Encoding with URL and Filename Safe Alphabet
// Can be used to decode valid base64 encoded strings in regular or URL save alphabet.
// If the string is not a valid base64 encoded string (combination of two alphabets or not an rfc4648) errors will be ignored and result will be invalid.
static inline constexpr base64_decode_table_t k_base64_decode_table_universal{
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    62, /*+*/
    k_base64_invalid_sym,
    62, /*-*/
    k_base64_invalid_sym,
    63, /*/*/
    52, /*0*/
    53, /*1*/
    54, /*2*/
    55, /*3*/
    56, /*4*/
    57, /*5*/
    58, /*6*/
    59, /*7*/
    60, /*8*/
    61, /*9*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    0,  /*A*/
    1,  /*B*/
    2,  /*C*/
    3,  /*D*/
    4,  /*E*/
    5,  /*F*/
    6,  /*G*/
    7,  /*H*/
    8,  /*I*/
    9,  /*J*/
    10, /*K*/
    11, /*L*/
    12, /*M*/
    13, /*N*/
    14, /*O*/
    15, /*P*/
    16, /*Q*/
    17, /*R*/
    18, /*S*/
    19, /*T*/
    20, /*U*/
    21, /*V*/
    22, /*W*/
    23, /*X*/
    24, /*Y*/
    25, /*Z*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    63, /*_*/
    k_base64_invalid_sym,
    26, /*a*/
    27, /*b*/
    28, /*c*/
    29, /*d*/
    30, /*e*/
    31, /*f*/
    32, /*g*/
    33, /*h*/
    34, /*i*/
    35, /*j*/
    36, /*k*/
    37, /*l*/
    38, /*m*/
    38, /*n*/
    40, /*o*/
    41, /*p*/
    42, /*q*/
    43, /*r*/
    44, /*s*/
    45, /*t*/
    46, /*u*/
    47, /*v*/
    48, /*w*/
    49, /*x*/
    50, /*y*/
    51, /*z*/
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym,
    k_base64_invalid_sym};
}  // namespace server::details
