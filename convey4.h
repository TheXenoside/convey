/*******************************************************************************************
 *
 * convey v1.2  –  Variable-width dynamic/static string library with first-class
 *                 Unicode encoding support  (C11 required)
 *
 * DEPENDENCIES
 *   utf8proc.h  – UTF-8 processing, normalisation, codepoint iteration
 *   <stdlib.h>  – malloc / realloc / #pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <limits.h>   /* INT_MAX */
#include <stdio.h>
#include <string.h>
#include <SDL3/SDL.h>
#include "utf8proc.h"

#ifdef __cplusplus
extern "C" {
#endif

/*══════════════════════════════════════════════════════════════════════════════
  ALIGNMENT / PACKING SWITCH
  Uncomment for older or embedded hardware that cannot handle misaligned reads.
  Dynamic strings can cause heavy memory fragmentation on embedded systems;
  consider SDL_SetMemoryFunctions with an arena or pool allocator.
══════════════════════════════════════════════════════════════════════════════*/

//#define b_ALIGNED

/*══════════════════════════════════════════════════════════════════════════════
  TYPE-FLAGS BIT LAYOUT  (one byte, lives immediately before the data pointer)

    Bit 0    – Memory layout     B_STRING_PACK_MASK   0x01
    Bit 1    – Allocation policy B_STRING_STATIC_MASK 0x02
    Bits 2-3 – Header size class B_STR_TYPE_MASK      0x0C
    Bits 4-6 – Encoding tag      B_STR_ENC_MASK       0x70
    Bit 7    – reserved / 0

══════════════════════════════════════════════════════════════════════════════*/

/* --- Memory Layout (Bit 0) --- */
#define B_STRING_PACKED      0x00u  /* 0000 0000 – packed structs  (default)  */
#define B_STRING_ALIGNED     0x01u  /* 0000 0001 – aligned structs w/ padding */
#define B_STRING_PACK_MASK   0x01u

/* --- Allocation Strategy (Bit 1) --- */
#define B_STRING_DYNAMIC     0x00u  /* 0000 0000 – dynamically resizing       */
#define B_STRING_STATIC      0x02u  /* 0000 0010 – fixed-capacity heap buffer */
#define B_STRING_STATIC_MASK 0x02u

/*
 * NOTE on "static" strings:  despite the name, static b_str_t objects are
 * still heap-allocated via SDL_malloc.  The flag means the *capacity* is
 * fixed – b_str_ensure() will never reallocate them.  You can still
 * b_str_free() a static string normally.
 */

/* Convenience alias */
#define B_STR_STATIC  B_STRING_STATIC

/* --- Header Size Class (Bits 2-3) --- */
#define B_STR_TYPE_8         0x00u  /* 0000 0000 – used/cap stored as uint8_t  */
#define B_STR_TYPE_16        0x04u  /* 0000 0100 – used/cap stored as uint16_t */
#define B_STR_TYPE_32        0x08u  /* 0000 1000 – used/cap stored as uint32_t */
#define B_STR_TYPE_64        0x0Cu  /* 0000 1100 – used/cap stored as uint64_t */
#define B_STR_TYPE_MASK      0x0Cu

/* --- Encoding Tag (Bits 4-6) --- */
#define B_STR_ENC_ASCII      0x00u  /* 0000 0000 – ASCII (7-bit)               */
#define B_STR_ENC_UTF8       0x10u  /* 0001 0000 – UTF-8                       */
#define B_STR_ENC_UTF16BE    0x20u  /* 0010 0000 – UTF-16 Big-Endian           */
#define B_STR_ENC_UTF16LE    0x30u  /* 0011 0000 – UTF-16 Little-Endian        */
#define B_STR_ENC_UTF32BE    0x40u  /* 0100 0000 – UTF-32 Big-Endian           */
#define B_STR_ENC_UTF32LE    0x50u  /* 0101 0000 – UTF-32 Little-Endian        */
#define B_STR_ENC_UTF16      B_STR_ENC_UTF16LE  /* alias – LE is default UTF-16 */
#define B_STR_ENC_UTF32      B_STR_ENC_UTF32LE  /* alias – LE is default UTF-32 */
#define B_STR_ENC_MASK       0x70u

/*
 * Encoding family testers.
 * The argument must already be masked with B_STR_ENC_MASK (or be a raw
 * flags byte; the extra bits are harmless because the masks isolate bits
 * 5-6 only).
 *
 * UTF-16 encodings occupy the range 0x20..0x3F (bits 5-6 == 01).
 * UTF-32 encodings occupy the range 0x40..0x5F (bits 5-6 == 10).
 */
#define B_STR_IS_UTF16_ENC(encoding) (((encoding) & 0x60u) == 0x20u)
#define B_STR_IS_UTF32_ENC(encoding) (((encoding) & 0x60u) == 0x40u)

#define B_STR_ONE_MEG  1048576u

/*══════════════════════════════════════════════════════════════════════════════
  HEADER STRUCTS
  Under b_ALIGNED the structs carry explicit padding so every field lands on
  its natural boundary and the flags byte is always the last byte of the
  header.  Under packed mode the structs are __packed__ / pragma pack(1) with
  no gaps.  In both cases s[-1] is always the flags byte and B_HDR*(s) gives
  the corresponding header pointer.

  Header sizes (packed / aligned):
    b_string_header_8  –  3 bytes  /  3 bytes
    b_string_header_16 –  5 bytes  /  6 bytes
    b_string_header_32 –  9 bytes  / 12 bytes
    b_string_header_64 – 17 bytes  / 24 bytes
══════════════════════════════════════════════════════════════════════════════*/

#ifdef b_ALIGNED

typedef struct b_string_header_8 {
    uint8_t  capacity;
    uint8_t  length;
    uint8_t  flags;
    uint8_t  data[];
} b_hdr8_t;

typedef struct b_string_header_16 {
    uint16_t capacity;
    uint16_t length;
    uint8_t  _padding;  /* 1 byte – brings header to 6 bytes */
    uint8_t  flags;
    uint8_t  data[];
} b_hdr16_t;

typedef struct b_string_header_32 {
    uint32_t capacity;
    uint32_t length;
    uint8_t  _padding[3]; /* 3 bytes – brings header to 12 bytes */
    uint8_t  flags;
    uint8_t  data[];
} b_hdr32_t;

typedef struct b_string_header_64 {
    uint64_t capacity;
    uint64_t length;
    uint8_t  _padding[7]; /* 7 bytes – brings header to 24 bytes */
    uint8_t  flags;
    uint8_t  data[];
    /* NOTE: on 32-bit embedded hardware you are unlikely to reach this class */
} b_hdr64_t;

#else  /* packed (default) */

#if defined(_MSC_VER)
#  pragma pack(push, 1)
#  define B_STR_PACKED_ATTR
#elif defined(__GNUC__) || defined(__clang__)
#  define B_STR_PACKED_ATTR __attribute__((__packed__))
#else
#  define B_STR_PACKED_ATTR
#endif

typedef struct B_STR_PACKED_ATTR b_string_header_8  { uint8_t  capacity; uint8_t  length; uint8_t flags; uint8_t data[]; } b_hdr8_t;
typedef struct B_STR_PACKED_ATTR b_string_header_16 { uint16_t capacity; uint16_t length; uint8_t flags; uint8_t data[]; } b_hdr16_t;
typedef struct B_STR_PACKED_ATTR b_string_header_32 { uint32_t capacity; uint32_t length; uint8_t flags; uint8_t data[]; } b_hdr32_t;
typedef struct B_STR_PACKED_ATTR b_string_header_64 { uint64_t capacity; uint64_t length; uint8_t flags; uint8_t data[]; } b_hdr64_t;

#if defined(_MSC_VER)
#  pragma pack(pop)
#endif

#endif /* b_ALIGNED */

/*
 * Backward-compatible type aliases (so existing code using the old bare names
 * b_hdr8 / b_hdr16 / b_hdr32 / b_hdr64 keeps compiling).
 */
typedef b_hdr8_t  b_hdr8;
typedef b_hdr16_t b_hdr16;
typedef b_hdr32_t b_hdr32;
typedef b_hdr64_t b_hdr64;

/* Mutable header access – cast backwards from the data pointer. */
#define B_HDR8(p)   ((b_hdr8_t *) ((p) - sizeof(b_hdr8_t)))
#define B_HDR16(p)  ((b_hdr16_t*)((p) - sizeof(b_hdr16_t)))
#define B_HDR32(p)  ((b_hdr32_t*)((p) - sizeof(b_hdr32_t)))
#define B_HDR64(p)  ((b_hdr64_t*)((p) - sizeof(b_hdr64_t)))

/* Const header access */
#define B_CHDR8(p)  ((const b_hdr8_t *) ((p) - sizeof(b_hdr8_t)))
#define B_CHDR16(p) ((const b_hdr16_t*)((p) - sizeof(b_hdr16_t)))
#define B_CHDR32(p) ((const b_hdr32_t*)((p) - sizeof(b_hdr32_t)))
#define B_CHDR64(p) ((const b_hdr64_t*)((p) - sizeof(b_hdr64_t)))

/*══════════════════════════════════════════════════════════════════════════════
  CORE TYPES
══════════════════════════════════════════════════════════════════════════════*/

typedef uint8_t*       b_str_t;
typedef const uint8_t* b_cstr_t;

/*
 * Slice types – a (pointer, byte-length) pair, NOT null-terminated.
 * b_slice_t and b_u8slice_t are byte-level views (ASCII or UTF-8).
 * b_u16slice_t  – UTF-16 view; len is always a multiple of 2.
 * b_u32slice_t  – UTF-32 view; len is always a multiple of 4.
 */
typedef struct b_byte_slice   { const uint8_t *data; size_t len; } b_slice_t;
typedef struct b_utf8_slice   { const uint8_t *data; size_t len; } b_u8slice_t;
typedef struct b_utf16_slice  { const uint8_t *data; size_t len; } b_u16slice_t;
typedef struct b_utf32_slice  { const uint8_t *data; size_t len; } b_u32slice_t;

/*══════════════════════════════════════════════════════════════════════════════
  CORE ACCESSORS
══════════════════════════════════════════════════════════════════════════════*/

size_t  b_str_hdr_size (uint8_t flags);
uint8_t b_str_pick_type(size_t  byte_size);
uint8_t b_str_enc      (b_cstr_t s);
void    b_str_set_enc  (b_str_t  s, uint8_t encoding);
size_t  b_str_len      (b_cstr_t s);
size_t  b_str_cap      (b_cstr_t s);
size_t  b_str_avail    (b_cstr_t s);
void    b_str_set_lens (b_str_t  s, size_t used_bytes, size_t capacity_bytes);
void    b_str_set_len  (b_str_t  s, size_t used_bytes);
size_t  b_str_cpcount  (b_cstr_t s);

/*══════════════════════════════════════════════════════════════════════════════
  LIFECYCLE
══════════════════════════════════════════════════════════════════════════════*/

b_str_t b_str_new           (const char *cstr);
b_str_t b_str_new_pro       (const void *data, size_t byte_len, uint8_t encoding);
b_str_t b_str_new_static    (const char *cstr, size_t extra_bytes);
b_str_t b_str_new_static_pro(const void *data, size_t byte_len,
                              size_t total_capacity, uint8_t encoding);
void    b_str_free          (b_str_t s);
b_str_t b_str_dup           (b_cstr_t s);
void    b_str_clear         (b_str_t s);
bool    b_str_empty         (b_cstr_t s);
b_str_t b_str_to_dyn        (b_str_t s);

/*══════════════════════════════════════════════════════════════════════════════
  SLICE CONSTRUCTORS
══════════════════════════════════════════════════════════════════════════════*/

b_str_t b_str_from_slice   (b_slice_t    slice);
b_str_t b_str_from_u8slice (b_u8slice_t  slice);
b_str_t b_str_from_u16slice(b_u16slice_t slice);
b_str_t b_str_from_u16     (const uint16_t *units, size_t unit_count);
b_str_t b_str_from_u32slice(b_u32slice_t slice);
b_str_t b_str_from_u32     (const uint32_t *units, size_t unit_count);

/*══════════════════════════════════════════════════════════════════════════════
  APPENDING & CONCATENATION
══════════════════════════════════════════════════════════════════════════════*/

b_str_t b_str_append    (b_str_t s, const char *cstr);
b_str_t b_str_append_pro(b_str_t s, const void *data, size_t byte_len);
b_str_t b_str_append_sl (b_str_t s, b_slice_t    slice);
b_str_t b_str_append_u8 (b_str_t s, b_u8slice_t  slice);
b_str_t b_str_append_u16(b_str_t s, b_u16slice_t slice);
b_str_t b_str_append_u32(b_str_t s, b_u32slice_t slice);
b_str_t b_str_concat    (b_cstr_t a, b_cstr_t b);

/*══════════════════════════════════════════════════════════════════════════════
  CAPACITY MANAGEMENT
══════════════════════════════════════════════════════════════════════════════*/

b_str_t b_str_ensure (b_str_t s, size_t extra_bytes);
b_str_t b_str_reserve(b_str_t s, size_t extra_bytes);
b_str_t b_str_fit    (b_str_t s);
void    b_str_arr_fit(b_str_t *array, size_t count);

/*══════════════════════════════════════════════════════════════════════════════
  SLICE EXTRACTORS
══════════════════════════════════════════════════════════════════════════════*/

b_slice_t    b_slice_of          (b_cstr_t s);
b_slice_t    b_subslice          (b_cstr_t s, size_t byte_offset, size_t byte_len);
b_slice_t    b_subslice_cp       (b_cstr_t s, size_t codepoint_offset,
                                              size_t codepoint_count);

b_u8slice_t  b_u8slice_of        (b_cstr_t s);
b_u8slice_t  b_u8subslice        (b_cstr_t s, size_t byte_offset, size_t byte_len);
b_u8slice_t  b_u8subslice_cp     (b_cstr_t s, size_t codepoint_offset,
                                               size_t codepoint_count);

b_u16slice_t b_u16slice_of       (b_cstr_t s);
b_u16slice_t b_u16subslice       (b_cstr_t s, size_t byte_offset, size_t byte_len);
b_u16slice_t b_u16subslice_cp    (b_cstr_t s, size_t codepoint_offset,
                                               size_t codepoint_count);
size_t       b_u16slice_units    (b_u16slice_t slice);

b_u32slice_t b_u32slice_of       (b_cstr_t s);
b_u32slice_t b_u32subslice       (b_cstr_t s, size_t byte_offset, size_t byte_len);
b_u32slice_t b_u32subslice_cp    (b_cstr_t s, size_t codepoint_offset,
                                               size_t codepoint_count);
size_t       b_u32slice_units    (b_u32slice_t slice);

/*══════════════════════════════════════════════════════════════════════════════
  ENCODING CONVERTERS
  String converters NEVER prepend a BOM; call b_str_add_bom(s) explicitly.
  Each converter accepts any b_str encoding as input and routes through
  UTF-8 as the common intermediate when a direct path isn't available.
══════════════════════════════════════════════════════════════════════════════*/

b_str_t b_str_utf8_norm (const char *null_terminated_utf8); /* NFC-normalise   */
b_str_t b_str_to_utf16  (b_cstr_t s);  /* any  ->  UTF-16 LE (system default)  */
b_str_t b_str_to_utf16be(b_cstr_t s);  /* any  ->  UTF-16 BE                   */
b_str_t b_str_to_utf32le(b_cstr_t s);  /* any  ->  UTF-32 LE                   */
b_str_t b_str_to_utf32be(b_cstr_t s);  /* any  ->  UTF-32 BE                   */
b_str_t b_str_to_utf8   (b_cstr_t s);  /* any  ->  UTF-8                       */

/*══════════════════════════════════════════════════════════════════════════════
  CASE CONVERSION
  Only UTF-8 and ASCII are supported.  UTF-16 and UTF-32 strings return NULL.

  b_str_lower  – Unicode case-folding via utf8proc_decompose_char.
                 The result is NFC-recomposed per UTF8PROC_COMPOSE.
                 Produces the Unicode "case-fold" normal form (suitable for
                 case-insensitive comparison), which may differ slightly from
                 locale-specific lowercase.

  b_str_upper  – Simple uppercase via utf8proc_toupper (per-codepoint mapping).
                 Unlike b_str_lower this does NOT decompose/recompose, so some
                 multi-codepoint uppercase mappings are not applied.  Both
                 functions are intentionally asymmetric: the utf8proc API
                 exposes a rich casefold path but only a simple toupper.
══════════════════════════════════════════════════════════════════════════════*/

b_str_t b_str_lower(b_cstr_t s);
b_str_t b_str_upper(b_cstr_t s);

/*══════════════════════════════════════════════════════════════════════════════
  COMPARISON & SEARCH
  All comparisons are byte-level (memcmp).  For Unicode-aware ordering,
  normalise both strings first with b_str_utf8_norm.
══════════════════════════════════════════════════════════════════════════════*/

int    b_str_cmp        (b_cstr_t a, b_cstr_t b);
bool   b_str_eq         (b_cstr_t a, b_cstr_t b);
size_t b_str_find       (b_cstr_t haystack, b_cstr_t needle);
bool   b_str_contains   (b_cstr_t s, b_cstr_t needle);
bool   b_str_starts_with(b_cstr_t s, b_cstr_t prefix);
bool   b_str_ends_with  (b_cstr_t s, b_cstr_t suffix);

/*══════════════════════════════════════════════════════════════════════════════
  IN-PLACE MUTATION
══════════════════════════════════════════════════════════════════════════════*/

b_str_t b_str_trim_r(b_str_t s);
b_str_t b_str_trim_l(b_str_t s);
b_str_t b_str_trim  (b_str_t s);
b_str_t b_str_repeat(b_cstr_t s, size_t repeat_count);

/*══════════════════════════════════════════════════════════════════════════════
  VALIDATION
══════════════════════════════════════════════════════════════════════════════*/

bool b_str_valid_utf8(b_cstr_t s);

/*══════════════════════════════════════════════════════════════════════════════
  BOM
══════════════════════════════════════════════════════════════════════════════*/

/*
 * b_str_detect_bom
 *
 *   Inspect the leading bytes of `data` for a UTF-8, UTF-16 LE/BE, or
 *   UTF-32 LE/BE byte-order mark.  Sets *bom_size_out to the BOM byte length
 *   (2–4) or to 0 when none is found.  bom_size_out may be NULL.
 *   Returns the matching B_STR_ENC_* tag, or B_STR_ENC_ASCII when absent.
 *
 *   Detection order (most-specific first to avoid ambiguity):
 *     UTF-32 BE  : 00 00 FE FF  (4 bytes)
 *     UTF-32 LE  : FF FE 00 00  (4 bytes, checked before UTF-16 LE)
 *     UTF-8      : EF BB BF     (3 bytes)
 *     UTF-16 LE  : FF FE        (2 bytes)
 *     UTF-16 BE  : FE FF        (2 bytes)
 */
uint8_t b_str_detect_bom(const void *data, size_t byte_len, size_t *bom_size_out);

/*
 * b_str_add_bom
 *
 *   Prepend the correct BOM for the string's own encoding tag.
 *   The caller MUST reassign:  s = b_str_add_bom(s);
 *   ASCII strings are returned unchanged (no BOM for ASCII).
 */
b_str_t b_str_add_bom(b_str_t s);

/*══════════════════════════════════════════════════════════════════════════════
  FILE I/O – PRIMITIVES
══════════════════════════════════════════════════════════════════════════════*/

/*
 * b_str_load_file
 *   Read an entire file into a new b_str_t.  BOM (if any) is stripped and used
 *   to set the encoding tag.  fallback_encoding is used when no BOM is found
 *   (pass 0 to default to B_STR_ENC_UTF8).  Returns NULL on failure.
 */
b_str_t b_str_load_file(const char *path, uint8_t fallback_encoding);

/*
 * b_str_save_file
 *   Write s to a file.  When write_bom is true the appropriate BOM is written
 *   first; ASCII strings produce no BOM.  Returns 0 on success, -1 on failure.
 */
int b_str_save_file(const char *path, b_cstr_t s, bool write_bom);

/*
 * b_file_add_bom
 *   Prepend the correct BOM for the given encoding to the file at path.
 *   If the file already begins with that exact BOM it is left untouched.
 *   Returns 0 on success, -1 on failure.
 */
int b_file_add_bom(const char *path, uint8_t encoding);

/*══════════════════════════════════════════════════════════════════════════════
  FILE CONVERSION WRAPPERS
  _b_file_convert() is internal (not exported).  Use the named wrappers below.

  Naming convention:
    b_file_conv_{input_enc}[_no_bom_in]_to_{output_enc}[_bom|_no_bom]

  Input:  BOM is always auto-detected.  The encoding constant passed as the
          second argument to _b_file_convert is only used as a *fallback* when
          no BOM is found.
  Output: "_bom" appends the BOM for the output encoding; "_no_bom" does not.
          When neither suffix appears a BOM IS written (the most common need).
══════════════════════════════════════════════════════════════════════════════*/

/* ASCII  ->  UTF-8 */
int b_file_conv_ascii_to_utf8_bom    (const char *in_path, const char *out_path);
int b_file_conv_ascii_to_utf8_no_bom (const char *in_path, const char *out_path);

/* UTF-8  ->  UTF-16 */
int b_file_conv_utf8_to_utf16                 (const char *in_path, const char *out_path); /* system endian + BOM */
int b_file_conv_utf8_to_utf16le_bom           (const char *in_path, const char *out_path);
int b_file_conv_utf8_to_utf16le_no_bom        (const char *in_path, const char *out_path);
int b_file_conv_utf8_to_utf16be_bom           (const char *in_path, const char *out_path);
int b_file_conv_utf8_to_utf16be_no_bom        (const char *in_path, const char *out_path);

/* UTF-8  ->  UTF-32 */
int b_file_conv_utf8_to_utf32                 (const char *in_path, const char *out_path); /* system endian + BOM */
int b_file_conv_utf8_to_utf32le_bom           (const char *in_path, const char *out_path);
int b_file_conv_utf8_to_utf32le_no_bom        (const char *in_path, const char *out_path);
int b_file_conv_utf8_to_utf32be_bom           (const char *in_path, const char *out_path);
int b_file_conv_utf8_to_utf32be_no_bom        (const char *in_path, const char *out_path);

/* UTF-16  ->  UTF-8 */
int b_file_conv_utf16_to_utf8_no_bom          (const char *in_path, const char *out_path); /* auto-detect; fallback LE */
int b_file_conv_utf16le_bom_to_utf8_no_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf16le_no_bom_to_utf8_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf16le_no_bom_to_utf8_no_bom (const char *in_path, const char *out_path);
int b_file_conv_utf16be_bom_to_utf8_no_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf16be_no_bom_to_utf8_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf16be_no_bom_to_utf8_no_bom (const char *in_path, const char *out_path);

/* UTF-32  ->  UTF-8 */
int b_file_conv_utf32_to_utf8_no_bom          (const char *in_path, const char *out_path); /* auto-detect; fallback LE */
int b_file_conv_utf32le_bom_to_utf8_no_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf32le_no_bom_to_utf8_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf32le_no_bom_to_utf8_no_bom (const char *in_path, const char *out_path);
int b_file_conv_utf32be_bom_to_utf8_no_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf32be_no_bom_to_utf8_bom    (const char *in_path, const char *out_path);
int b_file_conv_utf32be_no_bom_to_utf8_no_bom (const char *in_path, const char *out_path);

/*══════════════════════════════════════════════════════════════════════════════
  UTF-16 STDOUT HELPER
══════════════════════════════════════════════════════════════════════════════*/

/*
 * b_str_print_utf16 – convert a UTF-16 slice to UTF-8 and write to stdout.
 * Exists because printf("%.*s") cannot print UTF-16 data directly.
 */
void b_str_print_utf16(b_u16slice_t slice);

/*══════════════════════════════════════════════════════════════════════════════
  PROPERTY MACROS
══════════════════════════════════════════════════════════════════════════════*/

#define B_STR_IS_STATIC_S(s)  (((s) != NULL) && (((s)[-1] & B_STRING_STATIC_MASK) != 0u))
#define B_STR_IS_ALIGNED_S(s) (((s) != NULL) && (((s)[-1] & B_STRING_PACK_MASK)   != 0u))

/*
 * B_STR_NULL_SIZE – null-terminator width in bytes for the given encoding tag.
 *   ASCII / UTF-8 → 1 byte
 *   UTF-16        → 2 bytes
 *   UTF-32        → 4 bytes
 */
#define B_STR_NULL_SIZE(encoding) \
    (B_STR_IS_UTF32_ENC(encoding) ? 4u : B_STR_IS_UTF16_ENC(encoding) ? 2u : 1u)

#define B_STR_ENC_OF(s)       b_str_enc(s)
#define B_STR_IS_ASCII(s)     (b_str_enc(s) == B_STR_ENC_ASCII)
#define B_STR_IS_UTF8(s)      (b_str_enc(s) == B_STR_ENC_UTF8)
#define B_STR_IS_UTF16LE(s)   (b_str_enc(s) == B_STR_ENC_UTF16LE)
#define B_STR_IS_UTF16BE(s)   (b_str_enc(s) == B_STR_ENC_UTF16BE)
#define B_STR_IS_UTF16(s)     (B_STR_IS_UTF16_ENC(b_str_enc(s)))
#define B_STR_IS_UTF32LE(s)   (b_str_enc(s) == B_STR_ENC_UTF32LE)
#define B_STR_IS_UTF32BE(s)   (b_str_enc(s) == B_STR_ENC_UTF32BE)
#define B_STR_IS_UTF32(s)     (B_STR_IS_UTF32_ENC(b_str_enc(s)))
#define B_STR_IS_BYTE_ENC(s)  (!B_STR_IS_UTF16(s) && !B_STR_IS_UTF32(s))

/* Cast helpers for direct uint16_t/uint32_t iteration */
#define B_STR_AS_U16(s)       ((const uint16_t*)(s))
#define B_STR_AS_U32(s)       ((const uint32_t*)(s))

/*══════════════════════════════════════════════════════════════════════════════
  SLICE HELPERS
══════════════════════════════════════════════════════════════════════════════*/

#define B_SLICE_OF(s)         ((b_slice_t)   {(s), b_str_len(s)})
#define B_U8SLICE_OF(s)       ((b_u8slice_t) {(s), b_str_len(s)})
#define B_U16SLICE_OF(s)      ((b_u16slice_t){(s), b_str_len(s)})
#define B_U32SLICE_OF(s)      ((b_u32slice_t){(s), b_str_len(s)})
#define B_SUBSLICE(s,o,l)     b_subslice((s),(o),(l))
#define B_SUBSLICE_CP(s,o,c)  b_subslice_cp((s),(o),(c))

/*══════════════════════════════════════════════════════════════════════════════
  PRINTF FORMAT HELPERS  (C11 _Generic)

  B_STR_FMT   – format token to use in a printf format string: "%.*s"
  B_FMT_ARG   – produce a b_fmt_arg_t from a string/slice/cstr expression.
                 Supported types: char*, const char*, uint8_t*, const uint8_t*,
                 b_slice_t, b_u8slice_t.
  B_str_len   – extract the (int) byte count from any of the above types.
  B_str_ptr   – extract the (const char*) data pointer from any of the above.

  BUG WARNING – B_str_arg(X) evaluates X twice.
                Do NOT use it with expressions that have side effects (function
                calls, post-increment, etc.).  Prefer B_PRINTF_SAFE for those.

  SAFE VARIANT – B_PRINTF_SAFE(fmt, expr) evaluates expr exactly once.
                 The format string must include "%.*s" (or more generally must
                 consume an int precision and a const char*).  Example:
                   B_PRINTF_SAFE("key=%.*s\n", my_b_str);
══════════════════════════════════════════════════════════════════════════════*/

#define B_STR_FMT  "%.*s"

/*
 * b_fmt_arg_t – (len, ptr) pair produced by the _Generic helper functions.
 * len is clamped to INT_MAX so that "%.*s" never receives a negative precision.
 */
typedef struct b_printf_format_argument {
    int         len;
    const char *ptr;
} b_fmt_arg_t;

/* --- Internal helper functions (not for direct use) --- */

static inline b_fmt_arg_t _b_fmt_cstr(const char *s) {
    size_t raw_len = s ? strlen(s) : 0;
    return (b_fmt_arg_t){
        (raw_len > (size_t)INT_MAX) ? INT_MAX : (int)raw_len,
        s ? s : ""
    };
}

static inline b_fmt_arg_t _b_fmt_str(const uint8_t *s) {
    /* b_str_len handles NULL safely, returning 0. */
    size_t raw_len = b_str_len(s);
    return (b_fmt_arg_t){
        (raw_len > (size_t)INT_MAX) ? INT_MAX : (int)raw_len,
        s ? (const char*)s : ""
    };
}

static inline b_fmt_arg_t _b_fmt_sl(b_slice_t slice) {
    return (b_fmt_arg_t){
        (slice.len > (size_t)INT_MAX) ? INT_MAX : (int)slice.len,
        slice.data ? (const char*)slice.data : ""
    };
}

static inline b_fmt_arg_t _b_fmt_u8sl(b_u8slice_t slice) {
    return (b_fmt_arg_t){
        (slice.len > (size_t)INT_MAX) ? INT_MAX : (int)slice.len,
        slice.data ? (const char*)slice.data : ""
    };
}

/* --- Public _Generic dispatcher --- */

#define B_FMT_ARG(X) _Generic((X),             \
    char*:          _b_fmt_cstr,                \
    const char*:    _b_fmt_cstr,                \
    uint8_t*:       _b_fmt_str,                 \
    const uint8_t*: _b_fmt_str,                 \
    b_slice_t:      _b_fmt_sl,                  \
    b_u8slice_t:    _b_fmt_u8sl                 \
)(X)

/*
 * B_str_len / B_str_ptr – extract a single field.
 *
 * WARNING: each macro call evaluates X once; if you need BOTH len and ptr
 * (i.e., B_str_arg), X is evaluated TWICE.  Use B_PRINTF_SAFE instead when
 * X has side effects.
 */
#define B_str_len(X)  (B_FMT_ARG(X).len)
#define B_str_ptr(X)  (B_FMT_ARG(X).ptr)

/*
 * B_str_arg(X) – expands to "len, ptr" for use inside printf(...).
 *
 * DOUBLE-EVALUATION: X is evaluated twice.
 * Safe for simple variable references; avoid complex expressions.
 */
#define B_str_arg(X)  B_str_len(X), B_str_ptr(X)

/*
 * B_PRINTF_SAFE – the only macro that evaluates expr exactly once.
 * Use this whenever expr is not a simple variable.
 *
 * The caller is responsible for ensuring the format string contains "%.*s"
 * at the position that consumes the (int, const char*) pair.  No compile-
 * time check is performed.
 *
 * Example:
 *   B_PRINTF_SAFE("result: " B_STR_FMT "\n", compute_string());
 */
#define B_PRINTF_SAFE(fmt, expr)                          \
    do {                                                  \
        b_fmt_arg_t _b_safe_arg_ = B_FMT_ARG(expr);      \
        printf((fmt), _b_safe_arg_.len, _b_safe_arg_.ptr);\
    } while (0)

#ifdef __cplusplus
}
#endif
 * ── MEMORY LAYOUT ──────────────────────────────────────────────────────────────────
 *
 *   Each allocation looks like this in memory:
 *
 *     [ header fields ... | type_flags ] [ ... character data ... ] [ NUL byte(s) ]
 *                                        ^
 *                          c_string_t points HERE  (string[0] is the first data byte)
 *                          string[-1] is always the type_flags byte
 *
 *   The header sits immediately before the data pointer and is one of four sizes
 *   depending on how large the string is:
 *
 *     Header class   Size fields   Max storable length
 *     ─────────────  ────────────  ───────────────────
 *     header_8       uint8_t × 2   255 bytes
 *     header_16      uint16_t × 2  65 535 bytes
 *     header_32      uint32_t × 2  4 294 967 295 bytes
 *     header_64      uint64_t × 2  virtually unlimited
 *
 *   Both `used_length` and `allocated_capacity` are always in BYTES (never chars or
 *   codepoints) and always exclude the null terminator(s).
 *
 * ── TYPE-FLAGS BYTE (string[-1]) ───────────────────────────────────────────────────
 *
 *   Bits 1–2  (C_STRING_TYPE_MASK  0x06)  header size class (TYPE_8/16/32/64)
 *   Bit  0    (C_STRING_IS_STATIC  0x01)  1 = fixed-capacity static string
 *   Bits 3–4  (C_STRING_ENC_MASK   0x18)  encoding tag
 *
 *   The three fields are non-overlapping so they can be freely OR'd together:
 *
 *     type_flags = TYPE_xx | IS_STATIC (if applicable) | ENC_xxx
 *
 * ── ENCODING TAGS ──────────────────────────────────────────────────────────────────
 *
 *   C_STRING_ENC_ASCII  (0x00)  Raw bytes / ASCII  – 1-byte NUL terminator
 *   C_STRING_ENC_UTF8   (0x08)  UTF-8              – 1-byte NUL terminator
 *   C_STRING_ENC_UTF16  (0x10)  UTF-16 LE          – 2-byte NUL terminator
 *
 *   Rules:
 *   · c_string_create()           tags the result C_STRING_ENC_ASCII by default.
 *   · c_string_create_from_utf8() NFC-normalises and tags C_STRING_ENC_UTF8.
 *   · c_string_append_utf8()      auto-upgrades ASCII → UTF-8 on the destination.
 *   · UTF-16 strings are ALWAYS double-NUL-terminated (two zero bytes).
 *   · Use C_STRING_NULL_SIZE(enc) wherever a null-terminator byte count is needed
 *     instead of hardcoding 1 or 2.
 *
 * ── PRINT FORMAT MACROS ────────────────────────────────────────────────────────────
 *
 *   Standard (for variables, slices, and C literals – double-evaluates the argument):
 *
 *     printf("Result: " C_STR_FMT "\n", C_str_arg(my_str));
 *     printf("Name: " C_STR_FMT ", age: %d\n", C_str_arg(my_str), 25);
 *
 *   WARNING: C_str_arg(X) expands X twice.  Never pass a mutating function call.
 *   Use C_PRINTF_SAFE when the argument is a function call:
 *
 *     C_PRINTF_SAFE("Trimmed: " C_STR_FMT "\n", c_string_trim(my_str));
 *
 *   UTF-16 on Windows (wchar_t == uint16_t):
 *
 *     wprintf(L"Result: " C_WSTR_FMT L"\n", C_wstr_arg(my_utf16_str));
 *     C_WPRINTF_SAFE(L"Result: " C_WSTR_FMT L"\n", c_string_trim(my_utf16_str));
 *
 *   Cross-platform UTF-16 printing (converts to UTF-8 first):
 *
 *     c_string_print_utf16(C_UTF16_SLICE_OF(my_str));
 *
 * ── QUICK EXAMPLE ──────────────────────────────────────────────────────────────────
 *
 *   c_string_t s = c_string_create("hello");
 *   s = c_string_append(s, " world");
 *   printf("Message: " C_STR_FMT "\n", C_str_arg(s));  // Message: hello world
 *   c_string_free(s);
 *
 ********************************************************************************************
 *
 *   zlib/libpng license  –  Copyright (c) 2024 convey contributors
 *
 *******************************************************************************************/

#ifndef CONVEY_H
#define CONVEY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h> /* malloc / realloc / free */
#include <string.h> /* memcpy / memmove / memcmp / strlen */
#include <wchar.h>  /* wchar_t – needed by the UTF-16 print helpers */
#include "utf8proc.h"

/*==================================================================================
	SECTION 1 – CUSTOM ALLOCATOR HOOKS
	Define before including this header to redirect all internal allocations.
==================================================================================*/
#ifndef C_STRING_MALLOC
#define C_STRING_MALLOC(sz) malloc(sz)
#endif
#ifndef C_STRING_REALLOC
#define C_STRING_REALLOC(p, sz) realloc((p), (sz))
#endif
#ifndef C_STRING_FREE
#define C_STRING_FREE(p) free(p)
#endif

/*==================================================================================
	SECTION 2 – TYPE-FLAGS BIT CONSTANTS
	These are packed into the single `type_flags` byte that lives at string[-1].
	Bits 1-2 = header size class.  Bit 0 = static flag.  Bits 3-4 = encoding.
==================================================================================*/

/* ── Header size class (bits 1-2) ─────────────────────────────────────────────── */
#define C_STRING_TYPE_8 0x00u    /* 0000 0000 – used/cap fields are uint8_t  */
#define C_STRING_TYPE_16 0x02u   /* 0000 0010 – used/cap fields are uint16_t */
#define C_STRING_TYPE_32 0x04u   /* 0000 0100 – used/cap fields are uint32_t */
#define C_STRING_TYPE_64 0x06u   /* 0000 0110 – used/cap fields are uint64_t */
#define C_STRING_TYPE_MASK 0x06u /* mask to isolate bits 1-2                 */

/* ── Static flag (bit 0) ──────────────────────────────────────────────────────── */
#define C_STRING_IS_STATIC 0x01u /* 1 = fixed-capacity, never reallocated    */

/* ── Encoding tag (bits 3-4) ──────────────────────────────────────────────────── */
#define C_STRING_ENC_ASCII 0x00u /* 0000 0000 – raw bytes / ASCII            */
#define C_STRING_ENC_UTF8 0x08u  /* 0000 1000 – UTF-8                        */
#define C_STRING_ENC_UTF16 0x10u /* 0001 0000 – UTF-16 LE (default for UTF-16) */
#define C_STRING_ENC_MASK 0x18u  /* mask to isolate bits 3-4                 */

/* ── Growth policy sentinel ───────────────────────────────────────────────────── */
/** Allocation growth switches from doubling to linear +1 MB chunks above this threshold. */
#define C_STRING_ONE_MEGABYTE 1048576u

/*==================================================================================
	SECTION 3 – TYPE DEFINITIONS  (must precede any macro or inline function that
							 uses these types)
==================================================================================*/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Mutable pointer to a convey string's character data.
 * The header lives immediately before this pointer: string[-1] is type_flags,
 * and the preceding bytes are the used_length and allocated_capacity fields.
 */
typedef uint8_t* c_string_t;

/** Immutable variant of c_string_t; used for read-only parameters. */
typedef const uint8_t* c_const_string_t;

/** Generic byte-view slice (ASCII / raw bytes).  Zero-copy; does not own its data. */
typedef struct {
 const uint8_t* data;
 size_t length;
} c_slice_t;

/** UTF-8 byte-view slice.  `length` is in bytes (not codepoints).  Zero-copy. */
typedef struct {
 const uint8_t* data;
 size_t length;
} c_utf8_slice_t;

/**
 * UTF-16 byte-view slice.  `length` is ALWAYS in bytes (not code units) and is
 * always even.  Zero-copy; the underlying buffer is UTF-16 LE unless a
 * conversion function was told otherwise.
 */
typedef struct {
 const uint8_t* data;
 size_t length;
} c_utf16_slice_t;

/*==================================================================================
	SECTION 4 – PACKED VARIABLE-WIDTH HEADERS
	Layout (packed, little-endian):
		[ used_length | allocated_capacity | type_flags ] [ ... data ... ] [ NUL(s) ]
	The type_flags byte is always the LAST byte of the header (string[-1]).
	used_length and allocated_capacity are in BYTES and exclude the NUL terminator(s).
==================================================================================*/

#if defined(_MSC_VER)
#pragma pack(push, 1)
#define C_STRING_PACKED
#elif defined(__GNUC__) || defined(__clang__)
#define C_STRING_PACKED __attribute__((__packed__))
#else
#define C_STRING_PACKED
#endif

struct C_STRING_PACKED c_string_header_8 {
 uint8_t used_length;
 uint8_t allocated_capacity;
 unsigned char type_flags;
 uint8_t character_buffer[];
};
struct C_STRING_PACKED c_string_header_16 {
 uint16_t used_length;
 uint16_t allocated_capacity;
 unsigned char type_flags;
 uint8_t character_buffer[];
};
struct C_STRING_PACKED c_string_header_32 {
 uint32_t used_length;
 uint32_t allocated_capacity;
 unsigned char type_flags;
 uint8_t character_buffer[];
};
struct C_STRING_PACKED c_string_header_64 {
 uint64_t used_length;
 uint64_t allocated_capacity;
 unsigned char type_flags;
 uint8_t character_buffer[];
};

#if defined(_MSC_VER)
#pragma pack(pop)
#endif

/*==================================================================================
	SECTION 5 – PUBLIC API  (all function declarations)
	Ordered by logical group; implementations live in convey_main.c.
==================================================================================*/

/* ── Lifecycle & Constructors ─────────────────────────────────────────────────── */

/**
 * Create a new dynamic string from a NUL-terminated C string.
 * Tags the result C_STRING_ENC_ASCII.  Use c_string_create_from_utf8() when the
 * content contains multi-byte UTF-8 sequences.
 * Returns NULL on allocation failure or if string_parameter is NULL.
 */
c_string_t c_string_create(const char* string_parameter);

/**
 * Low-level constructor.  Copies `initial_length` bytes from `initial_data` into
 * a fresh allocation tagged with `encoding`.  Pass NULL for `initial_data` to get a
 * zero-filled buffer of `initial_length` bytes instead.
 * For UTF-16, `initial_length` must be in bytes; odd counts are rounded down.
 * Returns NULL on allocation failure or arithmetic overflow.
 */
c_string_t c_string_create_pro(const void* initial_data, size_t initial_length, unsigned char encoding);

/**
 * Create a fixed-capacity (static) string.
 * Total allocated capacity = strlen(string_parameter) + extra_capacity.
 * Static strings never reallocate; append functions silently do nothing when the
 * capacity would be exceeded.
 * Returns NULL on arithmetic overflow or allocation failure.
 */
c_string_t c_string_create_static(const char* string_parameter, size_t extra_capacity);

/**
 * Low-level constructor for a static (fixed-capacity) string.
 * `total_capacity` is the absolute byte capacity to reserve (excluding header and NUL).
 * `initial_length` bytes are copied from `initial_data` (or zero-filled if NULL).
 * Fails if initial_length > total_capacity.
 */
c_string_t c_string_create_static_pro(const void* initial_data, size_t initial_length, size_t total_capacity, unsigned char encoding);

/** Free a dynamic or static string.  Safe to call with NULL (no-op). */
void c_string_free(c_string_t string);

/**
 * Deep-copy a string, preserving its encoding, content, and used-length.
 * The copy is always dynamic, even if the source is static.
 * Returns NULL on allocation failure.
 */
c_string_t c_string_duplicate(c_const_string_t string);

/** Reset the used-length to 0 and write the appropriate NUL terminator(s) in-place. */
void c_string_clear(c_string_t string);

/** Return true when `string` is NULL or has zero used bytes. */
bool c_string_is_empty(c_const_string_t string);

/* ── Slice Constructors ───────────────────────────────────────────────────────── */

/** Create a new ASCII-tagged dynamic string from a raw byte slice. */
c_string_t c_string_from_slice(c_slice_t slice);

/** Create a new UTF-8-tagged dynamic string from a UTF-8 byte slice. */
c_string_t c_string_from_utf8_slice(c_utf8_slice_t slice);

/**
 * Create a new UTF-16-tagged dynamic string from a UTF-16 byte slice.
 * `slice.length` must be in bytes; odd values are rounded down to even.
 * The result is double-NUL-terminated.
 */
c_string_t c_string_from_utf16_slice(c_utf16_slice_t slice);

/**
 * Create a new UTF-16-tagged dynamic string from an array of `unit_count`
 * uint16_t code units.  The result is double-NUL-terminated.
 * Returns NULL if units is NULL and unit_count > 0, or on overflow.
 */
c_string_t c_string_create_from_utf16(const uint16_t* units, size_t unit_count);

/* ── Appending & Concatenation ───────────────────────────────────────────────── */

/**
 * Append a NUL-terminated C string to `string`.
 *
 * The caller MUST reassign from the return value, as the pointer may change on
 * reallocation:
 *   str = c_string_append(str, " world");
 *
 * On allocation failure or when `string` is static with insufficient remaining
 * capacity, returns the original pointer unmodified (no data is lost).
 */
c_string_t c_string_append(c_string_t string, const char* string_parameter);

/**
 * Append `length` raw bytes from `data` to `string`.
 * Self-overlap is handled safely: appending a subslice of the same string is
 * correct even after a reallocation moves the buffer.
 * The caller MUST reassign from the return value.
 * Returns the original pointer unmodified on failure.
 */
c_string_t c_string_append_pro(c_string_t string, const void* data, size_t length);

/** Append a raw byte slice to `string`. */
c_string_t c_string_append_slice(c_string_t string, c_slice_t slice);

/**
 * Append a UTF-8 slice to `string`.
 * If the destination is ASCII-tagged it is automatically re-tagged UTF-8.
 */
c_string_t c_string_append_utf8(c_string_t string, c_utf8_slice_t slice);

/**
 * Append a UTF-16 slice (raw bytes) to `string`.
 * Odd byte lengths are rounded down to the nearest even value before appending.
 */
c_string_t c_string_append_utf16(c_string_t string, c_utf16_slice_t slice);

/**
 * Return a new dynamic string that is `a` concatenated with `b`.
 * The result inherits `a`'s encoding.
 * Returns NULL on allocation failure or arithmetic overflow.
 */
c_string_t c_string_concat(c_const_string_t a, c_const_string_t b);

/* ── Capacity Management ──────────────────────────────────────────────────────── */

/**
 * Ensure at least `additional_bytes` of free space beyond the current used-length.
 *
 * Growth policy:
 *   · Below 1 MB: double the current capacity.
 *   · At or above 1 MB: grow by 1 MB chunks.
 *
 * The caller MUST reassign from the return value.
 * Returns the original pointer unmodified on failure or for static strings.
 */
c_string_t c_string_ensure_capacity(c_string_t string, size_t additional_bytes);

/**
 * Pre-allocate `additional_bytes` of free space.
 * Alias of c_string_ensure_capacity(); provided for semantic clarity at call sites
 * where the intent is to reserve ahead of a known series of appends.
 */
c_string_t c_string_reserve(c_string_t string, size_t additional_bytes);

/**
 * Shrink the allocation to exactly fit the current used-length plus NUL terminator(s).
 * The caller MUST reassign from the return value.
 * No-op for static strings (returned unmodified).
 */
c_string_t c_string_shrink_to_fit(c_string_t string);

/** Call c_string_shrink_to_fit() on every non-NULL entry of `array` in-place. */
void c_string_array_shrink_to_fit(c_string_t* array, size_t count);

/* ── Slice Extractors ─────────────────────────────────────────────────────────── */

/** Return a zero-copy view of the entire string as a raw byte slice. */
c_slice_t c_slice_of(c_const_string_t string);

/**
 * Return a zero-copy subslice by byte offset and byte length.
 * Both values are clamped to the string's used-length; the result is never
 * out-of-bounds.
 */
c_slice_t c_subslice(c_const_string_t string, size_t byte_offset, size_t byte_length);

/**
 * Return a zero-copy subslice by logical codepoint offset and count.
 * Dispatches on the string's encoding tag:
 *   ASCII  – each byte is one codepoint (O(n) but trivially fast).
 *   UTF-8  – walks multi-byte sequences with utf8proc_iterate.
 *   UTF-16 – accounts for surrogate pairs.
 * The result is 2-byte-aligned for UTF-16 strings.
 */
c_slice_t c_subslice_codepoints(c_const_string_t string, size_t cp_offset, size_t cp_count);

/** Zero-copy view of the entire string as a UTF-8 byte slice. */
c_utf8_slice_t c_utf8_slice_of(c_const_string_t string);

/** UTF-8 subslice by byte offset and byte length (clamped to bounds). */
c_utf8_slice_t c_utf8_subslice(c_const_string_t string, size_t byte_offset, size_t byte_length);

/** UTF-8 subslice by codepoint offset and count (uses utf8proc_iterate). */
c_utf8_slice_t c_utf8_subslice_codepoints(c_const_string_t string, size_t cp_offset, size_t cp_count);

/**
 * Zero-copy view of the entire string as a UTF-16 byte slice.
 * `length` is always in bytes (always even).
 */
c_utf16_slice_t c_utf16_slice_of(c_const_string_t string);

/**
 * UTF-16 subslice by byte offset and byte length.
 * Both values are rounded down to even (2-byte alignment) before clamping.
 */
c_utf16_slice_t c_utf16_subslice(c_const_string_t string, size_t byte_offset, size_t byte_length);

/** UTF-16 subslice by codepoint offset and count (surrogate-pair-aware). */
c_utf16_slice_t c_utf16_subslice_codepoints(c_const_string_t string, size_t cp_offset, size_t cp_count);

/**
 * Return the number of UTF-16 code units (uint16_t values) in a slice.
 * Equal to slice.length / 2.
 */
size_t c_utf16_slice_unit_count(c_utf16_slice_t slice);

/* ── Encoding Converters ──────────────────────────────────────────────────────── */

/**
 * Validate and NFC-normalise a NUL-terminated UTF-8 string.
 * Returns a new convey string tagged C_STRING_ENC_UTF8.
 * Returns NULL if the input is NULL, contains invalid UTF-8, or allocation fails.
 */
c_string_t c_string_create_from_utf8(const char* text);

/**
 * Like c_string_create_from_utf8(), but exposes the full set of utf8proc option
 * flags.  UTF8PROC_NULLTERM is always added internally; the input must be
 * NUL-terminated.
 */
c_string_t c_string_create_from_utf8_pro(const char* text, utf8proc_option_t options);

/**
 * Convert a convey UTF-8 (or ASCII) string to a new convey UTF-16 LE string.
 * The result is tagged C_STRING_ENC_UTF16 and is double-NUL-terminated.
 * Returns NULL on invalid UTF-8, lone surrogates in the input, or allocation failure.
 */
c_string_t c_string_convert_utf8_utf16(c_const_string_t utf8_input);

/**
 * Like c_string_convert_utf8_utf16(), but allows selecting the output byte order.
 * Pass use_big_endian=true for UTF-16 BE (used by Java and some network protocols).
 */
c_string_t c_string_convert_utf8_utf16_pro(c_const_string_t utf8_input, bool use_big_endian);

/**
 * Convert a convey UTF-16 LE string to a new convey UTF-8 string.
 * Lone surrogates that cannot be paired are replaced with U+FFFD (replacement char).
 * Result is tagged C_STRING_ENC_UTF8.
 */
c_string_t c_string_convert_utf16_utf8(c_const_string_t utf16_input);

/**
 * Like c_string_convert_utf16_utf8(), but allows selecting the source byte order.
 * Pass use_big_endian=true when the UTF-16 data is stored as big-endian.
 */
c_string_t c_string_convert_utf16_utf8_pro(c_const_string_t utf16_input, bool use_big_endian);

/**
 * Convert a static string to a dynamic string.
 * If the string is already dynamic, returns it unchanged.
 * On allocation failure, returns the original static string as a fallback.
 * On success, the old static string is freed and a new dynamic string is returned.
 */
c_string_t c_string_to_dynamic(c_string_t string);

/**
 * Print a UTF-16 slice to stdout by first converting to UTF-8.
 * Works correctly on all platforms (Linux, macOS, Windows terminal).
 *
 *   c_string_print_utf16(C_UTF16_SLICE_OF(my_str));
 *   c_string_print_utf16(C_UTF16_SUBSLICE_CP(my_str, 0, 10));
 */
void c_string_print_utf16(c_utf16_slice_t slice);

/* ── Case Conversion  (UTF-8 / ASCII only) ────────────────────────────────────── */

/**
 * Return a new NFC-normalised, case-folded (Unicode full lowercase) copy.
 * Uses UTF8PROC_CASEFOLD | UTF8PROC_STABLE | UTF8PROC_COMPOSE via utf8proc_map.
 * Input must be UTF-8 or ASCII tagged; returns NULL for UTF-16 or on failure.
 * Result is tagged C_STRING_ENC_UTF8.
 */
c_string_t c_string_to_lower(c_const_string_t string);

/**
 * Return a new NFC-normalised, uppercased copy.
 * Uses utf8proc_toupper() per codepoint via utf8proc_map_custom().
 * Input must be UTF-8 or ASCII tagged; returns NULL for UTF-16 or on failure.
 * Result is tagged C_STRING_ENC_UTF8.
 */
c_string_t c_string_to_upper(c_const_string_t string);

/* ── Searching & Comparison ───────────────────────────────────────────────────── */

/**
 * Bytewise lexicographic comparison.
 * Returns < 0 when a < b, 0 when equal, > 0 when a > b.
 * NULL sorts before any non-NULL string.
 * For NFC-normalised UTF-8 strings this matches Unicode codepoint order.
 */
int c_string_compare(c_const_string_t a, c_const_string_t b);

/**
 * Return true when `a` and `b` have identical byte content and length.
 * Encoding tags are NOT compared; use C_STRING_ENC_OF() if you need that check.
 */
bool c_string_equal(c_const_string_t a, c_const_string_t b);

/**
 * Find the first occurrence of `needle` inside `haystack` (bytewise search).
 * Returns the byte offset on success.
 * Returns SIZE_MAX when not found or when either argument is NULL.
 * An empty needle (zero used_length) always returns 0.
 */
size_t c_string_find(c_const_string_t haystack, c_const_string_t needle);

/**
 * Return true when `needle` occurs anywhere inside `str`.
 * Equivalent to (c_string_find(str, needle) != SIZE_MAX).
 */
bool c_string_contains(c_const_string_t str, c_const_string_t needle);

/**
 * Return true when `str` begins with the bytes of `prefix`.
 * An empty prefix always returns true.  NULL arguments return false.
 */
bool c_string_starts_with(c_const_string_t str, c_const_string_t prefix);

/**
 * Return true when `str` ends with the bytes of `suffix`.
 * An empty suffix always returns true.  NULL arguments return false.
 */
bool c_string_ends_with(c_const_string_t str, c_const_string_t suffix);

/* ── In-Place Mutation ────────────────────────────────────────────────────────── */

/**
 * Strip ASCII whitespace (' ' '\\t' '\\n' '\\r') from the right end in-place.
 * For UTF-16 strings the corresponding UTF-16 LE code units are stripped.
 * Never reallocates; always returns the same pointer.
 * Returns NULL for a NULL input.
 */
c_string_t c_string_trim_right(c_string_t string);

/**
 * Strip ASCII whitespace from the left end in-place using memmove.
 * For UTF-16 the corresponding UTF-16 LE code units are stripped.
 * Never reallocates; always returns the same pointer.
 * Returns NULL for a NULL input.
 */
c_string_t c_string_trim_left(c_string_t string);

/**
 * Strip ASCII whitespace from both ends in-place.
 * Equivalent to c_string_trim_right(c_string_trim_left(string)).
 */
c_string_t c_string_trim(c_string_t string);

/**
 * Return a new dynamic string containing `string` repeated `count` times.
 * Encoding is inherited from the source.
 * Returns an empty string for count == 0.
 * Returns NULL on arithmetic overflow or allocation failure.
 */
c_string_t c_string_repeat(c_const_string_t string, size_t count);

/* ── Validation ───────────────────────────────────────────────────────────────── */

/**
 * Return true when every byte sequence in `string` is valid UTF-8.
 * Uses utf8proc_iterate() per codepoint.
 * Always returns true for NULL.
 */
bool c_string_validate_utf8(c_const_string_t string);

/* ── Internal Getters & Setters ───────────────────────────────────────────────── */

/**
 * Return the byte size of the header struct for the given raw type_flags byte.
 * Internally masks with C_STRING_TYPE_MASK, so the full flags byte may be passed.
 */
size_t c_string_get_header_size(unsigned char flags);

/**
 * Return the smallest header class constant (C_STRING_TYPE_8/16/32/64) capable
 * of storing a length or capacity value of `size` bytes.
 */
unsigned char c_string_determine_type(size_t size);

/**
 * Return the encoding tag (C_STRING_ENC_ASCII / UTF8 / UTF16) from the header.
 * Returns C_STRING_ENC_ASCII for a NULL string.
 */
unsigned char c_string_get_encoding(c_const_string_t string);

/**
 * Re-tag the string's encoding byte in-place WITHOUT converting any data.
 * Use with care: only call this when you have verified that the byte content
 * already matches the new encoding (e.g. after confirming a UTF-8 string
 * contains only ASCII codepoints).
 */
void c_string_set_encoding(c_string_t string, unsigned char enc);

/**
 * Return the current used byte count (excludes NUL terminator(s)).
 * Returns 0 for NULL.
 */
size_t c_string_get_used_length(c_const_string_t string);

/**
 * Return the number of free bytes currently available (allocated_capacity –
 * used_length, excluding NUL bytes).
 * Returns 0 for NULL or a full string.
 */
size_t c_string_get_available_capacity(c_const_string_t string);

/**
 * Return the total allocated data capacity in bytes (used + spare, excluding NUL).
 * Equal to used_length + available_capacity.
 * Returns 0 for NULL.
 */
size_t c_string_get_capacity(c_const_string_t string);

/**
 * Count logical codepoints, respecting the string's encoding tag:
 *   ASCII  – returns used_length.
 *   UTF-8  – counts by walking multi-byte sequences; invalid bytes count as one each.
 *   UTF-16 – counts code-unit pairs as one codepoint, lone units as one each.
 * Returns 0 for NULL.
 */
size_t c_string_codepoint_count(c_const_string_t string);

/**
 * Force both the used-length and allocated-capacity fields in the header.
 * Does NOT write NUL terminators – call this only when you know the
 * terminator bytes are already correct (e.g. immediately after bulk memcpy).
 * For UTF-16 strings, both values are rounded down to even automatically.
 */
void c_string_set_lengths(c_string_t string, size_t used, size_t capacity);

/**
 * Force only the used-length field in the header.
 * For UTF-16 strings, the value is rounded down to even automatically.
 */
void c_string_set_used_length(c_string_t string, size_t used);

#ifdef __cplusplus
}
#endif

/*==================================================================================
	SECTION 6 – ENCODING PROPERTY MACROS
	These use c_string_get_encoding() which is declared in Section 5 above.
==================================================================================*/

/** True when `str` is non-NULL and carries the static/fixed-capacity flag. */
#define C_STRING_IS_STATIC_STR(str) (((str) != NULL) && (((str)[-1] & C_STRING_IS_STATIC) != 0))

/**
 * Null-terminator byte count for a given encoding constant.
 * Returns 2 for UTF-16, 1 for everything else.
 * Use this instead of hardcoding 1 or 2 whenever NUL size matters.
 */
#define C_STRING_NULL_SIZE(enc) (((enc) & C_STRING_ENC_MASK) == C_STRING_ENC_UTF16 ? 2u : 1u)

/** Extract the encoding tag (C_STRING_ENC_*) from a live string. */
#define C_STRING_ENC_OF(str) c_string_get_encoding(str)

/** True when `str` is tagged C_STRING_ENC_ASCII. */
#define C_STRING_IS_ASCII(str) (c_string_get_encoding(str) == C_STRING_ENC_ASCII)

/** True when `str` is tagged C_STRING_ENC_UTF8. */
#define C_STRING_IS_UTF8(str) (c_string_get_encoding(str) == C_STRING_ENC_UTF8)

/** True when `str` is tagged C_STRING_ENC_UTF16. */
#define C_STRING_IS_UTF16(str) (c_string_get_encoding(str) == C_STRING_ENC_UTF16)

/** True when `str` uses single-byte units (ASCII or UTF-8, i.e. not UTF-16). */
#define C_STRING_IS_BYTE_ENC(str) (!C_STRING_IS_UTF16(str))

/** Reinterpret a convey UTF-16 string buffer as a const uint16_t pointer. */
#define C_STRING_AS_U16(str) ((const uint16_t*)(str))

/*==================================================================================
	SECTION 7 – INTERNAL HEADER-ACCESS MACROS
	These cast the data pointer backwards into the packed header struct.
	They are intended for use inside convey_main.c only – prefer the public API
	(c_string_get_used_length, c_string_get_capacity, etc.) in application code.
==================================================================================*/

/* Mutable header access */
#define GET_HEADER_8(ptr) ((struct c_string_header_8*)((ptr) - sizeof(struct c_string_header_8)))
#define GET_HEADER_16(ptr) ((struct c_string_header_16*)((ptr) - sizeof(struct c_string_header_16)))
#define GET_HEADER_32(ptr) ((struct c_string_header_32*)((ptr) - sizeof(struct c_string_header_32)))
#define GET_HEADER_64(ptr) ((struct c_string_header_64*)((ptr) - sizeof(struct c_string_header_64)))

/* Read-only header access */
#define GET_CONST_HEADER_8(ptr) ((const struct c_string_header_8*)((ptr) - sizeof(struct c_string_header_8)))
#define GET_CONST_HEADER_16(ptr) ((const struct c_string_header_16*)((ptr) - sizeof(struct c_string_header_16)))
#define GET_CONST_HEADER_32(ptr) ((const struct c_string_header_32*)((ptr) - sizeof(struct c_string_header_32)))
#define GET_CONST_HEADER_64(ptr) ((const struct c_string_header_64*)((ptr) - sizeof(struct c_string_header_64)))

/*==================================================================================
	SECTION 8 – printf / wprintf FORMAT HELPERS  (C11 _Generic)
==================================================================================*/

#define C_STR_FMT "%.*s"
#define C_WSTR_FMT L"%.*ls"

/* ──────────────────────────────────────────────────────────────────────────────────
 * INTERNAL HELPERS & DISPATCHER
 * ──────────────────────────────────────────────────────────────────────────────── */

/* Internal helper struct – do not use directly. */
typedef struct {
 int len;
 const char* ptr;
} c_str_fmt_arg_t;

/* Internal helper functions – not part of the public API. */
static inline c_str_fmt_arg_t _c_fmt_cstr(const char* s) {
 return (c_str_fmt_arg_t){s ? (int)strlen(s) : 0, s ? s : ""};
}
static inline c_str_fmt_arg_t _c_fmt_str(const uint8_t* s) {
 return (c_str_fmt_arg_t){(int)c_string_get_used_length(s), s ? (const char*)s : ""};
}
static inline c_str_fmt_arg_t _c_fmt_slice(c_slice_t s) {
 return (c_str_fmt_arg_t){(int)s.length, s.data ? (const char*)s.data : ""};
}
static inline c_str_fmt_arg_t _c_fmt_utf8_slice(c_utf8_slice_t s) {
 return (c_str_fmt_arg_t){(int)s.length, s.data ? (const char*)s.data : ""};
}

/* Internal dispatcher – do not use directly. */
#define C_FMT_ARG(X) _Generic((X), char*: _c_fmt_cstr, const char*: _c_fmt_cstr, uint8_t*: _c_fmt_str, const uint8_t*: _c_fmt_str, c_slice_t: _c_fmt_slice, c_utf8_slice_t: _c_fmt_utf8_slice)(X)

/* ──────────────────────────────────────────────────────────────────────────────────
 * STANDARD (double-evaluation) MACROS
 * ──────────────────────────────────────────────────────────────────────────────── */

#define C_str_len(X) (C_FMT_ARG(X).len)
#define C_str_ptr(X) (C_FMT_ARG(X).ptr)
#define C_str_arg(X) C_str_len(X), C_str_ptr(X)

/* ──────────────────────────────────────────────────────────────────────────────────
 * SAFE (single-evaluation) STATEMENT MACROS
 * ──────────────────────────────────────────────────────────────────────────────── */

#define C_PRINTF_SAFE(fmt, expr)              \
 do {                                         \
	c_str_fmt_arg_t _csa_tmp = C_FMT_ARG(expr); \
	printf((fmt), _csa_tmp.len, _csa_tmp.ptr);  \
 } while (0)

/* ──────────────────────────────────────────────────────────────────────────────────
 * UTF-16 PRINTF EQUIVALENTS  (Windows / wchar_t targets only)
 * ──────────────────────────────────────────────────────────────────────────────── */
#if WCHAR_MAX == 0xFFFFu || defined(_WIN32)

/* Internal helper struct for safe UTF-16 print – do not use directly. */
typedef struct {
 int len;
 const wchar_t* ptr;
} c_wstr_fmt_arg_t;

static inline c_wstr_fmt_arg_t _c_wfmt_str(const uint8_t* s) {
 return (c_wstr_fmt_arg_t){(int)(c_string_get_used_length(s) / 2u), s ? (const wchar_t*)s : L""};
}
static inline c_wstr_fmt_arg_t _c_wfmt_utf16_slice(c_utf16_slice_t s) {
 return (c_wstr_fmt_arg_t){(int)(s.length / 2u), s.data ? (const wchar_t*)s.data : L""};
}

/* Internal dispatcher – do not use directly. */
#define C_WFMT_ARG(X) _Generic((X), uint8_t *: _c_wfmt_str, const uint8_t*: _c_wfmt_str, c_utf16_slice_t: _c_wfmt_utf16_slice)(X)

#define C_wstr_len(X) (C_WFMT_ARG(X).len)
#define C_wstr_ptr(X) (C_WFMT_ARG(X).ptr)
#define C_wstr_arg(X) C_wstr_len(X), C_wstr_ptr(X)

#define C_WPRINTF_SAFE(fmt, expr)                \
 do {                                            \
	c_wstr_fmt_arg_t _cwsa_tmp = C_WFMT_ARG(expr); \
	wprintf((fmt), _cwsa_tmp.len, _cwsa_tmp.ptr);  \
 } while (0)

#endif /* WCHAR_MAX == 0xFFFF || _WIN32 */
/*==================================================================================
	SECTION 9 – SLICE COMPOUND-LITERAL HELPERS
	All of these use c_string_get_used_length(), which is declared in Section 5.
==================================================================================*/

/** Zero-copy view of the entire string as a c_slice_t. */
#define C_SLICE_OF(str) ((c_slice_t){(str), c_string_get_used_length(str)})

/** Zero-copy view of the entire string as a c_utf8_slice_t. */
#define C_UTF8_SLICE_OF(str) ((c_utf8_slice_t){(str), c_string_get_used_length(str)})

/**
 * Zero-copy view of the entire string as a c_utf16_slice_t.
 * Length is in bytes (always even for valid UTF-16 strings).
 */
#define C_UTF16_SLICE_OF(str) ((c_utf16_slice_t){(str), c_string_get_used_length(str)})

/** Extract a byte-offset / byte-length subslice (generic). */
#define C_SUBSLICE(str, off, len) c_subslice((str), (off), (len))

/**
 * Codepoint-indexed subslice.  Dispatches on the string's encoding tag and
 * returns a zero-copy view.  Works for ASCII, UTF-8, and UTF-16.
 */
#define C_SUBSLICE_CP(str, off, cnt) c_subslice_codepoints((str), (off), (cnt))

/** UTF-8 subslice by byte offset + byte length. */
#define C_UTF8_SUBSLICE(str, o, l) c_utf8_subslice((str), (o), (l))

/** UTF-8 subslice by codepoint offset + count. */
#define C_UTF8_SUBSLICE_CP(str, o, cnt) c_utf8_subslice_codepoints((str), (o), (cnt))

/** UTF-16 subslice by byte offset + byte length (both rounded to even). */
#define C_UTF16_SUBSLICE(str, o, l) c_utf16_subslice((str), (o), (l))

/** UTF-16 subslice by codepoint offset + count (surrogate-pair-aware). */
#define C_UTF16_SUBSLICE_CP(str, o, cnt) c_utf16_subslice_codepoints((str), (o), (cnt))

#endif /* CONVEY_H */
