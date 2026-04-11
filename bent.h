// bent.h  –  b_str  Unicode string library
//
// Quick-start:
//   b_str_t string = b_str_new("hello");
//   string = b_str_append(string, " world");  // DYNAMIC: always reassign
//   printf("%.*s\n", (int)b_str_len(string), string);
//   b_str_free(string);
//
// All functions that return b_str_t follow the same contract:
//   – Never return NULL when the input string is non-NULL (they return string on OOM/failure).
//   – DYNAMIC strings: the returned pointer may differ from the input; always reassign.
//   – STATIC  strings: the returned pointer is ALWAYS the same; assignment is never
//     required. If an operation cannot be performed without reallocating (e.g. the
//     buffer is full), it fails silently and leaves the string unchanged.

#include "SDL3/SDL.h"
#include "utf8proc.h"

#ifdef __cplusplus
extern "C" {
#endif
    
// Uncomment to use aligned (padded) headers instead of packed ones.
// Packed is the default; it uses less memory.
//#define b_ALIGNED


// 
//  CORE TYPES
// 

// b_str_t  –  mutable string handle; treat as a uint8_t* to the first byte.
// b_cstr_t –  const (read-only) string handle.
// The header lives at string[-header_size], not at string itself.
typedef       uint8_t *b_str_t;
typedef const uint8_t *b_cstr_t;

// Slice: a (data pointer, byte-length) view into existing memory.
// Not null-terminated. Never owns the data.
typedef struct b_byte_slice  { const uint8_t *data; size_t byte_length; } b_slice_t;
typedef struct b_utf8_slice  { const uint8_t *data; size_t byte_length; } b_u8slice_t;
typedef struct b_utf16_slice { const uint8_t *data; size_t byte_length; } b_u16slice_t; // byte_length must be a multiple of 2
typedef struct b_utf32_slice { const uint8_t *data; size_t byte_length; } b_u32slice_t; // byte_length must be a multiple of 4


// 
//  ENCODING CONSTANTS
// 

// Pass these to _pro functions that take an explicit encoding argument.
#define B_STR_ENC_ASCII   0x00u  // 1 byte per code-unit, plain ASCII/Latin-1
#define B_STR_ENC_UTF8    0x10u  // variable-width, ASCII-compatible
#define B_STR_ENC_UTF16BE 0x20u  // big-endian UTF-16
#define B_STR_ENC_UTF16LE 0x30u  // little-endian UTF-16
#define B_STR_ENC_UTF32BE 0x40u  // big-endian UTF-32
#define B_STR_ENC_UTF32LE 0x50u  // little-endian UTF-32
// Aliases for "native" LE variants (the common choice on x86/ARM)
#define B_STR_ENC_UTF16   B_STR_ENC_UTF16LE
#define B_STR_ENC_UTF32   B_STR_ENC_UTF32LE

// Encoding predicates – useful for branching without memorising the constants.
#define B_STR_IS_UTF16_ENC(encoding) (((encoding) & 0x60u) == 0x20u)
#define B_STR_IS_UTF32_ENC(encoding) (((encoding) & 0x60u) == 0x40u)


// 
//  CORE ACCESSORS
// 

// b_str_hdr_size  – byte size of the header that precedes the string data.
size_t  b_str_hdr_size (uint8_t flags);

// b_str_pick_type – choose the smallest header class that fits byte_size.
uint8_t b_str_pick_type(size_t byte_size);

// b_str_enc   – encoding tag stored in the header.
uint8_t b_str_enc      (b_cstr_t string);

// b_str_set_enc – overwrite the encoding tag (does NOT re-encode the bytes).
void    b_str_set_enc  (b_str_t string, uint8_t encoding);

// b_str_len   – byte length of the string content (not including null term).
size_t  b_str_len      (b_cstr_t string);

// b_str_cap   – total byte capacity (used + available).
size_t  b_str_cap      (b_cstr_t string);

// b_str_avail – free bytes remaining before a reallocation is needed.
size_t  b_str_avail    (b_cstr_t string);

// b_str_set_lens – forcibly update both the used-length and capacity fields.
void    b_str_set_lens (b_str_t string, size_t used_bytes, size_t capacity_bytes);

// b_str_set_len  – update just the used-length field.
void    b_str_set_len  (b_str_t string, size_t used_bytes);

// b_str_cpcount  – count Unicode code-points (handles UTF-8/16/32 surrogates).
size_t  b_str_cpcount  (b_cstr_t string);


// 
//  LIFECYCLE
// 

// b_str_new – create a dynamic string from a null-terminated C string (ASCII).
b_str_t b_str_new(const char *c_string);

// b_str_new_pro – create a dynamic string from raw bytes with explicit encoding.
// Pass data=NULL and byte_length>0 to allocate a zero-filled buffer of that size.
b_str_t b_str_new_pro(const void *data, size_t byte_length, uint8_t encoding);

// b_str_new_static – create a STATIC (fixed-capacity) string from a C string.
// extra_bytes reserves that many additional writable bytes beyond the content.
// Static strings are never reallocated; b_str_ensure returns them unchanged.
b_str_t b_str_new_static(const char *c_string, size_t extra_bytes);

// b_str_new_static_pro – create a STATIC string with full control.
b_str_t b_str_new_static_pro(const void *data, size_t byte_length,
                              size_t total_capacity, uint8_t encoding);

// b_str_free – release the string. Safe to call with NULL.
void    b_str_free(b_str_t string);

// b_str_dup  – deep copy, same encoding. Returns NULL on OOM.
b_str_t b_str_dup(b_cstr_t string);

// b_str_clear – reset content to empty without freeing the allocation.
void    b_str_clear(b_str_t string);

// b_str_empty – true when string is NULL or has zero content bytes.
bool    b_str_empty(b_cstr_t string);

// b_str_to_dyn – if string is already DYNAMIC, returns string unchanged.
// STATIC strings: does nothing and returns NULL – the static buffer is never
// freed or moved, so every pointer held by the caller stays valid.
// To get an independent dynamic copy of a static string, use b_str_dup().
b_str_t b_str_to_dyn(b_str_t string);


// 
//  SLICE CONSTRUCTORS
// 

// Build a new b_str_t from a slice (allocates; caller owns the result).
b_str_t b_str_from_slice   (b_slice_t    slice);
b_str_t b_str_from_u8slice (b_u8slice_t  slice);
b_str_t b_str_from_u16slice(b_u16slice_t slice);  // encodes as UTF-16 LE
b_str_t b_str_from_u32slice(b_u32slice_t slice);  // encodes as UTF-32 LE
b_str_t b_str_from_u16     (const uint16_t *units, size_t unit_count);
b_str_t b_str_from_u32     (const uint32_t *units, size_t unit_count);


// 
//  CAPACITY MANAGEMENT
// 

// b_str_ensure – grow if needed so that at least extra_bytes of free space exist.
// STATIC strings: never reallocated; pointer is always returned unchanged.
// DYNAMIC strings: returned pointer may differ; always reassign the result.
// Returns string unchanged on OOM or when the string is already large enough.
b_str_t b_str_ensure (b_str_t string, size_t extra_bytes);

// b_str_reserve – alias for b_str_ensure (familiar name from C++ idiom).
b_str_t b_str_reserve(b_str_t string, size_t extra_bytes);

// b_str_fit – shrink allocation to exactly fit the current content. Frees slack.
// STATIC strings: returned unchanged; static buffers are never reallocated.
b_str_t b_str_fit(b_str_t string);

// b_str_arr_fit – call b_str_fit on every element of an array.
void    b_str_arr_fit(b_str_t *array, size_t array_count);


// 
//  APPENDING & CONCATENATION
// 

// All append variants return string (may be a new pointer for dynamic strings).
// They never return NULL when string is non-NULL.

// b_str_append – append a null-terminated C string.
b_str_t b_str_append(b_str_t string, const char *c_string);

// b_str_append_pro – append raw bytes (respects encoding alignment).
b_str_t b_str_append_pro(b_str_t string, const void *data, size_t byte_length);

// Typed slice appenders:
b_str_t b_str_append_sl (b_str_t string, b_slice_t    slice);
b_str_t b_str_append_u8 (b_str_t string, b_u8slice_t  slice);
b_str_t b_str_append_u16(b_str_t string, b_u16slice_t slice);
b_str_t b_str_append_u32(b_str_t string, b_u32slice_t slice);

// b_str_concat – allocate a new string = left_string + right_string. Returns NULL on OOM.
// Encoding is taken from left_string (or ASCII if left_string is NULL).
b_str_t b_str_concat(b_cstr_t left_string, b_cstr_t right_string);

// b_str_concat_pro – same but with an explicit output encoding.
b_str_t b_str_concat_pro(b_cstr_t left_string, b_cstr_t right_string, uint8_t encoding);


// 
//  SLICE EXTRACTORS
// 

// _of  variants: view the entire string as a slice (no copy, no alloc).
// sub  variants: byte-offset window into the string.
// _cp  variants: code-point-indexed window (slower; walks the string).

b_slice_t    b_slice_of      (b_cstr_t string);
b_slice_t    b_subslice      (b_cstr_t string, size_t byte_offset, size_t byte_length);
b_slice_t    b_subslice_cp   (b_cstr_t string, size_t codepoint_offset, size_t codepoint_count);

b_u8slice_t  b_u8slice_of    (b_cstr_t string);
b_u8slice_t  b_u8subslice    (b_cstr_t string, size_t byte_offset, size_t byte_length);
b_u8slice_t  b_u8subslice_cp (b_cstr_t string, size_t codepoint_offset, size_t codepoint_count);

b_u16slice_t b_u16slice_of   (b_cstr_t string);
b_u16slice_t b_u16subslice   (b_cstr_t string, size_t byte_offset, size_t byte_length);
b_u16slice_t b_u16subslice_cp(b_cstr_t string, size_t codepoint_offset, size_t codepoint_count);
size_t       b_u16slice_units(b_u16slice_t slice);  // byte_length / 2

b_u32slice_t b_u32slice_of   (b_cstr_t string);
b_u32slice_t b_u32subslice   (b_cstr_t string, size_t byte_offset, size_t byte_length);
b_u32slice_t b_u32subslice_cp(b_cstr_t string, size_t codepoint_offset, size_t codepoint_count);
size_t       b_u32slice_units(b_u32slice_t slice);  // byte_length / 4


// 
//  ENCODING CONVERTERS
// 

// On failure: return string unchanged (never NULL when string != NULL).
// STATIC  input: result written back in-place when it fits; pointer stays valid.
// DYNAMIC input: returns new b_str_t; caller must free the old pointer.

// b_str_utf8_norm – NFC-normalise a null-terminated UTF-8 string.
// Returns a new b_str_t, or NULL on failure. Does not modify the input.
b_str_t b_str_utf8_norm (const char *null_terminated_utf8);

b_str_t b_str_to_utf8   (b_str_t string);
b_str_t b_str_to_utf16  (b_str_t string);   // → UTF-16 LE
b_str_t b_str_to_utf16be(b_str_t string);   // → UTF-16 BE
b_str_t b_str_to_utf32le(b_str_t string);   // → UTF-32 LE
b_str_t b_str_to_utf32be(b_str_t string);   // → UTF-32 BE


// 
//  CASE CONVERSION
// 

// UTF-8 and ASCII only. UTF-16/32 strings are returned unchanged (silent fail).
// Same ownership contract as encoding converters above.
b_str_t b_str_lower(b_str_t string);
b_str_t b_str_upper(b_str_t string);


// 
//  COMPARISON & SEARCH
// 

// All comparisons are byte-level. NFC-normalise before calling for correct
// Unicode ordering (e.g. two strings that look identical may differ in bytes
// if one uses precomposed and the other uses decomposed code-points).

// b_str_cmp – lexicographic compare; returns <0, 0, or >0.
int    b_str_cmp        (b_cstr_t left_string, b_cstr_t right_string);
bool   b_str_eq         (b_cstr_t left_string, b_cstr_t right_string);

// b_str_find     – returns byte offset of first match, or SIZE_MAX if not found.
size_t b_str_find       (b_cstr_t haystack, b_cstr_t needle);

// b_str_find_pro – same, but search begins at from_byte_offset.
size_t b_str_find_pro   (b_cstr_t haystack, b_cstr_t needle, size_t from_byte_offset);

bool   b_str_contains   (b_cstr_t string, b_cstr_t needle);
bool   b_str_starts_with(b_cstr_t string, b_cstr_t prefix);
bool   b_str_ends_with  (b_cstr_t string, b_cstr_t suffix);


// 
//  IN-PLACE MUTATION
// 

// All return string. NULL in → NULL out.
b_str_t b_str_trim_r(b_str_t string);  // strip trailing whitespace
b_str_t b_str_trim_l(b_str_t string);  // strip leading whitespace
b_str_t b_str_trim  (b_str_t string);  // strip both ends

// b_str_repeat – new string = string repeated repeat_count times. May return NULL on overflow/OOM.
b_str_t b_str_repeat(b_cstr_t string, size_t repeat_count);


// 
//  VALIDATION
// 

// b_str_valid_utf8 – returns true if every byte sequence is valid UTF-8.
// NULL is considered valid (empty string).
bool b_str_valid_utf8(b_cstr_t string);


// 
//  BOM  (byte-order mark)
// 

// b_str_detect_bom – inspect raw bytes, return encoding tag and BOM size.
// bom_size_out may be NULL. Returns B_STR_ENC_ASCII when no BOM is found.
uint8_t b_str_detect_bom(const void *data, size_t byte_length, size_t *bom_size_out);

// b_str_add_bom – prepend the correct BOM for the string's encoding.
// DYNAMIC caller MUST reassign:  string = b_str_add_bom(string);
// STATIC  pointer stays valid; written in-place if it fits.
b_str_t b_str_add_bom(b_str_t string);


// 
//  FILE I/O  (synchronous, SDL IOStream)
//
//  All file I/O is done exclusively through SDL_IOFromFile / SDL_GetIOSize /
//  SDL_ReadIO / SDL_WriteIO / SDL_CloseIO — no fopen/fread/fclose anywhere.
// 

// b_str_load_file – read an entire file into a new b_str_t.
// BOM detection is automatic: if a BOM is found, its encoding takes precedence
// and the BOM bytes themselves are stripped from the returned string.
// fallback_encoding is used when no BOM is present; pass 0 to default to
// B_STR_ENC_UTF8. Returns NULL on any I/O error or allocation failure.
b_str_t b_str_load_file(const char *path, uint8_t fallback_encoding);

// b_str_save_file – write the string's bytes to a file via SDL_IOStream.
// Pass write_bom=true to prepend the correct byte-order mark for the string's
// encoding before the content. Returns 0 on success, -1 on any I/O error.
int b_str_save_file(const char *path, b_cstr_t string, bool write_bom);

// b_file_add_bom – prepend the correct BOM for 'encoding' to an existing file.
// Reads the file, checks whether the BOM is already present, and rewrites it
// only if needed. Returns 0 on success (or if nothing was needed), -1 on error.
int b_file_add_bom(const char *path, uint8_t encoding);


// 
//  FILE I/O  (asynchronous, SDL AsyncIO)
//
//  Typical async-load workflow:
//    1. b_str_load_file_async(path, queue, userdata)   – queue the read
//    2. SDL_WaitAsyncIOResult(queue, &outcome, -1)     – wait (or poll)
//    3. b_str_from_async_result(&outcome, fallback)    – build the b_str_t
//    4. SDL_free(outcome.buffer)                       – release SDL's buffer
// 

// b_str_load_file_async – queue an async file load via SDL_LoadFileAsync.
// Returns true if the task was successfully queued, false on error.
// When the task completes, call b_str_from_async_result() on the outcome.
bool b_str_load_file_async(const char *path, SDL_AsyncIOQueue *queue, void *userdata);

// b_str_from_async_result – build a b_str_t from a completed SDL_LoadFileAsync
// outcome. BOM detection works exactly like b_str_load_file. Returns NULL if
// outcome->result != SDL_ASYNCIO_COMPLETE or on allocation failure.
// IMPORTANT: the caller must SDL_free(outcome->buffer) after this call; that
// buffer belongs to SDL and is separate from the returned b_str_t.
b_str_t b_str_from_async_result(const SDL_AsyncIOOutcome *outcome, uint8_t fallback_encoding);


// 
//  FILE CONVERSION  (read in one encoding, write in another)
//
//  b_file_convert is the single generic entry point.
//  All the named b_file_conv_* wrappers below are thin aliases for it;
//  they exist only as a convenience so you don't have to spell out the
//  encoding constants yourself.
// 

// b_file_convert – convert a file from one encoding to another.
//
//   input_path      – source file path (read via SDL_IOStream).
//   output_path     – destination file path (written via SDL_IOStream; may equal
//                     input_path for an in-place conversion, but be careful).
//   fallback_encoding – encoding assumed when the source file has no BOM.
//                       Pass 0 to default to B_STR_ENC_UTF8.
//   output_encoding – target encoding for the output file (a B_STR_ENC_* constant).
//   write_bom       – true to write a byte-order mark at the start of output_path.
//
// Returns 0 on success, -1 on any I/O or conversion error.
int b_file_convert(const char *input_path, const char *output_path,
                   uint8_t fallback_encoding, uint8_t output_encoding, bool write_bom);

// ─── Convenience wrappers ────────────────────────────────────────────────────
// Each one calls b_file_convert with the obvious constant arguments.
// Use b_file_convert directly when you need more control (e.g. custom fallback).

int b_file_conv_ascii_to_utf8_bom            (const char *input_path, const char *output_path);
int b_file_conv_ascii_to_utf8_no_bom         (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf16                (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf16le_bom          (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf16le_no_bom       (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf16be_bom          (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf16be_no_bom       (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf32                (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf32le_bom          (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf32le_no_bom       (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf32be_bom          (const char *input_path, const char *output_path);
int b_file_conv_utf8_to_utf32be_no_bom       (const char *input_path, const char *output_path);
int b_file_conv_utf16_to_utf8_no_bom         (const char *input_path, const char *output_path);
int b_file_conv_utf16le_bom_to_utf8_no_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf16le_no_bom_to_utf8_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf16le_no_bom_to_utf8_no_bom(const char *input_path, const char *output_path);
int b_file_conv_utf16be_bom_to_utf8_no_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf16be_no_bom_to_utf8_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf16be_no_bom_to_utf8_no_bom(const char *input_path, const char *output_path);
int b_file_conv_utf32_to_utf8_no_bom         (const char *input_path, const char *output_path);
int b_file_conv_utf32le_bom_to_utf8_no_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf32le_no_bom_to_utf8_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf32le_no_bom_to_utf8_no_bom(const char *input_path, const char *output_path);
int b_file_conv_utf32be_bom_to_utf8_no_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf32be_no_bom_to_utf8_bom   (const char *input_path, const char *output_path);
int b_file_conv_utf32be_no_bom_to_utf8_no_bom(const char *input_path, const char *output_path);


// 
//  UTF PRINT HELPERS  (convert to UTF-8 then log via SDL_Log)
// 

// Print a UTF-16 slice by converting to UTF-8 first.
void b_str_print_utf16(b_u16slice_t slice);

// Print a UTF-32 slice by converting to UTF-8 first.
void b_str_print_utf32(b_u32slice_t slice);


// 
//  QUERY MACROS  (encoding / flag tests on a live b_str_t / b_cstr_t)
// 

// True when string is a STATIC (fixed-capacity) string.
#define B_STR_IS_STATIC_S(string)  ((string) && (((string)[-1] & B_STRING_STATIC_MASK) != 0u))

// True when string was built with aligned (padded) headers.
#define B_STR_IS_ALIGNED_S(string) ((string) && (((string)[-1] & B_STRING_PACK_MASK)   != 0u))

// b_str_null_size – byte size of the null terminator for the given encoding tag.
// UTF-32: 4 bytes, UTF-16: 2 bytes, everything else: 1 byte.
static inline size_t b_str_null_size(const uint8_t encoding) {
    if (B_STR_IS_UTF32_ENC(encoding)) {
        return 4u;
    }
    if (B_STR_IS_UTF16_ENC(encoding)) {
        return 2u;
    }
    return 1u;
}
// B_STR_NULL_SIZE – macro alias for b_str_null_size; kept for compatibility.
#define B_STR_NULL_SIZE(encoding) b_str_null_size(encoding)

// Encoding predicate macros (take a live b_str_t / b_cstr_t, not an enc tag).
#define B_STR_ENC_OF(string)      b_str_enc(string)
#define B_STR_IS_ASCII(string)    (b_str_enc(string) == B_STR_ENC_ASCII)
#define B_STR_IS_UTF8(string)     (b_str_enc(string) == B_STR_ENC_UTF8)
#define B_STR_IS_UTF16LE(string)  (b_str_enc(string) == B_STR_ENC_UTF16LE)
#define B_STR_IS_UTF16BE(string)  (b_str_enc(string) == B_STR_ENC_UTF16BE)
#define B_STR_IS_UTF16(string)    (B_STR_IS_UTF16_ENC(b_str_enc(string)))
#define B_STR_IS_UTF32LE(string)  (b_str_enc(string) == B_STR_ENC_UTF32LE)
#define B_STR_IS_UTF32BE(string)  (b_str_enc(string) == B_STR_ENC_UTF32BE)
#define B_STR_IS_UTF32(string)    (B_STR_IS_UTF32_ENC(b_str_enc(string)))
#define B_STR_IS_BYTE_ENC(string) (!B_STR_IS_UTF16(string) && !B_STR_IS_UTF32(string))

// Raw pointer casts for manual iteration of wide strings.
#define B_STR_AS_U16(string) ((const uint16_t *)(string))
#define B_STR_AS_U32(string) ((const uint32_t *)(string))


// 
//  SLICE HELPER MACROS  (build a slice from an existing b_str_t)
// 

#ifdef __cplusplus
// C++ aggregate initialisation
#  define B_SLICE_OF(string)    b_slice_t   {(string), b_str_len(string)}
#  define B_U8SLICE_OF(string)  b_u8slice_t {(string), b_str_len(string)}
#  define B_U16SLICE_OF(string) b_u16slice_t{(string), b_str_len(string)}
#  define B_U32SLICE_OF(string) b_u32slice_t{(string), b_str_len(string)}
#else
// C99 compound literals
#  define B_SLICE_OF(string)    ((b_slice_t)   {(string), b_str_len(string)})
#  define B_U8SLICE_OF(string)  ((b_u8slice_t) {(string), b_str_len(string)})
#  define B_U16SLICE_OF(string) ((b_u16slice_t){(string), b_str_len(string)})
#  define B_U32SLICE_OF(string) ((b_u32slice_t){(string), b_str_len(string)})
#endif

#define B_SUBSLICE(string, byte_offset, byte_length) \
    b_subslice((string), (byte_offset), (byte_length))
#define B_SUBSLICE_CP(string, codepoint_offset, codepoint_count) \
    b_subslice_cp((string), (codepoint_offset), (codepoint_count))


// 
//  PRINTF FORMAT HELPERS
// 
//
// Use B_STR_FMT as the format specifier, then B_str_arg(x) as the argument pair:
//
//   printf(B_STR_FMT "\n", B_str_arg(my_b_str));
//   printf(B_STR_FMT "\n", B_str_arg(my_c_string));
//   printf(B_STR_FMT "\n", B_str_arg(my_slice));
//
// WARNING: B_str_arg(X) evaluates X twice.
// Use B_PRINTF_SAFE(fmt, expr) when expr has side effects.

#define B_STR_FMT "%.*s"

// Internal helper struct: (print_length, print_pointer) pair for printf.
typedef struct b_printf_format_argument { int print_length; const char *print_pointer; } b_fmt_arg_t;

// The four typed helper functions (defined in the header so they inline).
// They are static inline to avoid multiple-definition errors across TUs.
static inline b_fmt_arg_t b_internal_fmt_cstr(const char * const c_string) {
    size_t string_length;
    if (c_string) {
        string_length = SDL_strlen(c_string);
    }
    else {
        string_length = 0u;
    }
    b_fmt_arg_t result;
    // clamp to INT_MAX so the %.*s width doesn't overflow
    if (string_length > (size_t)INT_MAX) {
        result.print_length = INT_MAX;
    }
    else {
        result.print_length = (int)string_length;
    }
    if (c_string) {
        result.print_pointer = c_string;
    }
    else {
        result.print_pointer = "";
    }
    return result;
}

static inline b_fmt_arg_t b_internal_fmt_string(const uint8_t * const string) {
    const size_t string_length = b_str_len(string);
    b_fmt_arg_t result;
    if (string_length > (size_t)INT_MAX) {
        result.print_length = INT_MAX;
    }
    else {
        result.print_length = (int)string_length;
    }
    if (string) {
        result.print_pointer = (const char *)string;
    }
    else {
        result.print_pointer = "";
    }
    return result;
}

static inline b_fmt_arg_t b_internal_fmt_slice(const b_slice_t slice) {
    b_fmt_arg_t result;
    if (slice.byte_length > (size_t)INT_MAX) {
        result.print_length = INT_MAX;
    }
    else {
        result.print_length = (int)slice.byte_length;
    }
    if (slice.data) {
        result.print_pointer = (const char *)slice.data;
    }
    else {
        result.print_pointer = "";
    }
    return result;
}

static inline b_fmt_arg_t b_internal_fmt_utf8_slice(const b_u8slice_t slice) {
    b_fmt_arg_t result;
    if (slice.byte_length > (size_t)INT_MAX) {
        result.print_length = INT_MAX;
    }
    else {
        result.print_length = (int)slice.byte_length;
    }
    if (slice.data) {
        result.print_pointer = (const char *)slice.data;
    }
    else {
        result.print_pointer = "";
    }
    return result;
}

// B_FMT_ARG(X) – dispatch to the right helper based on the type of X.
#ifdef __cplusplus
// C++ overload set
inline b_fmt_arg_t b_internal_fmt_arg_dispatch(const char    *string) { return b_internal_fmt_cstr(string);      }
inline b_fmt_arg_t b_internal_fmt_arg_dispatch(char           *string) { return b_internal_fmt_cstr(string);      }
inline b_fmt_arg_t b_internal_fmt_arg_dispatch(const uint8_t  *string) { return b_internal_fmt_string(string);    }
inline b_fmt_arg_t b_internal_fmt_arg_dispatch(uint8_t        *string) { return b_internal_fmt_string(string);    }
inline b_fmt_arg_t b_internal_fmt_arg_dispatch(b_slice_t       slice)  { return b_internal_fmt_slice(slice);      }
inline b_fmt_arg_t b_internal_fmt_arg_dispatch(b_u8slice_t     slice)  { return b_internal_fmt_utf8_slice(slice); }
#  define B_FMT_ARG(X) b_internal_fmt_arg_dispatch(X)
#else
// C11 _Generic
#  define B_FMT_ARG(X) _Generic((X),                         \
    char         *: b_internal_fmt_cstr,                     \
    const char   *: b_internal_fmt_cstr,                     \
    uint8_t      *: b_internal_fmt_string,                   \
    const uint8_t*: b_internal_fmt_string,                   \
    b_slice_t:      b_internal_fmt_slice,                    \
    b_u8slice_t:    b_internal_fmt_utf8_slice)(X)
#endif

// B_str_len / B_str_ptr – extract print_length and print_pointer separately.
#define B_str_len(X) (B_FMT_ARG(X).print_length)
#define B_str_ptr(X) (B_FMT_ARG(X).print_pointer)

// B_str_arg – expand to the two printf arguments (print_length, print_pointer).
// WARNING: evaluates X twice – do not pass expressions with side effects.
#define B_str_arg(X) B_str_len(X), B_str_ptr(X)

// B_PRINTF_SAFE – safe version that evaluates expr exactly once.
#define B_PRINTF_SAFE(fmt, expr)                                         \
    do {                                                                 \
        const b_fmt_arg_t _b_printf_safe_argument_ = B_FMT_ARG(expr);  \
        SDL_Log((fmt), _b_printf_safe_argument_.print_length,           \
                       _b_printf_safe_argument_.print_pointer);         \
    } while (0)


// ╔╗
// ║  INTERNALS                                                               ║
// ║  You do not need to read past this line for normal library use.          ║
// ║  These are the packed-struct definitions and bit-twiddling macros that   ║
// ║  back the public accessors above.                                        ║
// ╚╝

// ─── string[-1] flag byte layout ─────────────────────────────────────────────
//
//   bit 0     : pack/align flag
//   bit 1     : static allocation flag  (B_STR_STATIC)
//   bits 2–3  : header size class       (B_STR_TYPE_*)
//   bits 4–6  : encoding tag            (B_STR_ENC_*)
//   bit 7     : reserved
//
// "Static" means: fixed-capacity heap buffer – b_str_ensure never reallocates it.

#define B_STRING_PACKED      0x00u
#define B_STRING_ALIGNED     0x01u
#define B_STRING_PACK_MASK   0x01u

#define B_STRING_DYNAMIC     0x00u
#define B_STRING_STATIC      0x02u
#define B_STRING_STATIC_MASK 0x02u
#define B_STR_STATIC         B_STRING_STATIC

#define B_STR_TYPE_8    0x00u   // capacity/length fit in uint8
#define B_STR_TYPE_16   0x04u   // capacity/length fit in uint16
#define B_STR_TYPE_32   0x08u   // capacity/length fit in uint32
#define B_STR_TYPE_64   0x0Cu   // capacity/length fit in uint64
#define B_STR_TYPE_MASK 0x0Cu

#define B_STR_ENC_MASK  0x70u

#define B_STR_ONE_MEG   1048576u  // growth strategy threshold

// ─── Header structs ───────────────────────────────────────────────────────────
//
// Memory layout (packed, default):  [capacity | length | flags | data…]
// string points at data[0]; string[-1] is flags; string[-sizeof(header)] is the struct base.
//
// "Aligned" mode adds padding so that capacity/length fields are naturally
// aligned – useful on platforms that penalise unaligned reads.

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
    uint8_t  _padding;
    uint8_t  flags;
    uint8_t  data[];
} b_hdr16_t;

typedef struct b_string_header_32 {
    uint32_t capacity;
    uint32_t length;
    uint8_t  _padding[3];
    uint8_t  flags;
    uint8_t  data[];
} b_hdr32_t;

typedef struct b_string_header_64 {
    uint64_t capacity;
    uint64_t length;
    uint8_t  _padding[7];
    uint8_t  flags;
    uint8_t  data[];
} b_hdr64_t;

#else // packed (default)

#if defined(_MSC_VER)
#  pragma pack(push, 1)
#  define B_STR_PACKED_ATTR
#elif defined(__GNUC__) || defined(__clang__)
#  define B_STR_PACKED_ATTR __attribute__((__packed__))
#else
#  define B_STR_PACKED_ATTR
#endif

typedef struct B_STR_PACKED_ATTR b_string_header_8 {
    uint8_t  capacity;
    uint8_t  length;
    uint8_t  flags;
    uint8_t  data[];
} b_hdr8_t;

typedef struct B_STR_PACKED_ATTR b_string_header_16 {
    uint16_t capacity;
    uint16_t length;
    uint8_t  flags;
    uint8_t  data[];
} b_hdr16_t;

typedef struct B_STR_PACKED_ATTR b_string_header_32 {
    uint32_t capacity;
    uint32_t length;
    uint8_t  flags;
    uint8_t  data[];
} b_hdr32_t;

typedef struct B_STR_PACKED_ATTR b_string_header_64 {
    uint64_t capacity;
    uint64_t length;
    uint8_t  flags;
    uint8_t  data[];
} b_hdr64_t;

#if defined(_MSC_VER)
#  pragma pack(pop)
#endif

#endif // b_ALIGNED

// Backward-compatible bare-name aliases (no _t suffix).
typedef b_hdr8_t  b_hdr8;
typedef b_hdr16_t b_hdr16;
typedef b_hdr32_t b_hdr32;
typedef b_hdr64_t b_hdr64;

// ─── Header pointer macros ────────────────────────────────────────────────────
// Given a data pointer data_pointer (= the b_str_t value), recover the header struct.

// Mutable access:
#define B_HDR8(data_pointer)  ((b_hdr8_t  *)((data_pointer) - sizeof(b_hdr8_t)))
#define B_HDR16(data_pointer) ((b_hdr16_t *)((data_pointer) - sizeof(b_hdr16_t)))
#define B_HDR32(data_pointer) ((b_hdr32_t *)((data_pointer) - sizeof(b_hdr32_t)))
#define B_HDR64(data_pointer) ((b_hdr64_t *)((data_pointer) - sizeof(b_hdr64_t)))

// Const access:
#define B_CHDR8(data_pointer)  ((const b_hdr8_t  *)((data_pointer) - sizeof(b_hdr8_t)))
#define B_CHDR16(data_pointer) ((const b_hdr16_t *)((data_pointer) - sizeof(b_hdr16_t)))
#define B_CHDR32(data_pointer) ((const b_hdr32_t *)((data_pointer) - sizeof(b_hdr32_t)))
#define B_CHDR64(data_pointer) ((const b_hdr64_t *)((data_pointer) - sizeof(b_hdr64_t)))

// ─── Internal alignment flag ──────────────────────────────────────────────────
// Selected at compile time based on whether b_ALIGNED is defined.
#ifdef b_ALIGNED
#  define B_STR_ALIGN_FLAG B_STRING_ALIGNED
#else
#  define B_STR_ALIGN_FLAG B_STRING_PACKED
#endif

#ifdef __cplusplus
}
#endif
