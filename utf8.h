// (Below code taken from https://github.com/ocornut/imgui which in turn took it from Christopher Wellons)
// Minimal adjustments by me

// Convert UTF-8 to 32-bit character, process single character input.
// A nearly-branchless UTF-8 decoder, based on work of Christopher Wellons (https://github.com/skeeto/branchless-utf8).
// We handle UTF-8 decoding error by skipping forward.
int Utf8ToUnicodeCodepoint(unsigned int* out_char, const char* in_text, const char* in_text_end)
{
#define UNICODE_CODEPOINT_INVALID 0xFFFD     // Invalid Unicode code point (standard value).
#define UNICODE_CODEPOINT_MAX     0x10FFFF   // Maximum Unicode code point supported by this build.

  static const char lengths[32] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 2, 2, 2, 2, 3, 3, 4, 0 };
  static const int masks[]  = { 0x00, 0x7f, 0x1f, 0x0f, 0x07 };
  static const uint32_t mins[] = { 0x400000, 0, 0x80, 0x800, 0x10000 };
  static const int shiftc[] = { 0, 18, 12, 6, 0 };
  static const int shifte[] = { 0, 6, 4, 2, 0 };
  int len = lengths[*(const unsigned char*)in_text >> 3];
  int wanted = len + (len ? 0 : 1);

  if (in_text_end == NULL)
    in_text_end = in_text + wanted; // Max length, nulls will be taken into account.

  // Copy at most 'len' bytes, stop copying at 0 or past in_text_end. Branch predictor does a good job here,
  // so it is fast even with excessive branching.
  unsigned char s[4];
  s[0] = in_text + 0 < in_text_end ? in_text[0] : 0;
  s[1] = in_text + 1 < in_text_end ? in_text[1] : 0;
  s[2] = in_text + 2 < in_text_end ? in_text[2] : 0;
  s[3] = in_text + 3 < in_text_end ? in_text[3] : 0;

  // Assume a four-byte character and load four bytes. Unused bits are shifted out.
  *out_char  = (uint32_t)(s[0] & masks[len]) << 18;
  *out_char |= (uint32_t)(s[1] & 0x3f) << 12;
  *out_char |= (uint32_t)(s[2] & 0x3f) <<  6;
  *out_char |= (uint32_t)(s[3] & 0x3f) <<  0;
  *out_char >>= shiftc[len];

  // Accumulate the various error conditions.
  int e = 0;
  e  = (*out_char < mins[len]) << 6; // non-canonical encoding
  e |= ((*out_char >> 11) == 0x1b) << 7;  // surrogate half?
  e |= (*out_char > UNICODE_CODEPOINT_MAX) << 8;  // out of range?
  e |= (s[1] & 0xc0) >> 2;
  e |= (s[2] & 0xc0) >> 4;
  e |= (s[3]       ) >> 6;
  e ^= 0x2a; // top two bits of each tail byte correct?
  e >>= shifte[len];

  if (e)
  {
    // No bytes are consumed when *in_text == 0 || in_text == in_text_end.
    // One byte is consumed in case of invalid first byte of in_text.
    // All available bytes (at most `len` bytes) are consumed on incomplete/invalid second to last bytes.
    // Invalid or incomplete input may consume less bytes than wanted, therefore every byte has to be inspected in s.
    wanted = min(wanted, !!s[0] + !!s[1] + !!s[2] + !!s[3]);
    *out_char = UNICODE_CODEPOINT_INVALID;
  }

  return wanted;
}