#include <stdlib.h>
#include <iconv.h>

/* exported function */
void
aribstr_to_utf8 (char *source, size_t len, char *dest, size_t buf_len);

#define CODE_ASCII ('\x40')
#define CODE_JISX0208_1978 ('\x40')
#define CODE_JISX0208 ('\x42')
#define CODE_JISX0213_1 ('\x51')
#define CODE_JISX0213_2 ('\x50')
#define CODE_JISX0201_KATA ('\x49')
#define CODE_EXT ('\x3B')
#define CODE_X_HIRA ('\x30')
#define CODE_X_HIRA_P ('\x37')
#define CODE_X_KATA ('\x31')
#define CODE_X_KATA_P ('\x38')

struct g_char_s {
  char *buf;
  size_t len;
  size_t used;
};
typedef struct g_char_s gstr;

struct code_state
{
  int gl;                       /* index of the group invoked to GL */
  int gr;                       /* index of the group invoked to GR */
  int ss;                       /* flag if in SS2 or SS3.  2:SS2, 3:SS3 */
  struct
  {
    unsigned char mb;
    unsigned char code;
  } g[4];
};

static const char trans_hira[] = {
  '\xB5', '\xB6', '\xBC', '\xA3', '\xD6', '\xD7', '\xA2', '\xA6'
};

static const char *trans_kata = trans_hira;

static const char *trans_ext90[] = {
  /* 45-49 */
  "10.", "11.", "12.", "[HV]", "[SD]",
  /* 50-54 */
  "[P]", "[W]", "[MV]", "[\xBC\xEA]", "[\xBB\xFA]",
  /* 55-59 */
  "[\xC1\xD0]", "[\xA5\xC7]", "[S]", "[\xC6\xF3]", "[\xC2\xBF]",
  /* 60-64 */
  "[\xB2\xF2]", "[SS]", "[B]", "[N]", "\xA2\xA3",
  /* 65-69 */
  "\xA1\xFC", "[\xC5\xB7]", "[\xB8\xF2]", "[\xB1\xC7]", "[\xCC\xB5]",
  /* 70-74 */
  "[\xCE\xC1]", ".", "[\xC1\xB0]", "[\xB8\xE5]", "[\xBA\xC6]",
  /* 75-79 */
  "[\xBF\xB7]", "[\xBD\xE9]", "[\xBD\xAA]", "[\xC0\xB8]", "[\xC8\xCE]",
  /* 80-84 */
  "[\xC0\xBC]", "[\xBF\xE1]", "[PPV]", "(\xC8\xEB)", "\xC2\xBE",
  /* 85-94 */
  ".", ".", ".", ".", ".", ".", ".", ".", ".", "."
};

static const char *trans_ext92[][94] = {
  {                             /* row 92 */
        "\xA2\xAA", "\xA2\xAB", "\xA2\xAC", "\xA2\xAD", "\xA6\xBB",
        "\xA6\xBC", "\xC7\xAF", "\xB7\xEE", "\xC6\xFC", "\xB1\xDF",
        "\xAD\xD6", "m^3", "\xAD\xD1", "\xAD\xD1^2", "\xAD\xD1^3",
        "\xA3\xB0.", "\xA3\xB1.", "\xA3\xB2.", "\xA3\xB3.", "\xA3\xB4.",
        "\xA3\xB5.", "\xA3\xB6.", "\xA3\xB7.", "\xA3\xB8.", "\xA3\xB9.",
        "\xBB\xE1", "\xC9\xFB", "\xB8\xB5", "\xB8\xCE", "\xC1\xB0",
        "\xBF\xB7", "\xA3\xB0,", "\xA3\xB1,", "\xA3\xB2,", "\xA3\xB3,",
        "\xA3\xB4,", "\xA3\xB5,", "\xA3\xB6,", "\xA3\xB7,", "\xA3\xB8,",
        "\xA3\xB9,", "[\xBC\xD2]", "[\xBA\xE2]", "\xAD\xEB", "\xAD\xEA",
        "\xAD\xEC", "(\xCC\xE4)", "\xA3\xA2", "\xA3\xA4", "\xA2\xDA",
        "\xA2\xDB", "\xAD\xFD", "^2", "^3", "(CD)",
        "(vn)", "(ob)", "(cb)", "(ce", "mb)",
        "(hp)", "(br)", "(p)", "(s)", "(ms)",
        "(t)", "(bs)", "(b)", "(tb)", "(tp)",
        "(ds)", "(ag)", "(eg)", "(vo)", "(fl)",
        "(ke", "y)", "(sa", "x)", "(sy",
        "n)", "(or", "g)", "(pe", "r)",
        "(R)", "(C)", "(\xE4\xB7)", "DJ", "[\xB1\xE9]",
      "Fax", ".", ".", "."},
  {                             /* row 93 */
        "(\xB7\xEE)", "(\xB2\xD0)", "(\xBF\xE5)", "(\xCC\xDA)", "(\xB6\xE2)",
        "(\xC5\xDA)", "(\xC6\xFC)", "(\xBD\xCB)", "\xAD\xED", "\xAD\xEE",
        "\xAD\xEF", "\xAD\xDF", "\xAD\xE2", "\xAD\xE4", "\xA2\xA9",
        "\xA2\xD3", "[\xCB\xDC]", "[\xBB\xB0]", "[\xC6\xF3]", "[\xB0\xC2]",
        "[\xC5\xC0]", "[\xC2\xC7]", "[\xC5\xF0]", "[\xBE\xA1]", "[\xC7\xD4]",
        "[S]", "[\xC5\xEA]", "[\xCA\xE1]", "[\xB0\xEC]", "[\xC6\xF3]",
        "[\xBB\xB0]", "[\xCD\xB7]", "[\xBA\xB8]", "[\xC3\xE6]", "[\xB1\xA6]",
        "[\xBB\xD8]", "[\xC1\xF6]", "[\xC2\xC7]", "\xA3\xDF", "\xAD\xD4",
        "Hz", "ha", "\xAD\xD2", "\xAD\xD2^2", "hPa",
        ".", ".", "\xA9\xB4", "0/3", "\xA7\xF8",
        "\xA7\xF9", "\xA9\xB3", "\xA9\xB5", "\xA7\xFA", "2/5",
        "3/5", "4/5", "1/6", "5/6", "1/7",
        "1/8", "1/9", "1/10", "\xA6\xE8", "\xA6\xE9",
        "\xA6\xEA", "\xA6\xEB", "\xA6\xE4", "\xA6\xE5", "\xA6\xE4",
        "\xA6\xE5", "\xA6\xBC", "\xA6\xBE", "\xA6\xC0", "\xA6\xBA",
        ".", "\xA3\xBA", "\xA8\xEB", "\xA8\xEE", ".",
        /* 81-90 */
        ".", ".", ".", ".", ".", ".", ".", ".", ".", "\xA2\xFC",
      "\xA6\xE7", ".", ".", "."},
  {                             /* row 94 */
        "\xAD\xB5", "\xAD\xB6", "\xAD\xB7", "\xAD\xB8", "\xAD\xB9",
        "\xAD\xBA", "\xAD\xBB", "\xAD\xBC", "\xAD\xBD", "\xAD\xBE",
        "\xAD\xBF", "\xAD\xD7", "\xAD\xB1", "\xAD\xB2", "\xAD\xB3",
        "\xAD\xB4", "(1)", "(2)", "(3)", "(4)",
        "(5)", "(6)", "(7)", "(8)", "(9)",
        "(10)", "(11)", "(12)", "\xA8\xC1", "\xA8\xC2",
        "\xA8\xC3", "\xA8\xC4", "(A)", "(B)", "(C)",
        "(D)", "(E)", "(F)", "(G)", "(H)",
        "(I)", "(J)", "(K)", "(L)", "(M)",
        "(N)", "(O)", "(P)", "(Q)", "(R)",
        "(S)", "(T)", "(U)", "(V)", "(W)",
        "(X)", "(Y)", "(Z)", "\xA8\xC5", "\xA8\xC6",
        "\xA8\xC7", "\xA8\xC8", "\xA8\xC9", "\xA8\xCA", "\xAD\xA1",
        "\xAD\xA2", "\xAD\xA3", "\xAD\xA4", "\xAD\xA5", "\xAD\xA6",
        "\xAD\xA7", "\xAD\xA8", "\xAD\xA9", "\xAD\xAA", "\xAD\xAB",
        "\xAD\xAC", "\xAD\xAD", "\xAD\xAE", "\xAD\xAF", "\xAD\xB0",
        "\xAC\xA1", "\xAC\xA2", "\xAC\xA3", "\xAC\xA4", "\xAC\xA5",
        "\xAC\xA6", "\xAC\xA7", "\xAC\xA8", "\xAC\xA9", "\xAC\xAA",
      "\xAC\xAB", "\xAC\xAC", "\xA8\xCB", "."}
};

static const char *trans_ext85[] = {
  /*   1- 10 */
  "\xAE\xA3", "\xC4\xE2", "\xAE\xA9", "\xAE\xAA", "\x8F\xA1\xCE",
  "\xAE\xB9", "\x8F\xA1\xEC", "\xAE\xCD", "\x8F\xA3\xB0", "\x8F\xA3\xC8",
  /*  11- 20 */
  "\xAE\xEC", "\xAE\xEF", "\x8F\xA3\xD5", "\xFC\xA8", "\xB5\xC8",
  "\xAE\xF7", "\xAE\xFD", "\xAE\xF8", "\xAF\xA1", "\xAF\xA4",
  /*  21- 30 */
  "\x8F\xA2\xA5", "\xAF\xB9", "\xA2\xA2", "\xAF\xC5", "\xAF\xC6",
  "\xAF\xD7", "\xBA\xD4", "\x8F\xA5\xD2", "\xAF\xF2", "\x8F\xA5\xDD",
  /*  31- 40 */
  "\xCF\xDA", "\xCF\xF2", "\xCF\xEF", "\x8F\xAC\xA5", "\xF4\xB6",
  "\xF4\xBA", "\xF4\xC5", "\x8F\xAC\xC7", "\xB7\xC3", "\xF4\xDA",
  /*  41- 40 */
  "\xF5\xB2", "\xF5\xC8", "\xBD\xEC", "\xF5\xCC", "\xF5\xB7",
  "\xDB\xD9", "\xCE\xC2", "\xD0\xEC", "\x8F\xAF\xAB", "\xF6\xAC",
  /*  51- 50 */
  "\x8F\xAF\xDE", "\xB6\xFB", "\xDB\xB9", "\xC8\xB2", "\xF5\xF2",
  "\x8F\xEE\xAD", "\xF6\xDB", "\xF6\xE3", "\xF6\xE9", "\xF6\xF0",
  /*  61- 70 */
  "\xF1\xB2", "\xC0\xB6", "\xF6\xF7", "\xF7\xAB", "\xF7\xB9",
  "\xF7\xC3", "\xDF\xE6", "\xDF\xE6", "\xF7\xD3", "\xF7\xDE",
  /*  71- 80 */
  "\xF7\xE2", "\xF7\xF4", "\x8F\xF0\xE0", "\xF7\xF9", "\xF7\xFB",
  "\xF8\xA4", "\xBD\xCA", "\xF8\xA5", "\xF8\xA6", "\xF8\xA8",
  /*  81- 90 */
  "\xF8\xAA", "\x8F\xF0\xF0", "\xF8\xB1", "\xBB\xB9", "\xBD\xF1",
  "\x8F\xF1\xC3", "\x8F\xF2\xA9", "\x8F\xF2\xB9", "\x8F\xF2\xC0",
  "\x8F\xF2\xC4",
  /*  91-100 */
  "\xB5\xC0", "\xE3\xB9", "\xE3\xB1", "\xEA\xD6", "\xEB\xA1",
  "\xEA\xDB", "\x8F\xF2\xFC", "\x8F\xF3\xC9", "\xF9\xE8", "\xF9\xED",
  /* 101-110 */
  "\xFA\xA7", "\xC1\xA2", "\xFA\xCE", "\xD0\xE6", "\xB4\xDC",
  "\xFA\xE3", "\xB3\xEB", "\x8F\xF6\xD5", "\xCB\xA9", "\xFB\xB8",
  /* 111-120 */
  "\xFB\xC2", "\xBF\xAA", "\xFB\xE2", "\x8F\xF7\xFC", "\xFB\xED",
  "\xB3\xD1", "\xFC\xAD", "\xFC\xC1", "\xC4\xD4", "\xD0\xD2",
  /* 121-130 */
  "\xFC\xE6", "\xFC\xF0", "\xC5\xA2", "\xEE\xD6", "\x8F\xFA\xD8",
  "\xFD\xAE", "\xFD\xB7", "\xFD\xB9", "\xB4\xD6", "\xFD\xE2",
  /* 131-137 */
  "\xF1\xAD", "\x8F\xFC\xE4", "\xB9\xE2", "\xBB\xAA", "\xFE\xE5",
  "\xFE\xEF", "\xFE\xF0"
};

static void
g_string_append_c (gstr *euc_str, unsigned char c)
{
  if (euc_str->used < euc_str->len)
    euc_str->buf[euc_str->used++] = c;
}

static void
g_string_append (gstr *euc_str, const char *txt)
{
  while (*txt != '\0' && euc_str->used < euc_str->len)
    euc_str->buf[euc_str->used++] = *txt++;
}

static const char UNDEF_CHAR = '.';

static void
append_arib_char (gstr *euc_str, const struct code_state *state,
    unsigned char c1, unsigned char c2)
{
  int gidx;

  if (state->ss > 1)
    gidx = state->ss;
  else if (c1 & 0x80)
    gidx = state->gr;
  else
    gidx = state->gl;

  switch (state->g[gidx].code) {
    case CODE_ASCII:
      g_string_append_c (euc_str, c1 & 0x7F);
      break;
    case CODE_JISX0208:
    case CODE_JISX0213_1:
      g_string_append_c (euc_str, c1 | 0x80);
      g_string_append_c (euc_str, c2 | 0x80);
      break;
    case CODE_JISX0201_KATA:
      g_string_append_c (euc_str, '\x8E');      /* SS2 */
      g_string_append_c (euc_str, c1 | 0x80);
      break;
    case CODE_JISX0213_2:
      g_string_append_c (euc_str, '\x8F');      /* SS3 */
      g_string_append_c (euc_str, c1 | 0x80);
      g_string_append_c (euc_str, c2 | 0x80);
      break;
    case CODE_X_HIRA:
    case CODE_X_HIRA_P:
      /* map to Row:4,Cell:c1 in JISX0208/0213 */
      if ((c1 & 0x7F) >= 0x77) {
        g_string_append_c (euc_str, '\xA1');
        g_string_append_c (euc_str, trans_hira[(c1 & 0x7F) - 0x77]);
      } else {
        g_string_append_c (euc_str, '\xA4');
        g_string_append_c (euc_str, c1 | 0x80);
      }
      break;
    case CODE_X_KATA:
    case CODE_X_KATA_P:
      /* map to Row:5,Cell:c1 in JISX0208/0213 */
      if ((c1 & 0x7F) >= 0x77) {
        g_string_append_c (euc_str, '\xA1');
        g_string_append_c (euc_str, trans_kata[(c1 & 0x7F) - 0x77]);
      } else {
        g_string_append_c (euc_str, '\xA5');
        g_string_append_c (euc_str, c1 | 0x80);
      }
      break;
    case CODE_EXT:
      c1 &= 0x7F;
      c2 &= 0x7F;
      switch (c1) {
        case 0x7A:
          if (c2 < 0x4D) {
            g_string_append_c (euc_str, UNDEF_CHAR);
          } else {
            c2 -= 0x4D;
            g_string_append (euc_str, trans_ext90[c2]);
          }
          break;
        case 0x7C:
        case 0x7D:
        case 0x7E:
          c1 -= 0x7C;
          c2 -= 0x21;
          g_string_append (euc_str, trans_ext92[c1][c2]);
          break;
        case 0x75:
        case 0x76:
          c2 -= 0x21;
          if (c1 == 0x76)
            c2 += 94;
          if (c2 < 137)
            g_string_append (euc_str, trans_ext85[c2]);
          else
            g_string_append_c (euc_str, UNDEF_CHAR);
          break;
        default:
          g_string_append_c (euc_str, UNDEF_CHAR);
      }
      break;
    default:                   /* unsupported char-set. */
      g_string_append_c (euc_str, UNDEF_CHAR);
  }

  return;
};


void
aribstr_to_utf8 (char *source, size_t len, char *dest, size_t buf_len)
{
  int i, idx = 0;
  unsigned char prev = '\0';
  gstr euc_str;
  iconv_t cd;
  char *p;

  enum
  {
    NORMAL,
    ESCAPE,
    DESIGNATE_1B,
    DESIGNATE_MB,
    MB_2ND
  } mode;

  struct code_state state;
 //TR-14(2) 4 4.3
  const static struct code_state state_def = {
    .gl = 0,
    .gr = 2,
    .ss = 0,
    .g = {
          {.mb = 2,.code = CODE_JISX0213_1},
          {.mb = 1,.code = CODE_ASCII},
          {.mb = 1,.code = CODE_X_HIRA},
          {.mb = 1,.code = CODE_X_KATA},
        }
  };

  euc_str.buf = NULL;
  euc_str.used = 0;

  if (source == NULL)
    goto bailout;

  if (*source == '\0' || len == 0)
    goto bailout;

  mode = NORMAL;
  state = state_def;

  /* first, rewrite to EUC-JP/JISX0213 */
  euc_str.len = len * 3;
  euc_str.buf = malloc (euc_str.len);
  if (euc_str.buf == NULL)
    goto bailout;

  for (i = 0; i < len; i++) {
    switch (source[i]) {
      case 0x19:               /* SS2 */
        state.ss = 2;
        mode = NORMAL;
        continue;
        break;
      case 0x1D:               /* SS3 */
        state.ss = 3;
        mode = NORMAL;
        continue;
        break;
      case 0x1B:               /* ESC */
        mode = ESCAPE;
        continue;
        break;
      case 0x0F:               /* LS0 */
        mode = NORMAL;
        state.ss = 0;
        state.gl = 0;
        continue;
        break;
      case 0x0E:               /* LS1 */
        mode = NORMAL;
        state.ss = 0;
        state.gl = 1;
        continue;
        break;
      case 0x07:               /* BELL */
      case 0x08:               /* BS */
      case 0x09:               /* HT */
      case 0x0A:               /* LF ?? */
      case 0x0D:               /* CR */
      case 0x7F:               /* DEL */
      case '\xA0':
      case '\xFF':
        state = state_def;     /* fall through */
      case 0x20:               /* SPACE */
        mode = NORMAL;
        g_string_append_c (&euc_str, source[i] & 0x7F);
        continue;
        break;
    }
    if (!(source[i] & 0x60)) {  /* C0 or C1 */
      /* skip */
      continue;
    }

    switch (mode) {
      case ESCAPE:
        switch (source[i]) {
          case '\x28':
          case '\x29':
          case '\x2A':
          case '\x2B':
            idx = source[i] - '\x28';
            mode = DESIGNATE_1B;
            continue;
            break;
          case '\x24':
            idx = 0;
            mode = DESIGNATE_MB;
            continue;
            break;
          case '\x6E':         /* LS2 */
            mode = NORMAL;
            state.ss = 0;
            state.gl = 2;
            continue;
            break;
          case '\x6F':         /* LS3 */
            mode = NORMAL;
            state.ss = 0;
            state.gl = 3;
            continue;
            break;
          case '\x7E':         /* LS1R */
            mode = NORMAL;
            state.ss = 0;
            state.gr = 1;
            continue;
            break;
          case '\x7D':         /* LS2R */
            mode = NORMAL;
            state.ss = 0;
            state.gr = 2;
            continue;
            break;
          case '\x7C':         /* LS3R */
            mode = NORMAL;
            state.ss = 0;
            state.gr = 3;
            continue;
            break;
          default:
            /* skip unknown ESC sequences */
            mode = NORMAL;
            continue;
        }
        break;
      case DESIGNATE_1B:
        mode = NORMAL;
        state.ss = 0;
        state.g[idx].mb = 1;
        if (source[i] == '\x4a' || source[i] == '\x36')
          state.g[idx].code = CODE_ASCII;
        else
          state.g[idx].code = source[i];
        continue;
        break;
      case DESIGNATE_MB:
        switch (source[i]) {
          case '\x28':         /* DRCS only */
          case '\x29':
          case '\x2A':
          case '\x2B':
            idx = source[i] - '\x28';
            continue;
            break;
          case '\x39':         /* JISX0213, plane 1 */
            state.g[idx].code = CODE_JISX0213_1;
            break;
          case '\x3A':         /* JISX0213, plane 2 */
            state.g[idx].code = CODE_JISX0213_2;
            break;
          case '\x40':         /* JISX0208_1978 */
            state.g[idx].code = CODE_JISX0208;
            break;
          default:
            state.g[idx].code = source[i];
        }
        mode = NORMAL;
        state.ss = 0;
        state.g[idx].mb = 2;
        continue;
        break;
      case MB_2ND:
        /* convert prev, source[i] / state  */
        append_arib_char (&euc_str, &state, prev, source[i]);
        mode = NORMAL;
        continue;
        break;
      case NORMAL:
        if ((state.ss > 0 && state.g[state.ss].mb > 1)
            || (!state.ss && (source[i] & 0x80) && state.g[state.gr].mb > 1)
            || (!state.ss && !(source[i] & 0x80) && state.g[state.gl].mb > 1)) {
          prev = source[i];
          mode = MB_2ND;
          state.ss = 0;
          continue;
        }
        /* convert source[i] / state  */
        append_arib_char (&euc_str, &state, source[i], '\0');
        state.ss = 0;
        break;
    }
  }

  g_string_append_c (&euc_str, '\0');

  cd = iconv_open("UTF-8//TRANSLIT", "EUC-JISX0213");
  if (cd < 0)
    goto bailout;

  p = euc_str.buf; // to keep euc_str.buf unmodified for later free()
  iconv(cd, &p, &euc_str.used, &dest, &buf_len);
  iconv_close(cd);
  if (buf_len > 0)
    *dest = '\0';
  else
    *(dest - 1) = '\0'; // rewrite the tail byte

  free(euc_str.buf);
  return;

bailout:
  free(euc_str.buf);
  if (dest && buf_len > 0)
    *dest = '\0';
  return;
}
