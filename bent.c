#include "bent.h"

// SSIZE_MAX is POSIX; define a fallback for MSVC / freestanding targets.
#ifndef SSIZE_MAX
#  define SSIZE_MAX ((size_t)(~(size_t)0) >> 1)
#endif


//  BOM byte sequences  (file-scope constants; used in BOM and file I/O sections)

static const uint8_t BOM_UTF8[3]    = { 0xEFu, 0xBBu, 0xBFu };
static const uint8_t BOM_UTF16LE[2] = { 0xFFu, 0xFEu };
static const uint8_t BOM_UTF16BE[2] = { 0xFEu, 0xFFu };
static const uint8_t BOM_UTF32LE[4] = { 0xFFu, 0xFEu, 0x00u, 0x00u };
static const uint8_t BOM_UTF32BE[4] = { 0x00u, 0x00u, 0xFEu, 0xFFu };


// 
//  INTERNAL HELPERS
//  All helpers are file-private (static).
//  No static inline here – the compiler decides whether to inline.
// 

// Null terminator width: 4 bytes for UTF-32, 2 for UTF-16, 1 for everything else.
static size_t b_internal_null_terminator_size(const uint8_t encoding) {
    if (B_STR_IS_UTF32_ENC(encoding)) {
        return 4u;
    }
    if (B_STR_IS_UTF16_ENC(encoding)) {
        return 2u;
    }
    return 1u;
}

// Round byte_length DOWN to a valid code-unit boundary for the given encoding.
static size_t b_internal_align_length_down(const size_t byte_length, const uint8_t encoding) {
    if (B_STR_IS_UTF32_ENC(encoding)) {
        return byte_length & ~(size_t)3u;
    }
    if (B_STR_IS_UTF16_ENC(encoding)) {
        return byte_length & ~(size_t)1u;
    }
    return byte_length;
}

// Round capacity UP to the next code-unit boundary.
// Caller is responsible for checking that overflow cannot occur before calling.
static size_t b_internal_align_capacity_up(const size_t capacity, const uint8_t encoding) {
    if (B_STR_IS_UTF32_ENC(encoding)) {
        return (capacity + 3u) & ~(size_t)3u;
    }
    if (B_STR_IS_UTF16_ENC(encoding)) {
        return (capacity + 1u) & ~(size_t)1u;
    }
    return capacity;
}

// Write null_terminator_size zero bytes starting at buffer[position].
static void b_internal_write_null_terminator(uint8_t * const buffer,
                                              const size_t position,
                                              const size_t null_terminator_size) {
    for (size_t i = 0u; i < null_terminator_size; i++) {
        buffer[position + i] = '\0';
    }
}

// Unaligned 16-bit read via SDL_memcpy – avoids strict-aliasing undefined behaviour.
static uint16_t b_internal_read_uint16(const void * const raw) {
    uint16_t value;
    SDL_memcpy(&value, raw, 2u);
    return value;
}

// Unaligned 32-bit read via SDL_memcpy – same rationale as b_internal_read_uint16.
static uint32_t b_internal_read_uint32(const void * const raw) {
    uint32_t value;
    SDL_memcpy(&value, raw, 4u);
    return value;
}

// Byte-swap a 16-bit value.
static uint16_t b_internal_byte_swap_16(const uint16_t value) {
    return (uint16_t)(((value & 0x00FFu) << 8) | ((value & 0xFF00u) >> 8));
}

// Byte-swap a 32-bit value.
static uint32_t b_internal_byte_swap_32(const uint32_t value) {
    return ((value & 0x000000FFu) << 24)
         | ((value & 0x0000FF00u) <<  8)
         | ((value & 0x00FF0000u) >>  8)
         | ((value & 0xFF000000u) >> 24);
}

// Return the UTF-16 encoding tag that matches the host's native byte order.
static uint8_t b_internal_system_utf16_encoding(void) {
#if defined(SDL_BYTEORDER) && SDL_BYTEORDER == SDL_BIG_ENDIAN
    return B_STR_ENC_UTF16BE;
#else
    return B_STR_ENC_UTF16LE;
#endif
}

// Return the UTF-32 encoding tag that matches the host's native byte order.
static uint8_t b_internal_system_utf32_encoding(void) {
#if defined(SDL_BYTEORDER) && SDL_BYTEORDER == SDL_BIG_ENDIAN
    return B_STR_ENC_UTF32BE;
#else
    return B_STR_ENC_UTF32LE;
#endif
}


// 
//  STATIC WRITE-BACK HELPER
//
//  b_internal_apply_or_keep(original, converted, new_encoding):
//    – converted == NULL  →  conversion failed; return original unchanged.
//    – converted == original  →  same-encoding no-op; return original.
//    – original is DYNAMIC  →  return converted (caller owns both, must free old).
//    – original is STATIC  →  try to copy converted data back into original's
//      buffer. If it fits, the pointer stays stable. If it doesn't fit, fail
//      silently (original is unchanged). Either way, free(converted) and return
//      original.
// 

static b_str_t b_internal_apply_or_keep(b_str_t original, b_str_t converted,
                                         const uint8_t new_encoding) {
    // Conversion failed or was a no-op: nothing to do.
    if (!converted || converted == original) {
        return original;
    }

    // Dynamic string: hand the new allocation back to the caller.
    if (!(original[-1] & B_STR_STATIC)) {
        return converted;
    }

    // Static string: try to write the result back so the pointer stays stable.
    const size_t result_byte_length     = b_str_len(converted);
    const size_t new_null_terminator_size = b_internal_null_terminator_size(new_encoding);
    const size_t old_null_terminator_size = b_internal_null_terminator_size(
        (uint8_t)(original[-1] & B_STR_ENC_MASK));
    const size_t total_buffer_size = b_str_cap(original) + old_null_terminator_size;

    if (result_byte_length + new_null_terminator_size <= total_buffer_size) {
        // Result fits: copy data + null terminator, then update header fields.
        SDL_memcpy(original, converted, result_byte_length + new_null_terminator_size);
        b_str_set_enc(original, new_encoding);
        // Capacity: align downward to keep the header consistent.
        const size_t new_capacity = b_internal_align_length_down(
            total_buffer_size - new_null_terminator_size, new_encoding);
        b_str_set_lens(original, result_byte_length, new_capacity);
    }
    // If the result doesn't fit, fail silently – original is unchanged.

    b_str_free(converted);
    return original;
}


// 
//  CORE ACCESSORS
// 

size_t b_str_hdr_size(const uint8_t flags) {
    switch (flags & B_STR_TYPE_MASK) {
    case B_STR_TYPE_8:  return sizeof(b_hdr8_t);
    case B_STR_TYPE_16: return sizeof(b_hdr16_t);
    case B_STR_TYPE_32: return sizeof(b_hdr32_t);
    case B_STR_TYPE_64: return sizeof(b_hdr64_t);
    default:            return 0u;
    }
}

uint8_t b_str_pick_type(const size_t byte_size) {
    if (byte_size < 256u) {
        return B_STR_TYPE_8;
    }
    if (byte_size < 65536u) {
        return B_STR_TYPE_16;
    }
    if (byte_size <= (size_t)UINT32_MAX) {
        return B_STR_TYPE_32;
    }
    return B_STR_TYPE_64;
}

uint8_t b_str_enc(const b_cstr_t string) {
    if (!string) {
        return B_STR_ENC_ASCII;
    }
    return (uint8_t)(string[-1] & B_STR_ENC_MASK);
}

void b_str_set_enc(b_str_t string, const uint8_t encoding) {
    if (string) {
        string[-1] = (uint8_t)((string[-1] & ~B_STR_ENC_MASK) | (encoding & B_STR_ENC_MASK));
    }
}

size_t b_str_len(const b_cstr_t string) {
    if (!string) {
        return 0u;
    }
    switch (string[-1] & B_STR_TYPE_MASK) {
    case B_STR_TYPE_8:  return B_CHDR8(string)->length;
    case B_STR_TYPE_16: return B_CHDR16(string)->length;
    case B_STR_TYPE_32: return B_CHDR32(string)->length;
    case B_STR_TYPE_64: return B_CHDR64(string)->length;
    default:            return 0u;
    }
}

size_t b_str_avail(const b_cstr_t string) {
    if (!string) {
        return 0u;
    }
    switch (string[-1] & B_STR_TYPE_MASK) {
    case B_STR_TYPE_8: {
        const b_hdr8_t * const header = B_CHDR8(string);
        if (header->capacity > header->length) {
            return (size_t)(header->capacity - header->length);
        }
        return 0u;
    }
    case B_STR_TYPE_16: {
        const b_hdr16_t * const header = B_CHDR16(string);
        if (header->capacity > header->length) {
            return (size_t)(header->capacity - header->length);
        }
        return 0u;
    }
    case B_STR_TYPE_32: {
        const b_hdr32_t * const header = B_CHDR32(string);
        if (header->capacity > header->length) {
            return (size_t)(header->capacity - header->length);
        }
        return 0u;
    }
    case B_STR_TYPE_64: {
        const b_hdr64_t * const header = B_CHDR64(string);
        if (header->capacity > header->length) {
            return (size_t)(header->capacity - header->length);
        }
        return 0u;
    }
    default:
        return 0u;
    }
}

size_t b_str_cap(const b_cstr_t string) {
    if (!string) {
        return 0u;
    }
    return b_str_len(string) + b_str_avail(string);
}

void b_str_set_lens(b_str_t string, size_t used_bytes, size_t capacity_bytes) {
    if (!string) {
        return;
    }
    const uint8_t encoding = (uint8_t)(string[-1] & B_STR_ENC_MASK);
    // Align both values down to a valid code-unit boundary.
    used_bytes     = b_internal_align_length_down(used_bytes,     encoding);
    capacity_bytes = b_internal_align_length_down(capacity_bytes, encoding);
    switch (string[-1] & B_STR_TYPE_MASK) {
    case B_STR_TYPE_8:
        B_HDR8(string)->capacity = (uint8_t)capacity_bytes;
        B_HDR8(string)->length   = (uint8_t)used_bytes;
        break;
    case B_STR_TYPE_16:
        B_HDR16(string)->capacity = (uint16_t)capacity_bytes;
        B_HDR16(string)->length   = (uint16_t)used_bytes;
        break;
    case B_STR_TYPE_32:
        B_HDR32(string)->capacity = (uint32_t)capacity_bytes;
        B_HDR32(string)->length   = (uint32_t)used_bytes;
        break;
    case B_STR_TYPE_64:
        B_HDR64(string)->capacity = (uint64_t)capacity_bytes;
        B_HDR64(string)->length   = (uint64_t)used_bytes;
        break;
    default:
        break;
    }
}

void b_str_set_len(b_str_t string, size_t used_bytes) {
    if (!string) {
        return;
    }
    // Align down so the stored length is always on a code-unit boundary.
    used_bytes = b_internal_align_length_down(used_bytes, (uint8_t)(string[-1] & B_STR_ENC_MASK));
    switch (string[-1] & B_STR_TYPE_MASK) {
    case B_STR_TYPE_8:  B_HDR8(string)->length  = (uint8_t) used_bytes; break;
    case B_STR_TYPE_16: B_HDR16(string)->length = (uint16_t)used_bytes; break;
    case B_STR_TYPE_32: B_HDR32(string)->length = (uint32_t)used_bytes; break;
    case B_STR_TYPE_64: B_HDR64(string)->length = (uint64_t)used_bytes; break;
    default: break;
    }
}

size_t b_str_cpcount(const b_cstr_t string) {
    if (!string) {
        return 0u;
    }
    const size_t  total_byte_length = b_str_len(string);
    const uint8_t encoding          = b_str_enc(string);
    if (!total_byte_length) {
        return 0u;
    }

    if (encoding == B_STR_ENC_UTF8) {
        // Walk each sequence; utf8proc_iterate returns its byte width.
        size_t codepoint_count = 0u;
        size_t byte_position   = 0u;
        while (byte_position < total_byte_length) {
            utf8proc_int32_t codepoint;
            const size_t remaining_bytes = total_byte_length - byte_position;
            size_t safe_remaining_bytes;
            if (remaining_bytes < (size_t)SSIZE_MAX) {
                safe_remaining_bytes = remaining_bytes;
            }
            else {
                safe_remaining_bytes = (size_t)SSIZE_MAX;
            }
            const utf8proc_ssize_t advance_bytes = utf8proc_iterate(
                string + byte_position, (utf8proc_ssize_t)safe_remaining_bytes, &codepoint);
            if (advance_bytes > 0) {
                byte_position += (size_t)advance_bytes;
            }
            else {
                byte_position += 1u; // invalid byte: skip it but still count one "character"
            }
            codepoint_count++;
        }
        return codepoint_count;
    }

    if (B_STR_IS_UTF16_ENC(encoding)) {
        // Surrogate pairs count as one code-point.
        const bool   is_big_endian = (encoding == B_STR_ENC_UTF16BE);
        const size_t total_units   = total_byte_length / 2u;
        size_t codepoint_count = 0u;
        size_t unit_index      = 0u;
        while (unit_index < total_units) {
            uint16_t first_code_unit = b_internal_read_uint16(string + unit_index * 2u);
            if (is_big_endian) {
                first_code_unit = b_internal_byte_swap_16(first_code_unit);
            }
            unit_index++;
            // High surrogate: consume the matching low surrogate.
            if (first_code_unit >= 0xD800u && first_code_unit <= 0xDBFFu && unit_index < total_units) {
                uint16_t second_code_unit = b_internal_read_uint16(string + unit_index * 2u);
                if (is_big_endian) {
                    second_code_unit = b_internal_byte_swap_16(second_code_unit);
                }
                if (second_code_unit >= 0xDC00u && second_code_unit <= 0xDFFFu) {
                    unit_index++;
                }
            }
            codepoint_count++;
        }
        return codepoint_count;
    }

    if (B_STR_IS_UTF32_ENC(encoding)) {
        // UTF-32: one 4-byte unit equals exactly one code-point.
        return total_byte_length / 4u;
    }

    // ASCII: one byte equals one code-point.
    return total_byte_length;
}


// 
//  LIFECYCLE
// 

b_str_t b_str_new_pro(const void * const data, size_t byte_length,
                       const uint8_t encoding) {
    byte_length = b_internal_align_length_down(byte_length, encoding);

    const uint8_t header_type         = b_str_pick_type(byte_length);
    const size_t  header_size         = b_str_hdr_size(header_type);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);

    // Guard against size_t overflow before calling SDL_malloc.
    if (byte_length > SIZE_MAX - header_size - null_terminator_size) {
        return NULL;
    }

    uint8_t * const memory = (uint8_t *)SDL_malloc(header_size + byte_length + null_terminator_size);
    if (!memory) {
        return NULL;
    }

    b_str_t string = memory + header_size;
    string[-1] = (uint8_t)(header_type | B_STR_ALIGN_FLAG | (encoding & B_STR_ENC_MASK));

    if (data) {
        SDL_memcpy(string, data, byte_length);
    }
    else if (byte_length > 0u) {
        SDL_memset(string, 0, byte_length);
    }

    b_str_set_lens(string, byte_length, byte_length);
    b_internal_write_null_terminator(string, byte_length, null_terminator_size);
    return string;
}

b_str_t b_str_new(const char * const c_string) {
    size_t c_string_length;
    if (c_string) {
        c_string_length = SDL_strlen(c_string);
    }
    else {
        c_string_length = 0u;
    }
    return b_str_new_pro(c_string, c_string_length, B_STR_ENC_ASCII);
}

b_str_t b_str_new_static_pro(const void * const data, size_t byte_length,
                               size_t total_capacity, const uint8_t encoding) {
    byte_length    = b_internal_align_length_down(byte_length,    encoding);
    total_capacity = b_internal_align_length_down(total_capacity, encoding);
    if (byte_length > total_capacity) {
        return NULL;
    }

    const uint8_t header_type         = b_str_pick_type(total_capacity);
    const size_t  header_size         = b_str_hdr_size(header_type);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);

    if (total_capacity > SIZE_MAX - header_size - null_terminator_size) {
        return NULL;
    }

    uint8_t * const memory = (uint8_t *)SDL_malloc(
        header_size + total_capacity + null_terminator_size);
    if (!memory) {
        return NULL;
    }

    b_str_t string = memory + header_size;
    string[-1] = (uint8_t)(header_type | B_STR_ALIGN_FLAG | B_STR_STATIC
                            | (encoding & B_STR_ENC_MASK));

    if (data) {
        SDL_memcpy(string, data, byte_length);
    }
    else if (byte_length > 0u) {
        SDL_memset(string, 0, byte_length);
    }

    b_str_set_lens(string, byte_length, total_capacity);
    b_internal_write_null_terminator(string, byte_length, null_terminator_size);
    return string;
}

b_str_t b_str_new_static(const char * const c_string, const size_t extra_bytes) {
    size_t byte_length;
    if (c_string) {
        byte_length = SDL_strlen(c_string);
    }
    else {
        byte_length = 0u;
    }
    if (byte_length > SIZE_MAX - extra_bytes) {
        return NULL;
    }
    return b_str_new_static_pro(c_string, byte_length, byte_length + extra_bytes, B_STR_ENC_ASCII);
}

void b_str_free(b_str_t string) {
    if (string) {
        SDL_free((uint8_t *)string - b_str_hdr_size(string[-1]));
    }
}

b_str_t b_str_dup(const b_cstr_t string) {
    if (!string) {
        return NULL;
    }
    return b_str_new_pro(string, b_str_len(string), b_str_enc(string));
}

void b_str_clear(b_str_t string) {
    if (!string) {
        return;
    }
    const size_t null_terminator_size = b_internal_null_terminator_size(b_str_enc(string));
    b_str_set_len(string, 0u);
    b_internal_write_null_terminator(string, 0u, null_terminator_size);
}

bool b_str_empty(const b_cstr_t string) {
    return !string || b_str_len(string) == 0u;
}

b_str_t b_str_to_dyn(b_str_t string) {
    // Not a valid string: nothing to do.
    if (!string) {
        return string;
    }
    // Already dynamic: nothing to do.
    if (!(string[-1] & B_STR_STATIC)) {
        return string;
    }
    // STATIC strings: this function does nothing and returns NULL.
    // Static strings guarantee pointer stability; freeing the buffer here would
    // invalidate every pointer the caller holds. Use b_str_dup() instead to get
    // an independent dynamic copy that you can mutate and reallocate freely.
    return NULL;
}


// 
//  SLICE CONSTRUCTORS
// 

b_str_t b_str_from_slice(const b_slice_t slice) {
    return b_str_new_pro(slice.data, slice.byte_length, B_STR_ENC_ASCII);
}

b_str_t b_str_from_u8slice(const b_u8slice_t slice) {
    return b_str_new_pro(slice.data, slice.byte_length, B_STR_ENC_UTF8);
}

b_str_t b_str_from_u16slice(const b_u16slice_t slice) {
    // Mask off the last byte if the length isn't code-unit-aligned.
    return b_str_new_pro(slice.data, slice.byte_length & ~(size_t)1u, B_STR_ENC_UTF16LE);
}

b_str_t b_str_from_u32slice(const b_u32slice_t slice) {
    return b_str_new_pro(slice.data, slice.byte_length & ~(size_t)3u, B_STR_ENC_UTF32LE);
}

b_str_t b_str_from_u16(const uint16_t * const units, const size_t unit_count) {
    if (!units && unit_count > 0u) {
        return NULL;
    }
    if (unit_count > SIZE_MAX / 2u) {
        return NULL;
    }
    return b_str_new_pro(units, unit_count * 2u, B_STR_ENC_UTF16LE);
}

b_str_t b_str_from_u32(const uint32_t * const units, const size_t unit_count) {
    if (!units && unit_count > 0u) {
        return NULL;
    }
    if (unit_count > SIZE_MAX / 4u) {
        return NULL;
    }
    return b_str_new_pro(units, unit_count * 4u, B_STR_ENC_UTF32LE);
}


// 
//  CAPACITY MANAGEMENT
// 

b_str_t b_str_ensure(b_str_t string, const size_t extra_bytes) {
    if (!string) {
        return NULL;
    }
    if (b_str_avail(string) >= extra_bytes) {
        return string;
    }
    // Static strings have a fixed capacity; never reallocate them.
    if (string[-1] & B_STR_STATIC) {
        return string;
    }

    const uint8_t encoding            = b_str_enc(string);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);
    const size_t  current_length      = b_str_len(string);
    const size_t  current_capacity    = b_str_cap(string);

    if (extra_bytes > SIZE_MAX - current_length) {
        return string; // requested size would overflow size_t
    }
    size_t needed_capacity = b_internal_align_capacity_up(current_length + extra_bytes, encoding);

    // Growth strategy: double below 1 MB, add 1 MB above that.
    size_t new_capacity = needed_capacity;
    if (new_capacity < B_STR_ONE_MEG) {
        size_t doubled_capacity;
        if (current_capacity > SIZE_MAX / 2u) {
            doubled_capacity = SIZE_MAX;
        }
        else {
            doubled_capacity = current_capacity * 2u;
        }
        if (doubled_capacity > new_capacity) {
            new_capacity = doubled_capacity;
        }
    }
    else {
        if (SIZE_MAX - B_STR_ONE_MEG >= new_capacity) {
            new_capacity = new_capacity + B_STR_ONE_MEG;
        }
        else {
            new_capacity = SIZE_MAX;
        }
    }
    new_capacity = b_internal_align_capacity_up(new_capacity, encoding);

    uint8_t new_header_type   = b_str_pick_type(new_capacity);
    size_t  new_header_size   = b_str_hdr_size(new_header_type);

    // Clamp to avoid size_t overflow in the allocation.
    if (new_capacity > SIZE_MAX - new_header_size - null_terminator_size) {
        new_capacity    = b_internal_align_length_down(
            SIZE_MAX - new_header_size - null_terminator_size, encoding);
        new_header_type = b_str_pick_type(new_capacity);
        new_header_size = b_str_hdr_size(new_header_type);
        if (new_capacity < needed_capacity) {
            return string; // cannot satisfy the request even at the ceiling
        }
    }

    const uint8_t current_header_type   = (uint8_t)(string[-1] & B_STR_TYPE_MASK);
    const size_t  current_header_size   = b_str_hdr_size(current_header_type);
    uint8_t *memory;

    if (current_header_type == new_header_type) {
        // Header size is unchanged: realloc in place.
        memory = (uint8_t *)SDL_realloc(string - current_header_size,
                                         new_header_size + new_capacity + null_terminator_size);
        if (!memory) {
            return string; // OOM: original is still valid
        }
        string = memory + new_header_size;
    }
    else {
        // Header size is changing: alloc a fresh block, copy, free the old one.
        memory = (uint8_t *)SDL_malloc(new_header_size + new_capacity + null_terminator_size);
        if (!memory) {
            return string;
        }
        SDL_memcpy(memory + new_header_size, string, current_length + null_terminator_size);
        SDL_free(string - current_header_size);
        string = memory + new_header_size;
        string[-1] = (uint8_t)(new_header_type | B_STR_ALIGN_FLAG | encoding);
    }

    b_str_set_lens(string, current_length, new_capacity);
    return string;
}

b_str_t b_str_reserve(b_str_t string, const size_t extra_bytes) {
    return b_str_ensure(string, extra_bytes);
}

b_str_t b_str_fit(b_str_t string) {
    if (!string || (string[-1] & B_STR_STATIC)) {
        return string;
    }

    const uint8_t encoding            = b_str_enc(string);
    const size_t  used_bytes          = b_str_len(string);
    const uint8_t current_header_type = (uint8_t)(string[-1] & B_STR_TYPE_MASK);
    const size_t  current_header_size = b_str_hdr_size(current_header_type);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);
    const uint8_t tight_header_type   = b_str_pick_type(used_bytes);
    const size_t  tight_header_size   = b_str_hdr_size(tight_header_type);

    if (current_header_type == tight_header_type) {
        // Same header class: just shrink the allocation in place.
        uint8_t * const memory = (uint8_t *)SDL_realloc(
            string - current_header_size,
            current_header_size + used_bytes + null_terminator_size);
        if (!memory) {
            return string;
        }
        string = memory + current_header_size;
    }
    else {
        // Header class is shrinking: alloc, copy, free.
        uint8_t * const memory = (uint8_t *)SDL_malloc(
            tight_header_size + used_bytes + null_terminator_size);
        if (!memory) {
            return string;
        }
        SDL_memcpy(memory + tight_header_size, string, used_bytes + null_terminator_size);
        SDL_free(string - current_header_size);
        string = memory + tight_header_size;
        string[-1] = (uint8_t)(tight_header_type | B_STR_ALIGN_FLAG | encoding);
    }

    b_str_set_lens(string, used_bytes, used_bytes);
    b_internal_write_null_terminator(string, used_bytes, null_terminator_size);
    return string;
}

void b_str_arr_fit(b_str_t * const array, const size_t array_count) {
    if (!array) {
        return;
    }
    for (size_t i = 0u; i < array_count; i++) {
        if (array[i]) {
            array[i] = b_str_fit(array[i]);
        }
    }
}


// 
//  APPENDING & CONCATENATION
//  All append variants return string; never NULL when string != NULL.
// 

b_str_t b_str_append_pro(b_str_t string, const void * const data, size_t byte_length) {
    if (!string || !data || !byte_length) {
        return string;
    }

    const uint8_t encoding            = b_str_enc(string);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);
    const size_t  current_length      = b_str_len(string);

    byte_length = b_internal_align_length_down(byte_length, encoding);
    if (!byte_length) {
        return string;
    }
    if (byte_length > SIZE_MAX - current_length) {
        return string;
    }

    // Detect self-overlap before the potential realloc moves the buffer.
    const uintptr_t source_address = (uintptr_t)data;
    const uintptr_t buffer_address = (uintptr_t)string;
    const bool is_overlap = (source_address >= buffer_address
                              && source_address < (uintptr_t)(string + current_length
                                                               + null_terminator_size));
    ptrdiff_t overlap_byte_offset;
    if (is_overlap) {
        overlap_byte_offset = (ptrdiff_t)((const uint8_t *)data - string);
    }
    else {
        overlap_byte_offset = 0;
    }

    string = b_str_ensure(string, byte_length);
    if (b_str_avail(string) < byte_length) {
        return string; // OOM or static string that is already full
    }

    // If the source was inside our buffer it may have moved; recalculate.
    const void *source_pointer;
    if (is_overlap) {
        source_pointer = (const void *)(string + overlap_byte_offset);
    }
    else {
        source_pointer = data;
    }
    SDL_memmove(string + current_length, source_pointer, byte_length);

    const size_t new_length = current_length + byte_length;
    b_str_set_len(string, new_length);
    b_internal_write_null_terminator(string, new_length, null_terminator_size);
    return string;
}

b_str_t b_str_append(b_str_t string, const char * const c_string) {
    size_t c_string_length;
    if (c_string) {
        c_string_length = SDL_strlen(c_string);
    }
    else {
        c_string_length = 0u;
    }
    return b_str_append_pro(string, c_string, c_string_length);
}

b_str_t b_str_append_sl(b_str_t string, const b_slice_t slice) {
    if (!slice.data) {
        return string;
    }
    return b_str_append_pro(string, slice.data, slice.byte_length);
}

b_str_t b_str_append_u8(b_str_t string, const b_u8slice_t slice) {
    if (!slice.data || !slice.byte_length) {
        return string;
    }
    // Promote encoding from ASCII to UTF-8 when appending UTF-8 bytes.
    if (b_str_enc(string) == B_STR_ENC_ASCII) {
        b_str_set_enc(string, B_STR_ENC_UTF8);
    }
    return b_str_append_pro(string, slice.data, slice.byte_length);
}

b_str_t b_str_append_u16(b_str_t string, const b_u16slice_t slice) {
    const size_t aligned_byte_length = slice.byte_length & ~(size_t)1u;
    if (!slice.data || !aligned_byte_length) {
        return string;
    }
    return b_str_append_pro(string, slice.data, aligned_byte_length);
}

b_str_t b_str_append_u32(b_str_t string, const b_u32slice_t slice) {
    const size_t aligned_byte_length = slice.byte_length & ~(size_t)3u;
    if (!slice.data || !aligned_byte_length) {
        return string;
    }
    return b_str_append_pro(string, slice.data, aligned_byte_length);
}

b_str_t b_str_concat(const b_cstr_t left_string, const b_cstr_t right_string) {
    uint8_t encoding;
    if (left_string) {
        encoding = b_str_enc(left_string);
    }
    else {
        encoding = B_STR_ENC_ASCII;
    }
    size_t left_byte_length;
    if (left_string) {
        left_byte_length = b_str_len(left_string);
    }
    else {
        left_byte_length = 0u;
    }
    size_t right_byte_length;
    if (right_string) {
        right_byte_length = b_str_len(right_string);
    }
    else {
        right_byte_length = 0u;
    }
    if (left_byte_length > SIZE_MAX - right_byte_length) {
        return NULL;
    }

    b_str_t result = b_str_new_pro(left_string, left_byte_length, encoding);
    if (!result) {
        return NULL;
    }
    if (right_byte_length) {
        const size_t previous_length = b_str_len(result);
        result = b_str_append_pro(result, right_string, right_byte_length);
        if (b_str_len(result) == previous_length) {
            // Append failed (OOM): clean up and signal failure to caller.
            b_str_free(result);
            return NULL;
        }
    }
    return result;
}

b_str_t b_str_concat_pro(const b_cstr_t left_string, const b_cstr_t right_string,
                          const uint8_t encoding) {
    size_t left_byte_length;
    if (left_string) {
        left_byte_length = b_str_len(left_string);
    }
    else {
        left_byte_length = 0u;
    }
    size_t right_byte_length;
    if (right_string) {
        right_byte_length = b_str_len(right_string);
    }
    else {
        right_byte_length = 0u;
    }
    if (left_byte_length > SIZE_MAX - right_byte_length) {
        return NULL;
    }

    b_str_t result = b_str_new_pro(left_string, left_byte_length, encoding);
    if (!result) {
        return NULL;
    }
    if (right_byte_length) {
        const size_t previous_length = b_str_len(result);
        result = b_str_append_pro(result, right_string, right_byte_length);
        if (b_str_len(result) == previous_length) {
            b_str_free(result);
            return NULL;
        }
    }
    return result;
}


// 
//  SLICE EXTRACTORS
// 

b_slice_t b_slice_of(const b_cstr_t string) {
    b_slice_t result;
    result.data        = string;
    result.byte_length = b_str_len(string);
    return result;
}

b_slice_t b_subslice(const b_cstr_t string, const size_t byte_offset,
                      const size_t byte_length) {
    b_slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    const size_t total_byte_length = b_str_len(string);
    if (byte_offset >= total_byte_length) {
        return empty_slice;
    }
    const size_t remaining_bytes = total_byte_length - byte_offset;
    b_slice_t result;
    result.data = string + byte_offset;
    if (byte_length > remaining_bytes) {
        result.byte_length = remaining_bytes;
    }
    else {
        result.byte_length = byte_length;
    }
    return result;
}

b_u8slice_t b_u8slice_of(const b_cstr_t string) {
    b_u8slice_t result;
    result.data        = string;
    result.byte_length = b_str_len(string);
    return result;
}

b_u8slice_t b_u8subslice(const b_cstr_t string, const size_t byte_offset,
                           const size_t byte_length) {
    b_u8slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    const size_t total_byte_length = b_str_len(string);
    if (byte_offset >= total_byte_length) {
        return empty_slice;
    }
    const size_t remaining_bytes = total_byte_length - byte_offset;
    b_u8slice_t result;
    result.data = string + byte_offset;
    if (byte_length > remaining_bytes) {
        result.byte_length = remaining_bytes;
    }
    else {
        result.byte_length = byte_length;
    }
    return result;
}

b_u8slice_t b_u8subslice_cp(const b_cstr_t string, const size_t codepoint_offset,
                              const size_t codepoint_count) {
    b_u8slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    const size_t total_byte_length = b_str_len(string);
    size_t byte_position = 0u;

    // Walk forward codepoint_offset code-points to find the start.
    for (size_t i = 0u; i < codepoint_offset && byte_position < total_byte_length; i++) {
        utf8proc_int32_t codepoint;
        const size_t remaining_bytes = total_byte_length - byte_position;
        size_t safe_remaining_bytes;
        if (remaining_bytes < (size_t)SSIZE_MAX) {
            safe_remaining_bytes = remaining_bytes;
        }
        else {
            safe_remaining_bytes = (size_t)SSIZE_MAX;
        }
        const utf8proc_ssize_t advance_bytes = utf8proc_iterate(
            string + byte_position, (utf8proc_ssize_t)safe_remaining_bytes, &codepoint);
        if (advance_bytes > 0) {
            byte_position += (size_t)advance_bytes;
        }
        else {
            byte_position += 1u;
        }
    }
    if (byte_position >= total_byte_length && codepoint_offset > 0u) {
        return empty_slice;
    }
    const size_t start_byte_offset = byte_position;

    // Walk forward codepoint_count more code-points to find the end.
    for (size_t i = 0u; i < codepoint_count && byte_position < total_byte_length; i++) {
        utf8proc_int32_t codepoint;
        const size_t remaining_bytes = total_byte_length - byte_position;
        size_t safe_remaining_bytes;
        if (remaining_bytes < (size_t)SSIZE_MAX) {
            safe_remaining_bytes = remaining_bytes;
        }
        else {
            safe_remaining_bytes = (size_t)SSIZE_MAX;
        }
        const utf8proc_ssize_t advance_bytes = utf8proc_iterate(
            string + byte_position, (utf8proc_ssize_t)safe_remaining_bytes, &codepoint);
        if (advance_bytes > 0) {
            byte_position += (size_t)advance_bytes;
        }
        else {
            byte_position += 1u;
        }
    }
    b_u8slice_t result;
    result.data        = string + start_byte_offset;
    result.byte_length = byte_position - start_byte_offset;
    return result;
}

b_u16slice_t b_u16slice_of(const b_cstr_t string) {
    b_u16slice_t result;
    result.data        = string;
    result.byte_length = b_str_len(string);
    return result;
}

b_u16slice_t b_u16subslice(const b_cstr_t string, size_t byte_offset, size_t byte_length) {
    b_u16slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    // Force alignment to 2-byte code-unit boundaries.
    byte_offset &= ~(size_t)1u;
    byte_length &= ~(size_t)1u;
    const size_t total_byte_length = b_str_len(string) & ~(size_t)1u;
    if (byte_offset >= total_byte_length) {
        return empty_slice;
    }
    const size_t remaining_bytes = total_byte_length - byte_offset;
    b_u16slice_t result;
    result.data = string + byte_offset;
    if (byte_length > remaining_bytes) {
        result.byte_length = remaining_bytes;
    }
    else {
        result.byte_length = byte_length;
    }
    return result;
}

b_u16slice_t b_u16subslice_cp(const b_cstr_t string, const size_t codepoint_offset,
                                const size_t codepoint_count) {
    b_u16slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    const bool   is_big_endian = (b_str_enc(string) == B_STR_ENC_UTF16BE);
    const size_t total_units   = b_str_len(string) / 2u;
    size_t unit_index = 0u;

    // Advance unit_index by codepoint_offset code-points (handling surrogate pairs).
    for (size_t i = 0u; i < codepoint_offset && unit_index < total_units; i++) {
        uint16_t first_code_unit = b_internal_read_uint16(string + unit_index * 2u);
        if (is_big_endian) {
            first_code_unit = b_internal_byte_swap_16(first_code_unit);
        }
        unit_index++;
        if (first_code_unit >= 0xD800u && first_code_unit <= 0xDBFFu && unit_index < total_units) {
            uint16_t second_code_unit = b_internal_read_uint16(string + unit_index * 2u);
            if (is_big_endian) {
                second_code_unit = b_internal_byte_swap_16(second_code_unit);
            }
            if (second_code_unit >= 0xDC00u && second_code_unit <= 0xDFFFu) {
                unit_index++;
            }
        }
    }
    if (unit_index >= total_units && codepoint_offset > 0u) {
        return empty_slice;
    }
    const size_t start_unit_index = unit_index;

    // Advance unit_index by codepoint_count more code-points.
    for (size_t i = 0u; i < codepoint_count && unit_index < total_units; i++) {
        uint16_t first_code_unit = b_internal_read_uint16(string + unit_index * 2u);
        if (is_big_endian) {
            first_code_unit = b_internal_byte_swap_16(first_code_unit);
        }
        unit_index++;
        if (first_code_unit >= 0xD800u && first_code_unit <= 0xDBFFu && unit_index < total_units) {
            uint16_t second_code_unit = b_internal_read_uint16(string + unit_index * 2u);
            if (is_big_endian) {
                second_code_unit = b_internal_byte_swap_16(second_code_unit);
            }
            if (second_code_unit >= 0xDC00u && second_code_unit <= 0xDFFFu) {
                unit_index++;
            }
        }
    }
    b_u16slice_t result;
    result.data        = string + start_unit_index * 2u;
    result.byte_length = (unit_index - start_unit_index) * 2u;
    return result;
}

size_t b_u16slice_units(const b_u16slice_t slice) {
    return slice.byte_length / 2u;
}

b_u32slice_t b_u32slice_of(const b_cstr_t string) {
    b_u32slice_t result;
    result.data        = string;
    result.byte_length = b_str_len(string);
    return result;
}

b_u32slice_t b_u32subslice(const b_cstr_t string, size_t byte_offset, size_t byte_length) {
    b_u32slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    // Force alignment to 4-byte code-unit boundaries.
    byte_offset &= ~(size_t)3u;
    byte_length &= ~(size_t)3u;
    const size_t total_byte_length = b_str_len(string) & ~(size_t)3u;
    if (byte_offset >= total_byte_length) {
        return empty_slice;
    }
    const size_t remaining_bytes = total_byte_length - byte_offset;
    b_u32slice_t result;
    result.data = string + byte_offset;
    if (byte_length > remaining_bytes) {
        result.byte_length = remaining_bytes;
    }
    else {
        result.byte_length = byte_length;
    }
    return result;
}

b_u32slice_t b_u32subslice_cp(const b_cstr_t string, const size_t codepoint_offset,
                                const size_t codepoint_count) {
    b_u32slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    // UTF-32: one 4-byte code-unit equals one code-point – no walking needed.
    const size_t total_byte_length = b_str_len(string) & ~(size_t)3u;
    const size_t total_units       = total_byte_length / 4u;
    if (codepoint_offset >= total_units) {
        return empty_slice;
    }
    const size_t available_units    = total_units - codepoint_offset;
    size_t actual_unit_count;
    if (codepoint_count > available_units) {
        actual_unit_count = available_units;
    }
    else {
        actual_unit_count = codepoint_count;
    }
    b_u32slice_t result;
    result.data        = string + codepoint_offset * 4u;
    result.byte_length = actual_unit_count * 4u;
    return result;
}

size_t b_u32slice_units(const b_u32slice_t slice) {
    return slice.byte_length / 4u;
}

b_slice_t b_subslice_cp(const b_cstr_t string, const size_t codepoint_offset,
                          const size_t codepoint_count) {
    b_slice_t empty_slice;
    empty_slice.data        = NULL;
    empty_slice.byte_length = 0u;
    if (!string) {
        return empty_slice;
    }
    const uint8_t encoding = b_str_enc(string);

    if (B_STR_IS_UTF32_ENC(encoding)) {
        const b_u32slice_t utf32_slice = b_u32subslice_cp(string, codepoint_offset, codepoint_count);
        b_slice_t result;
        result.data        = utf32_slice.data;
        result.byte_length = utf32_slice.byte_length;
        return result;
    }
    if (B_STR_IS_UTF16_ENC(encoding)) {
        const b_u16slice_t utf16_slice = b_u16subslice_cp(string, codepoint_offset, codepoint_count);
        b_slice_t result;
        result.data        = utf16_slice.data;
        result.byte_length = utf16_slice.byte_length;
        return result;
    }
    if (encoding == B_STR_ENC_UTF8) {
        const b_u8slice_t utf8_slice = b_u8subslice_cp(string, codepoint_offset, codepoint_count);
        b_slice_t result;
        result.data        = utf8_slice.data;
        result.byte_length = utf8_slice.byte_length;
        return result;
    }
    // ASCII: one byte equals one code-point.
    const size_t total_byte_length = b_str_len(string);
    if (codepoint_offset >= total_byte_length) {
        return empty_slice;
    }
    const size_t remaining_bytes = total_byte_length - codepoint_offset;
    b_slice_t result;
    result.data = string + codepoint_offset;
    if (codepoint_count > remaining_bytes) {
        result.byte_length = remaining_bytes;
    }
    else {
        result.byte_length = codepoint_count;
    }
    return result;
}


// 
//  ENCODING CONVERTERS  –  via SDL_iconv
// 

// b_internal_iconv: low-level iconv wrapper.
// Converts input (input_byte_count bytes long, source_encoding_name encoding) to a new b_str_t
// tagged output_encoding_tag. Returns NULL on any iconv error or OOM.
static b_str_t b_internal_iconv(const b_cstr_t input_string,
                                  const size_t input_byte_count,
                                  const char * const source_encoding_name,
                                  const char * const destination_encoding_name,
                                  const uint8_t output_encoding_tag) {
    SDL_iconv_t conversion_descriptor = SDL_iconv_open(destination_encoding_name,
                                                        source_encoding_name);
    if (conversion_descriptor == (SDL_iconv_t)-1) {
        return NULL;
    }

    // Worst case: UTF-8 → UTF-32 expands by 4×, plus a little slack.
    if (input_byte_count > (SIZE_MAX - 8u) / 4u) {
        SDL_iconv_close(conversion_descriptor);
        return NULL;
    }
    const size_t output_capacity = input_byte_count * 4u + 8u;
    uint8_t * const output_buffer = (uint8_t *)SDL_malloc(output_capacity);
    if (!output_buffer) {
        SDL_iconv_close(conversion_descriptor);
        return NULL;
    }

    const char *input_pointer         = (const char *)input_string;
    size_t      input_remaining_bytes  = input_byte_count;
    char       *output_pointer        = (char *)output_buffer;
    size_t      output_remaining_bytes = output_capacity;

    const size_t iconv_result = SDL_iconv(conversion_descriptor,
                                           &input_pointer, &input_remaining_bytes,
                                           &output_pointer, &output_remaining_bytes);
    SDL_iconv_close(conversion_descriptor);

    // Any error code or unconsumed input means we failed.
    if (iconv_result == SDL_ICONV_ERROR || iconv_result == SDL_ICONV_E2BIG
        || iconv_result == SDL_ICONV_EILSEQ || iconv_result == SDL_ICONV_EINVAL
        || input_remaining_bytes != 0u) {
        SDL_free(output_buffer);
        return NULL;
    }

    const size_t converted_byte_count = output_capacity - output_remaining_bytes;
    b_str_t result = b_str_new_pro(output_buffer, converted_byte_count, output_encoding_tag);
    SDL_free(output_buffer);
    return result;
}

b_str_t b_str_to_utf8(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding = b_str_enc(string);

    // Already UTF-8: nothing to do.
    if (encoding == B_STR_ENC_UTF8) {
        return string;
    }

    // ASCII and UTF-8 share the same byte layout; just retag.
    if (!B_STR_IS_UTF16_ENC(encoding) && !B_STR_IS_UTF32_ENC(encoding)) {
        if (string[-1] & B_STR_STATIC) {
            // Static: update the tag byte directly – no reallocation.
            b_str_set_enc(string, B_STR_ENC_UTF8);
            return string;
        }
        // Dynamic: dup and retag so the caller gets a fresh ownable string.
        b_str_t result = b_str_dup(string);
        if (!result) {
            return string; // OOM: return string to avoid losing the pointer
        }
        b_str_set_enc(result, B_STR_ENC_UTF8);
        return result;
    }

    // Choose the iconv source name based on the stored encoding tag.
    const char *source_encoding_name;
    if (encoding == B_STR_ENC_UTF16BE) {
        source_encoding_name = "UTF-16BE";
    }
    else if (encoding == B_STR_ENC_UTF16LE) {
        source_encoding_name = "UTF-16LE";
    }
    else if (encoding == B_STR_ENC_UTF32BE) {
        source_encoding_name = "UTF-32BE";
    }
    else {
        source_encoding_name = "UTF-32LE";
    }

    b_str_t result = b_internal_iconv(string, b_str_len(string),
                                       source_encoding_name, "UTF-8", B_STR_ENC_UTF8);
    return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF8);
}

b_str_t b_str_to_utf16(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding = b_str_enc(string);

    // Already UTF-16 LE: nothing to do.
    if (encoding == B_STR_ENC_UTF16LE) {
        return string;
    }

    if (encoding == B_STR_ENC_UTF16BE || B_STR_IS_UTF32_ENC(encoding)) {
        // Route through UTF-8 as an intermediate step.
        b_str_t intermediate = b_str_to_utf8(string);
        b_cstr_t source;
        if (intermediate && intermediate != string) {
            source = intermediate;
        }
        else {
            source = string;
        }
        b_str_t result = b_internal_iconv(source, b_str_len(source),
                                           "UTF-8", "UTF-16LE", B_STR_ENC_UTF16LE);
        if (intermediate && intermediate != string) {
            b_str_free(intermediate);
        }
        return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF16LE);
    }

    // ASCII or UTF-8: convert directly.
    b_str_t result = b_internal_iconv(string, b_str_len(string),
                                       "UTF-8", "UTF-16LE", B_STR_ENC_UTF16LE);
    return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF16LE);
}

b_str_t b_str_to_utf16be(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding = b_str_enc(string);

    if (encoding == B_STR_ENC_UTF16BE) {
        return string;
    }

    if (B_STR_IS_UTF16_ENC(encoding) || B_STR_IS_UTF32_ENC(encoding)) {
        b_str_t intermediate = b_str_to_utf8(string);
        b_cstr_t source;
        if (intermediate && intermediate != string) {
            source = intermediate;
        }
        else {
            source = string;
        }
        b_str_t result = b_internal_iconv(source, b_str_len(source),
                                           "UTF-8", "UTF-16BE", B_STR_ENC_UTF16BE);
        if (intermediate && intermediate != string) {
            b_str_free(intermediate);
        }
        return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF16BE);
    }

    b_str_t result = b_internal_iconv(string, b_str_len(string),
                                       "UTF-8", "UTF-16BE", B_STR_ENC_UTF16BE);
    return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF16BE);
}

b_str_t b_str_to_utf32le(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding = b_str_enc(string);

    if (encoding == B_STR_ENC_UTF32LE) {
        return string;
    }

    if (B_STR_IS_UTF16_ENC(encoding) || encoding == B_STR_ENC_UTF32BE) {
        b_str_t intermediate = b_str_to_utf8(string);
        b_cstr_t source;
        if (intermediate && intermediate != string) {
            source = intermediate;
        }
        else {
            source = string;
        }
        b_str_t result = b_internal_iconv(source, b_str_len(source),
                                           "UTF-8", "UTF-32LE", B_STR_ENC_UTF32LE);
        if (intermediate && intermediate != string) {
            b_str_free(intermediate);
        }
        return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF32LE);
    }

    b_str_t result = b_internal_iconv(string, b_str_len(string),
                                       "UTF-8", "UTF-32LE", B_STR_ENC_UTF32LE);
    return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF32LE);
}

b_str_t b_str_to_utf32be(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding = b_str_enc(string);

    if (encoding == B_STR_ENC_UTF32BE) {
        return string;
    }

    if (B_STR_IS_UTF16_ENC(encoding) || encoding == B_STR_ENC_UTF32LE) {
        b_str_t intermediate = b_str_to_utf8(string);
        b_cstr_t source;
        if (intermediate && intermediate != string) {
            source = intermediate;
        }
        else {
            source = string;
        }
        b_str_t result = b_internal_iconv(source, b_str_len(source),
                                           "UTF-8", "UTF-32BE", B_STR_ENC_UTF32BE);
        if (intermediate && intermediate != string) {
            b_str_free(intermediate);
        }
        return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF32BE);
    }

    b_str_t result = b_internal_iconv(string, b_str_len(string),
                                       "UTF-8", "UTF-32BE", B_STR_ENC_UTF32BE);
    return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF32BE);
}


// 
//  UTF-8 NFC NORMALISATION
// 

// NFC-normalise null_terminated_utf8 using utf8proc_decompose + utf8proc_reencode so that
// every allocation goes through SDL_malloc / SDL_free; utf8proc_map is NOT used
// because it allocates internally with the system malloc, breaking custom allocator support.
b_str_t b_str_utf8_norm(const char * const null_terminated_utf8) {
    if (!null_terminated_utf8) {
        return NULL;
    }

    const utf8proc_option_t decomposition_options =
        (utf8proc_option_t)(UTF8PROC_NULLTERM | UTF8PROC_STABLE | UTF8PROC_COMPOSE);

    // First pass: measure the number of codepoints in the decomposed form.
    // Passing NULL/0 for the buffer makes utf8proc_decompose return the required size.
    const utf8proc_ssize_t codepoint_count = utf8proc_decompose(
        (const utf8proc_uint8_t *)null_terminated_utf8, 0, NULL, 0, decomposition_options);
    if (codepoint_count < 0) {
        return NULL;
    }

    // Allocate our own codepoint buffer via SDL.
    // utf8proc_reencode writes UTF-8 in-place and needs one extra element of headroom.
    const size_t allocation_element_count = (size_t)codepoint_count + 1u;
    if (allocation_element_count > SIZE_MAX / sizeof(utf8proc_int32_t)) {
        return NULL;
    }
    utf8proc_int32_t * const codepoint_buffer = (utf8proc_int32_t *)SDL_malloc(
        allocation_element_count * sizeof(utf8proc_int32_t));
    if (!codepoint_buffer) {
        return NULL;
    }

    // Second pass: decompose into our buffer.
    const utf8proc_ssize_t filled_codepoint_count = utf8proc_decompose(
        (const utf8proc_uint8_t *)null_terminated_utf8, 0,
        codepoint_buffer, codepoint_count, decomposition_options);
    if (filled_codepoint_count < 0) {
        SDL_free(codepoint_buffer);
        return NULL;
    }

    // Re-encode the codepoints in-place to NFC UTF-8.
    const utf8proc_option_t reencode_options =
        (utf8proc_option_t)(UTF8PROC_STABLE | UTF8PROC_COMPOSE);
    const utf8proc_ssize_t byte_length = utf8proc_reencode(
        codepoint_buffer, filled_codepoint_count, reencode_options);
    if (byte_length < 0) {
        SDL_free(codepoint_buffer);
        return NULL;
    }

    // codepoint_buffer now holds UTF-8 bytes; copy into a proper b_str_t then release our buffer.
    b_str_t result = b_str_new_pro(codepoint_buffer, (size_t)byte_length, B_STR_ENC_UTF8);
    SDL_free(codepoint_buffer);
    return result;
}


// 
//  CASE CONVERSION  (UTF-8/ASCII only)
//  Returns string unchanged (never NULL) when the encoding is unsupported or OOM.
//  For static strings: result is written back in-place when it fits.
// 

b_str_t b_str_lower(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding = b_str_enc(string);
    // UTF-16 and UTF-32 are not handled; return string unchanged (silent fail).
    if (B_STR_IS_UTF16_ENC(encoding) || B_STR_IS_UTF32_ENC(encoding)) {
        return string;
    }

    const size_t input_length = b_str_len(string);
    // Allocate a generous output buffer; UTF-8 case folding can expand.
    size_t   output_capacity = input_length * 4u + 16u;
    uint8_t *output_buffer   = (uint8_t *)SDL_malloc(output_capacity);
    if (!output_buffer) {
        return string; // OOM: return string unchanged
    }

    size_t input_position  = 0u;
    size_t output_position = 0u;
    while (input_position < input_length) {
        utf8proc_int32_t codepoint;
        const size_t remaining_bytes = input_length - input_position;
        size_t safe_remaining_bytes;
        if (remaining_bytes < (size_t)SSIZE_MAX) {
            safe_remaining_bytes = remaining_bytes;
        }
        else {
            safe_remaining_bytes = (size_t)SSIZE_MAX;
        }
        const utf8proc_ssize_t advance_bytes = utf8proc_iterate(
            string + input_position, (utf8proc_ssize_t)safe_remaining_bytes, &codepoint);

        if (advance_bytes <= 0) {
            // Invalid byte: pass through unchanged; grow buffer if needed.
            if (output_position + 1u > output_capacity) {
                output_capacity *= 2u;
                uint8_t * const reallocated_buffer = (uint8_t *)SDL_realloc(
                    output_buffer, output_capacity);
                if (!reallocated_buffer) {
                    SDL_free(output_buffer);
                    return string;
                }
                output_buffer = reallocated_buffer;
            }
            output_buffer[output_position] = string[input_position];
            output_position++;
            input_position++;
            continue;
        }
        input_position += (size_t)advance_bytes;

        // Decompose, apply case-fold, then re-compose.
        utf8proc_int32_t folded_codepoints[4];
        int last_codepoint_category = 0;
        const utf8proc_ssize_t folded_count = utf8proc_decompose_char(
            codepoint, folded_codepoints,
            (utf8proc_ssize_t)(sizeof(folded_codepoints) / sizeof(folded_codepoints[0])),
            (utf8proc_option_t)(UTF8PROC_CASEFOLD | UTF8PROC_DECOMPOSE | UTF8PROC_COMPOSE),
            &last_codepoint_category);

        int total_codepoints;
        if (folded_count > 0) {
            total_codepoints = (int)folded_count;
        }
        else {
            total_codepoints = 1;
        }
        if (folded_count <= 0) {
            folded_codepoints[0] = utf8proc_tolower(codepoint);
        }

        for (int folded_index = 0; folded_index < total_codepoints; folded_index++) {
            // Ensure room for up to 4 UTF-8 bytes per code-point.
            if (output_position + 4u > output_capacity) {
                output_capacity = output_capacity * 2u + 16u;
                uint8_t * const reallocated_buffer = (uint8_t *)SDL_realloc(
                    output_buffer, output_capacity);
                if (!reallocated_buffer) {
                    SDL_free(output_buffer);
                    return string;
                }
                output_buffer = reallocated_buffer;
            }
            const utf8proc_ssize_t bytes_written = utf8proc_encode_char(
                folded_codepoints[folded_index], output_buffer + output_position);
            if (bytes_written > 0) {
                output_position += (size_t)bytes_written;
            }
        }
    }

    b_str_t result = b_str_new_pro(output_buffer, output_position, B_STR_ENC_UTF8);
    SDL_free(output_buffer);
    if (!result) {
        return string; // OOM when building the final string
    }
    return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF8);
}

b_str_t b_str_upper(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding = b_str_enc(string);
    if (B_STR_IS_UTF16_ENC(encoding) || B_STR_IS_UTF32_ENC(encoding)) {
        return string;
    }

    const size_t input_length = b_str_len(string);
    size_t   output_capacity = input_length * 4u + 16u;
    uint8_t *output_buffer   = (uint8_t *)SDL_malloc(output_capacity);
    if (!output_buffer) {
        return string;
    }

    size_t input_position  = 0u;
    size_t output_position = 0u;
    while (input_position < input_length) {
        utf8proc_int32_t codepoint;
        const size_t remaining_bytes = input_length - input_position;
        size_t safe_remaining_bytes;
        if (remaining_bytes < (size_t)SSIZE_MAX) {
            safe_remaining_bytes = remaining_bytes;
        }
        else {
            safe_remaining_bytes = (size_t)SSIZE_MAX;
        }
        const utf8proc_ssize_t advance_bytes = utf8proc_iterate(
            string + input_position, (utf8proc_ssize_t)safe_remaining_bytes, &codepoint);

        if (advance_bytes <= 0) {
            if (output_position + 1u > output_capacity) {
                output_capacity *= 2u;
                uint8_t * const reallocated_buffer = (uint8_t *)SDL_realloc(
                    output_buffer, output_capacity);
                if (!reallocated_buffer) {
                    SDL_free(output_buffer);
                    return string;
                }
                output_buffer = reallocated_buffer;
            }
            output_buffer[output_position] = string[input_position];
            output_position++;
            input_position++;
            continue;
        }
        input_position += (size_t)advance_bytes;

        if (output_position + 4u > output_capacity) {
            output_capacity = output_capacity * 2u + 16u;
            uint8_t * const reallocated_buffer = (uint8_t *)SDL_realloc(
                output_buffer, output_capacity);
            if (!reallocated_buffer) {
                SDL_free(output_buffer);
                return string;
            }
            output_buffer = reallocated_buffer;
        }
        const utf8proc_ssize_t bytes_written = utf8proc_encode_char(
            utf8proc_toupper(codepoint), output_buffer + output_position);
        if (bytes_written > 0) {
            output_position += (size_t)bytes_written;
        }
    }

    b_str_t result = b_str_new_pro(output_buffer, output_position, B_STR_ENC_UTF8);
    SDL_free(output_buffer);
    if (!result) {
        return string;
    }
    return b_internal_apply_or_keep(string, result, B_STR_ENC_UTF8);
}


// 
//  COMPARISON & SEARCH
// 

int b_str_cmp(const b_cstr_t left_string, const b_cstr_t right_string) {
    if (left_string == right_string) {
        return 0;
    }
    if (!left_string) {
        return -1;
    }
    if (!right_string) {
        return  1;
    }
    const size_t left_length  = b_str_len(left_string);
    const size_t right_length = b_str_len(right_string);
    size_t minimum_length;
    if (left_length < right_length) {
        minimum_length = left_length;
    }
    else {
        minimum_length = right_length;
    }
    const int comparison_result = SDL_memcmp(left_string, right_string, minimum_length);
    if (comparison_result) {
        return comparison_result;
    }
    if (left_length < right_length) {
        return -1;
    }
    if (left_length > right_length) {
        return  1;
    }
    return 0;
}

bool b_str_eq(const b_cstr_t left_string, const b_cstr_t right_string) {
    if (left_string == right_string) {
        return true;
    }
    if (!left_string || !right_string) {
        return false;
    }
    const size_t left_length = b_str_len(left_string);
    return left_length == b_str_len(right_string)
           && SDL_memcmp(left_string, right_string, left_length) == 0;
}

size_t b_str_find(const b_cstr_t haystack, const b_cstr_t needle) {
    return b_str_find_pro(haystack, needle, 0u);
}

size_t b_str_find_pro(const b_cstr_t haystack, const b_cstr_t needle,
                       const size_t from_byte_offset) {
    if (!haystack || !needle) {
        return SIZE_MAX;
    }
    const size_t haystack_length = b_str_len(haystack);
    const size_t needle_length   = b_str_len(needle);

    // An empty needle is found at the start (or from_byte_offset if valid).
    if (!needle_length) {
        if (from_byte_offset <= haystack_length) {
            return from_byte_offset;
        }
        return SIZE_MAX;
    }
    if (from_byte_offset >= haystack_length) {
        return SIZE_MAX;
    }
    if (needle_length > haystack_length - from_byte_offset) {
        return SIZE_MAX;
    }
    const size_t search_limit = haystack_length - needle_length;
    for (size_t search_index = from_byte_offset; search_index <= search_limit; search_index++) {
        if (SDL_memcmp(haystack + search_index, needle, needle_length) == 0) {
            return search_index;
        }
    }
    return SIZE_MAX;
}

bool b_str_contains(const b_cstr_t string, const b_cstr_t needle) {
    return b_str_find(string, needle) != SIZE_MAX;
}

bool b_str_starts_with(const b_cstr_t string, const b_cstr_t prefix) {
    if (!string || !prefix) {
        return false;
    }
    const size_t prefix_length = b_str_len(prefix);
    if (!prefix_length) {
        return true;
    }
    return prefix_length <= b_str_len(string)
           && SDL_memcmp(string, prefix, prefix_length) == 0;
}

bool b_str_ends_with(const b_cstr_t string, const b_cstr_t suffix) {
    if (!string || !suffix) {
        return false;
    }
    const size_t string_length = b_str_len(string);
    const size_t suffix_length = b_str_len(suffix);
    if (!suffix_length) {
        return true;
    }
    return suffix_length <= string_length
           && SDL_memcmp(string + string_length - suffix_length, suffix, suffix_length) == 0;
}


// 
//  IN-PLACE MUTATION
// 

// b_internal_is_utf16_whitespace: true when the 16-bit code unit at buffer[byte_offset]
// is an ASCII whitespace character.
static bool b_internal_is_utf16_whitespace(const uint8_t * const buffer,
                                            const size_t byte_offset,
                                            const bool is_big_endian) {
    uint16_t code_unit = b_internal_read_uint16(buffer + byte_offset);
    if (is_big_endian) {
        code_unit = b_internal_byte_swap_16(code_unit);
    }
    return code_unit == 0x0020u || code_unit == 0x0009u
        || code_unit == 0x000Au || code_unit == 0x000Du;
}

// b_internal_is_utf32_whitespace: true when the 32-bit code unit at buffer[byte_offset]
// is an ASCII whitespace character.
static bool b_internal_is_utf32_whitespace(const uint8_t * const buffer,
                                            const size_t byte_offset,
                                            const bool is_big_endian) {
    uint32_t code_unit = b_internal_read_uint32(buffer + byte_offset);
    if (is_big_endian) {
        code_unit = b_internal_byte_swap_32(code_unit);
    }
    return code_unit == 0x20u || code_unit == 0x09u
        || code_unit == 0x0Au || code_unit == 0x0Du;
}

b_str_t b_str_trim_r(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding            = b_str_enc(string);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);
    size_t        byte_length          = b_str_len(string);

    if (B_STR_IS_UTF32_ENC(encoding)) {
        const bool is_big_endian = (encoding == B_STR_ENC_UTF32BE);
        while (byte_length >= 4u
               && b_internal_is_utf32_whitespace(string, byte_length - 4u, is_big_endian)) {
            byte_length -= 4u;
        }
    }
    else if (B_STR_IS_UTF16_ENC(encoding)) {
        const bool is_big_endian = (encoding == B_STR_ENC_UTF16BE);
        while (byte_length >= 2u
               && b_internal_is_utf16_whitespace(string, byte_length - 2u, is_big_endian)) {
            byte_length -= 2u;
        }
    }
    else {
        while (byte_length > 0u) {
            const uint8_t current_byte = string[byte_length - 1u];
            if (current_byte == ' ' || current_byte == '\t'
                || current_byte == '\n' || current_byte == '\r') {
                byte_length--;
            }
            else {
                break;
            }
        }
    }
    b_str_set_len(string, byte_length);
    b_internal_write_null_terminator(string, byte_length, null_terminator_size);
    return string;
}

b_str_t b_str_trim_l(b_str_t string) {
    if (!string) {
        return NULL;
    }
    const uint8_t encoding            = b_str_enc(string);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);
    const size_t  byte_length          = b_str_len(string);
    size_t        start_byte_offset    = 0u;

    if (B_STR_IS_UTF32_ENC(encoding)) {
        const bool is_big_endian = (encoding == B_STR_ENC_UTF32BE);
        while (start_byte_offset + 4u <= byte_length
               && b_internal_is_utf32_whitespace(string, start_byte_offset, is_big_endian)) {
            start_byte_offset += 4u;
        }
    }
    else if (B_STR_IS_UTF16_ENC(encoding)) {
        const bool is_big_endian = (encoding == B_STR_ENC_UTF16BE);
        while (start_byte_offset + 2u <= byte_length
               && b_internal_is_utf16_whitespace(string, start_byte_offset, is_big_endian)) {
            start_byte_offset += 2u;
        }
    }
    else {
        while (start_byte_offset < byte_length) {
            const uint8_t current_byte = string[start_byte_offset];
            if (current_byte == ' ' || current_byte == '\t'
                || current_byte == '\n' || current_byte == '\r') {
                start_byte_offset++;
            }
            else {
                break;
            }
        }
    }

    if (start_byte_offset > 0u) {
        const size_t new_byte_length = byte_length - start_byte_offset;
        SDL_memmove(string, string + start_byte_offset, new_byte_length);
        b_str_set_len(string, new_byte_length);
        b_internal_write_null_terminator(string, new_byte_length, null_terminator_size);
    }
    return string;
}

b_str_t b_str_trim(b_str_t string) {
    return b_str_trim_r(b_str_trim_l(string));
}

b_str_t b_str_repeat(const b_cstr_t string, const size_t repeat_count) {
    uint8_t encoding;
    if (string) {
        encoding = b_str_enc(string);
    }
    else {
        encoding = B_STR_ENC_ASCII;
    }
    if (!string || !repeat_count) {
        return b_str_new_pro(NULL, 0u, encoding);
    }
    const size_t unit_byte_length = b_str_len(string);
    if (!unit_byte_length) {
        return b_str_new_pro(NULL, 0u, encoding);
    }
    if (unit_byte_length > SIZE_MAX / repeat_count) {
        return NULL; // overflow: cannot represent total length
    }

    const size_t total_byte_length = unit_byte_length * repeat_count;
    b_str_t result = b_str_new_pro(NULL, total_byte_length, encoding);
    if (!result) {
        return NULL;
    }
    for (size_t i = 0u; i < repeat_count; i++) {
        SDL_memcpy(result + i * unit_byte_length, string, unit_byte_length);
    }
    return result;
}


// 
//  VALIDATION
// 

bool b_str_valid_utf8(const b_cstr_t string) {
    if (!string) {
        return true; // NULL is treated as a valid empty string
    }
    const size_t total_byte_length = b_str_len(string);
    size_t byte_position = 0u;
    while (byte_position < total_byte_length) {
        utf8proc_int32_t codepoint;
        const size_t remaining_bytes = total_byte_length - byte_position;
        size_t safe_remaining_bytes;
        if (remaining_bytes < (size_t)SSIZE_MAX) {
            safe_remaining_bytes = remaining_bytes;
        }
        else {
            safe_remaining_bytes = (size_t)SSIZE_MAX;
        }
        const utf8proc_ssize_t advance_bytes = utf8proc_iterate(
            string + byte_position, (utf8proc_ssize_t)safe_remaining_bytes, &codepoint);
        if (advance_bytes <= 0) {
            return false; // invalid sequence found
        }
        byte_position += (size_t)advance_bytes;
    }
    return true;
}


// 
//  BOM  (byte-order mark)
// 

uint8_t b_str_detect_bom(const void * const data, const size_t byte_length,
                          size_t * const bom_size_out) {
    if (bom_size_out) {
        *bom_size_out = 0u;
    }
    if (!data || byte_length < 2u) {
        return B_STR_ENC_ASCII;
    }
    const uint8_t * const raw_bytes = (const uint8_t *)data;

    // UTF-32 BE: 00 00 FE FF — must be tested before UTF-16 BE (same first 2 bytes).
    if (byte_length >= 4u
        && raw_bytes[0] == 0x00u && raw_bytes[1] == 0x00u
        && raw_bytes[2] == 0xFEu && raw_bytes[3] == 0xFFu) {
        if (bom_size_out) { *bom_size_out = 4u; }
        return B_STR_ENC_UTF32BE;
    }
    // UTF-32 LE: FF FE 00 00 — must be tested before UTF-16 LE (same first 2 bytes).
    if (byte_length >= 4u
        && raw_bytes[0] == 0xFFu && raw_bytes[1] == 0xFEu
        && raw_bytes[2] == 0x00u && raw_bytes[3] == 0x00u) {
        if (bom_size_out) { *bom_size_out = 4u; }
        return B_STR_ENC_UTF32LE;
    }
    // UTF-8: EF BB BF
    if (byte_length >= 3u
        && raw_bytes[0] == 0xEFu && raw_bytes[1] == 0xBBu && raw_bytes[2] == 0xBFu) {
        if (bom_size_out) { *bom_size_out = 3u; }
        return B_STR_ENC_UTF8;
    }
    // UTF-16 LE: FF FE
    if (raw_bytes[0] == 0xFFu && raw_bytes[1] == 0xFEu) {
        if (bom_size_out) { *bom_size_out = 2u; }
        return B_STR_ENC_UTF16LE;
    }
    // UTF-16 BE: FE FF
    if (raw_bytes[0] == 0xFEu && raw_bytes[1] == 0xFFu) {
        if (bom_size_out) { *bom_size_out = 2u; }
        return B_STR_ENC_UTF16BE;
    }

    return B_STR_ENC_ASCII;
}

// b_internal_bom_bytes_for_encoding: return a pointer to the correct BOM bytes for encoding,
// and set *bom_size_out to its length. Returns NULL for ASCII (no BOM).
static const uint8_t *b_internal_bom_bytes_for_encoding(const uint8_t encoding,
                                                          size_t * const bom_size_out) {
    switch (encoding & B_STR_ENC_MASK) {
    case B_STR_ENC_UTF8:    *bom_size_out = 3u; return BOM_UTF8;
    case B_STR_ENC_UTF16LE: *bom_size_out = 2u; return BOM_UTF16LE;
    case B_STR_ENC_UTF16BE: *bom_size_out = 2u; return BOM_UTF16BE;
    case B_STR_ENC_UTF32LE: *bom_size_out = 4u; return BOM_UTF32LE;
    case B_STR_ENC_UTF32BE: *bom_size_out = 4u; return BOM_UTF32BE;
    default:                *bom_size_out = 0u; return NULL;
    }
}

b_str_t b_str_add_bom(b_str_t string) {
    if (!string) {
        return NULL;
    }
    size_t bom_size;
    const uint8_t * const bom_bytes = b_internal_bom_bytes_for_encoding(
        b_str_enc(string), &bom_size);
    if (!bom_bytes || !bom_size) {
        return string; // ASCII has no BOM; nothing to do
    }

    const size_t  current_length      = b_str_len(string);
    const uint8_t encoding            = b_str_enc(string);
    const size_t  null_terminator_size = b_internal_null_terminator_size(encoding);

    string = b_str_ensure(string, bom_size);
    if (b_str_avail(string) < bom_size) {
        return string; // OOM or static full: fail silently
    }

    // Shift existing content right to make room at the front.
    SDL_memmove(string + bom_size, string, current_length + null_terminator_size);
    SDL_memcpy(string, bom_bytes, bom_size);
    b_str_set_len(string, current_length + bom_size);
    return string;
}


// 
//  FILE I/O  –  synchronous, using SDL IOStream
//
//  fopen/fread/fwrite/fclose are NOT used anywhere below.
//  All file access goes through SDL_IOFromFile / SDL_GetIOSize /
//  SDL_ReadIO / SDL_WriteIO / SDL_CloseIO.
// 

// b_internal_read_entire_file: read a file into a SDL_malloc'd buffer.
// On success, *file_size_out is set to the file size and the caller owns the buffer.
// The buffer has 4 extra zero bytes at the end (widest null terminator for UTF-32).
static uint8_t *b_internal_read_entire_file(const char * const path,
                                              size_t * const file_size_out) {
    SDL_IOStream * const io_stream = SDL_IOFromFile(path, "rb");
    if (!io_stream) {
        return NULL;
    }

    const Sint64 file_size_signed = SDL_GetIOSize(io_stream);
    if (file_size_signed < 0) {
        SDL_CloseIO(io_stream);
        return NULL;
    }

    const size_t file_size = (size_t)file_size_signed;

    // +4 trailing zero bytes to cover any null-terminator width (UTF-32 needs 4).
    uint8_t * const file_buffer = (uint8_t *)SDL_malloc(file_size + 4u);
    if (!file_buffer) {
        SDL_CloseIO(io_stream);
        return NULL;
    }
    SDL_memset(file_buffer + file_size, 0, 4u);

    const size_t bytes_read = SDL_ReadIO(io_stream, file_buffer, file_size);
    SDL_CloseIO(io_stream);

    if (bytes_read != file_size) {
        SDL_free(file_buffer);
        return NULL;
    }

    *file_size_out = file_size;
    return file_buffer;
}

b_str_t b_str_load_file(const char * const path, uint8_t fallback_encoding) {
    if (!path) {
        return NULL;
    }
    if (!fallback_encoding) {
        fallback_encoding = B_STR_ENC_UTF8;
    }

    size_t file_size = 0u;
    uint8_t * const raw_bytes = b_internal_read_entire_file(path, &file_size);
    if (!raw_bytes) {
        return NULL;
    }

    size_t        bom_size           = 0u;
    const uint8_t detected_encoding  = b_str_detect_bom(raw_bytes, file_size, &bom_size);
    uint8_t effective_encoding;
    if (bom_size) {
        effective_encoding = detected_encoding;
    }
    else {
        effective_encoding = fallback_encoding;
    }

    b_str_t result = b_str_new_pro(raw_bytes + bom_size, file_size - bom_size, effective_encoding);
    SDL_free(raw_bytes);
    return result;
}

int b_str_save_file(const char * const path, const b_cstr_t string, const bool write_bom) {
    if (!path || !string) {
        return -1;
    }

    SDL_IOStream * const io_stream = SDL_IOFromFile(path, "wb");
    if (!io_stream) {
        return -1;
    }

    if (write_bom) {
        size_t bom_size;
        const uint8_t * const bom_bytes = b_internal_bom_bytes_for_encoding(
            b_str_enc(string), &bom_size);
        if (bom_bytes && bom_size) {
            if (SDL_WriteIO(io_stream, bom_bytes, bom_size) != bom_size) {
                SDL_CloseIO(io_stream);
                return -1;
            }
        }
    }

    const size_t content_length = b_str_len(string);
    if (content_length) {
        if (SDL_WriteIO(io_stream, string, content_length) != content_length) {
            SDL_CloseIO(io_stream);
            return -1;
        }
    }

    SDL_CloseIO(io_stream);
    return 0;
}

int b_file_add_bom(const char * const path, const uint8_t encoding) {
    size_t bom_size;
    const uint8_t * const bom_bytes = b_internal_bom_bytes_for_encoding(encoding, &bom_size);
    if (!bom_bytes || !bom_size) {
        return 0; // ASCII: no BOM exists for this encoding, nothing to do
    }

    size_t file_size = 0u;
    uint8_t * const raw_bytes = b_internal_read_entire_file(path, &file_size);
    if (!raw_bytes) {
        return -1;
    }

    // Already has this exact BOM: leave the file untouched.
    if (file_size >= bom_size && SDL_memcmp(raw_bytes, bom_bytes, bom_size) == 0) {
        SDL_free(raw_bytes);
        return 0;
    }

    SDL_IOStream * const io_stream = SDL_IOFromFile(path, "wb");
    if (!io_stream) {
        SDL_free(raw_bytes);
        return -1;
    }

    int result = 0;
    if (SDL_WriteIO(io_stream, bom_bytes, bom_size) != bom_size) {
        result = -1;
    }
    if (result == 0 && file_size) {
        if (SDL_WriteIO(io_stream, raw_bytes, file_size) != file_size) {
            result = -1;
        }
    }
    SDL_CloseIO(io_stream);
    SDL_free(raw_bytes);
    return result;
}


// 
//  FILE I/O  –  asynchronous, using SDL AsyncIO
// 

bool b_str_load_file_async(const char * const path, SDL_AsyncIOQueue * const queue,
                            void * const userdata) {
    if (!path || !queue) {
        return false;
    }
    // SDL_LoadFileAsync allocates the buffer, reads the file, and null-terminates.
    // Results land in queue; call b_str_from_async_result() when done.
    return SDL_LoadFileAsync(path, queue, userdata);
}

b_str_t b_str_from_async_result(const SDL_AsyncIOOutcome * const outcome,
                                  const uint8_t fallback_encoding) {
    if (!outcome || outcome->result != SDL_ASYNCIO_COMPLETE) {
        return NULL;
    }
    // An empty file is valid; produce an empty b_str_t with the fallback encoding.
    if (!outcome->buffer || outcome->bytes_transferred == 0u) {
        const uint8_t effective_encoding = fallback_encoding ? fallback_encoding : B_STR_ENC_UTF8;
        return b_str_new_pro(NULL, 0u, effective_encoding);
    }

    const uint8_t effective_fallback   = fallback_encoding ? fallback_encoding : B_STR_ENC_UTF8;
    const uint8_t * const raw_bytes    = (const uint8_t *)outcome->buffer;
    const size_t transfer_size         = (size_t)outcome->bytes_transferred;

    // Detect and skip a BOM just like the synchronous b_str_load_file.
    size_t        bom_size           = 0u;
    const uint8_t detected_encoding  = b_str_detect_bom(raw_bytes, transfer_size, &bom_size);
    uint8_t effective_encoding;
    if (bom_size) {
        effective_encoding = detected_encoding;
    }
    else {
        effective_encoding = effective_fallback;
    }

    // NOTE: bytes_transferred does NOT include the null byte SDL appended.
    // Caller must SDL_free(outcome->buffer) after this call.
    return b_str_new_pro(raw_bytes + bom_size, transfer_size - bom_size, effective_encoding);
}


// 
//  GENERIC FILE CONVERTER  (public API – see bent.h for full parameter docs)
// 

int b_file_convert(const char * const input_path, const char * const output_path,
                   const uint8_t fallback_encoding, const uint8_t output_encoding,
                   const bool write_bom) {
    const uint8_t effective_fallback = fallback_encoding ? fallback_encoding : B_STR_ENC_UTF8;
    b_str_t source_string = b_str_load_file(input_path, effective_fallback);
    if (!source_string) {
        return -1;
    }

    const uint8_t source_encoding = b_str_enc(source_string);
    b_str_t destination_string = NULL;

    if (source_encoding == output_encoding) {
        // Same encoding: just duplicate.
        destination_string = b_str_dup(source_string);
    }
    else if (!B_STR_IS_UTF16_ENC(source_encoding) && !B_STR_IS_UTF32_ENC(source_encoding)
             && !B_STR_IS_UTF16_ENC(output_encoding) && !B_STR_IS_UTF32_ENC(output_encoding)) {
        // Both encodings are byte-width (ASCII/UTF-8): dup and retag.
        destination_string = b_str_dup(source_string);
        if (destination_string) {
            b_str_set_enc(destination_string, output_encoding);
        }
    }
    else {
        // At least one side is wide: route through UTF-8 as an intermediate.
        b_str_t intermediate_string = NULL;
        if (B_STR_IS_UTF16_ENC(source_encoding) || B_STR_IS_UTF32_ENC(source_encoding)) {
            intermediate_string = b_str_to_utf8(source_string);
            if (intermediate_string == source_string) {
                // to_utf8 returned the same pointer, which means the conversion
                // actually failed (not a no-op, because source_string is wide).
                b_str_free(source_string);
                return -1;
            }
        }
        else {
            intermediate_string = b_str_dup(source_string);
            if (intermediate_string) {
                b_str_set_enc(intermediate_string, B_STR_ENC_UTF8);
            }
        }
        if (!intermediate_string) {
            b_str_free(source_string);
            return -1;
        }

        if (!B_STR_IS_UTF16_ENC(output_encoding) && !B_STR_IS_UTF32_ENC(output_encoding)) {
            // Output is byte-width: retag the UTF-8 intermediate.
            destination_string = intermediate_string;
            intermediate_string = NULL;
            b_str_set_enc(destination_string, output_encoding);
        }
        else if (output_encoding == B_STR_ENC_UTF16LE) {
            destination_string = b_str_to_utf16(intermediate_string);
            if (destination_string == intermediate_string) {
                destination_string = NULL; // conversion failed
            }
        }
        else if (output_encoding == B_STR_ENC_UTF16BE) {
            destination_string = b_str_to_utf16be(intermediate_string);
            if (destination_string == intermediate_string) {
                destination_string = NULL;
            }
        }
        else if (output_encoding == B_STR_ENC_UTF32LE) {
            destination_string = b_str_to_utf32le(intermediate_string);
            if (destination_string == intermediate_string) {
                destination_string = NULL;
            }
        }
        else if (output_encoding == B_STR_ENC_UTF32BE) {
            destination_string = b_str_to_utf32be(intermediate_string);
            if (destination_string == intermediate_string) {
                destination_string = NULL;
            }
        }

        if (intermediate_string) {
            b_str_free(intermediate_string);
        }
    }

    b_str_free(source_string);
    if (!destination_string) {
        return -1;
    }

    b_str_set_enc(destination_string, output_encoding);
    const int result = b_str_save_file(output_path, destination_string, write_bom);
    b_str_free(destination_string);
    return result;
}


// 
//  FILE CONVERSION WRAPPERS
//  Each wrapper calls b_file_convert with hard-coded encoding constants.
//  Use b_file_convert() directly when you need a custom fallback encoding.
// 

int b_file_conv_ascii_to_utf8_bom   (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_ASCII,   B_STR_ENC_UTF8,    true);  }
int b_file_conv_ascii_to_utf8_no_bom(const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_ASCII,   B_STR_ENC_UTF8,    false); }

int b_file_conv_utf8_to_utf16               (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, b_internal_system_utf16_encoding(), true);  }
int b_file_conv_utf8_to_utf16le_bom         (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF16LE,  true);  }
int b_file_conv_utf8_to_utf16le_no_bom      (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF16LE,  false); }
int b_file_conv_utf8_to_utf16be_bom         (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF16BE,  true);  }
int b_file_conv_utf8_to_utf16be_no_bom      (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF16BE,  false); }

int b_file_conv_utf8_to_utf32               (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, b_internal_system_utf32_encoding(), true);  }
int b_file_conv_utf8_to_utf32le_bom         (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF32LE,  true);  }
int b_file_conv_utf8_to_utf32le_no_bom      (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF32LE,  false); }
int b_file_conv_utf8_to_utf32be_bom         (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF32BE,  true);  }
int b_file_conv_utf8_to_utf32be_no_bom      (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF8, B_STR_ENC_UTF32BE,  false); }

int b_file_conv_utf16_to_utf8_no_bom          (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF16LE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf16le_bom_to_utf8_no_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF16LE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf16le_no_bom_to_utf8_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF16LE, B_STR_ENC_UTF8,  true); }
int b_file_conv_utf16le_no_bom_to_utf8_no_bom (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF16LE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf16be_bom_to_utf8_no_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF16BE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf16be_no_bom_to_utf8_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF16BE, B_STR_ENC_UTF8,  true); }
int b_file_conv_utf16be_no_bom_to_utf8_no_bom (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF16BE, B_STR_ENC_UTF8, false); }

int b_file_conv_utf32_to_utf8_no_bom          (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF32LE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf32le_bom_to_utf8_no_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF32LE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf32le_no_bom_to_utf8_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF32LE, B_STR_ENC_UTF8,  true); }
int b_file_conv_utf32le_no_bom_to_utf8_no_bom (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF32LE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf32be_bom_to_utf8_no_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF32BE, B_STR_ENC_UTF8, false); }
int b_file_conv_utf32be_no_bom_to_utf8_bom    (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF32BE, B_STR_ENC_UTF8,  true); }
int b_file_conv_utf32be_no_bom_to_utf8_no_bom (const char *input_path, const char *output_path) { return b_file_convert(input_path, output_path, B_STR_ENC_UTF32BE, B_STR_ENC_UTF8, false); }


// 
//  UTF PRINT HELPERS
//  Convert wide-encoding slices to UTF-8 then output via SDL_Log.
// 

void b_str_print_utf16(const b_u16slice_t slice) {
    if (!slice.data || !slice.byte_length) {
        return;
    }

    // Build a temporary UTF-16 LE b_str_t from the slice.
    b_str_t temporary_utf16_string = b_str_from_u16slice(slice);
    if (!temporary_utf16_string) {
        return;
    }

    // Convert to UTF-8 for output.
    b_str_t temporary_utf8_string = b_str_to_utf8(temporary_utf16_string);

    // to_utf8 returns temporary_utf16_string unchanged when conversion fails; check for that.
    if (temporary_utf8_string && temporary_utf8_string != temporary_utf16_string) {
        SDL_Log("%.*s", (int)b_str_len(temporary_utf8_string), (const char *)temporary_utf8_string);
        b_str_free(temporary_utf8_string);
    }

    b_str_free(temporary_utf16_string);
}

void b_str_print_utf32(const b_u32slice_t slice) {
    if (!slice.data || !slice.byte_length) {
        return;
    }

    // Build a temporary UTF-32 LE b_str_t from the slice.
    b_str_t temporary_utf32_string = b_str_from_u32slice(slice);
    if (!temporary_utf32_string) {
        return;
    }

    // Convert UTF-32 → UTF-8 via the intermediate UTF-8 path.
    b_str_t temporary_utf8_string = b_str_to_utf8(temporary_utf32_string);

    if (temporary_utf8_string && temporary_utf8_string != temporary_utf32_string) {
        SDL_Log("%.*s", (int)b_str_len(temporary_utf8_string), (const char *)temporary_utf8_string);
        b_str_free(temporary_utf8_string);
    }

    b_str_free(temporary_utf32_string);
}
