#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>
#include <wchar.h>
#include <locale.h>
#include "replxx.h"
#include "sqlite3.h"
#include "bqnffi.h"

Replxx* replxx;
typedef   int8_t i8; typedef  uint8_t u8; typedef  int16_t i16; typedef uint16_t u16; typedef  int32_t i32; typedef uint32_t u32; typedef  int64_t i64; typedef uint64_t u64; typedef double   f64; typedef float    f32; typedef size_t ux;
bool inBackslash=false;


#define IS_CONT(x) (((x) & 0xc0) == 0x80)

int utf8str_codepoint_len( char const* s, int utf8len ) {
	int codepointLen = 0;
	unsigned char m4 = 128 + 64 + 32 + 16;
	unsigned char m3 = 128 + 64 + 32;
	unsigned char m2 = 128 + 64;
	for ( int i = 0; i < utf8len; ++ i, ++ codepointLen ) {
		char c = s[i];
		if ( ( c & m4 ) == m4 ) {
			i += 3;
		} else if ( ( c & m3 ) == m3 ) {
			i += 2;
		} else if ( ( c & m2 ) == m2 ) {
			i += 1;
		}
	}
	return ( codepointLen );
}

int context_len( char const* prefix ) {
	char const wb[] = " \t\n\r\v\f-=+*&^%$#@!,./?<>;:`~'\"[]{}()\\|";
	int i = (int)strlen( prefix ) - 1;
	int cl = 0;
	while ( i >= 0 ) {
		if ( strchr( wb, prefix[i] ) != NULL ) {
			break;
		}
		++ cl;
		-- i;
	}
	return ( cl );
}

/*
 * convert cursor position from utf8 character space to char space eg. πabc -> π_abc since π is 2 chars long
 */
i32 utf8str_pos(char const* s, i32 utf8pos) {
    i32 i=0;
	unsigned char m4 = 128 + 64 + 32 + 16;
	unsigned char m3 = 128 + 64 + 32;
	unsigned char m2 = 128 + 64;
	for (i32 j=0; j<utf8pos; j++, i++) {
		char c = s[i];
		if ((c & m4) == m4 ) { i += 3; } 
        else if ((c & m3) == m3) { i += 2; } 
        else if ((c & m2) == m2) { i += 1; }
	}
	return i;
}

i32 decode_code_point(const char **s) {
    i32 k = **s ? __builtin_clz(~(**s << 24)) : 0; // Count # of leading 1 bits.
    i32 mask = (1 << (8 - k)) - 1;                 // All 1s with k leading 0s.
    i32 value = **s & mask;
    // k = 0 for one-byte code points; otherwise, k = #total bytes.
    for (++(*s), --k; k > 0 && IS_CONT(**s); --k, ++(*s)) {
        value <<= 6;
        value += (**s & 0x3F);
    }
    return value;
}

void encode_code_point(char **s, char *end, int code) {
    char val[4];
    int lead_byte_max = 0x7F;
    int val_index = 0;
    while (code > lead_byte_max) {
        val[val_index++] = (code & 0x3F) | 0x80;
        code >>= 6;
        lead_byte_max >>= (val_index == 1 ? 2 : 1);
    }
    val[val_index++] = (code & lead_byte_max) | (~lead_byte_max << 1);
    while (val_index-- && *s < end) {
        **s = val[val_index];
        (*s)++;
    }
}

/*
 * Removes current char and replaces with utf8 string
 */
void modify_unicode(char *utf8, i32 m, char **line, i32 pos) {
    char* s = *line;
    i32 n = strlen(s), j=0;
    char *new_s = *line = calloc(n+m+1, 1);

    for (i32 i=0; i<n; i++) { 
        if (i!=(pos-1)) { new_s[j++] = s[i]; } 
        else { for (i32 k=0; k<m; k++) { new_s[j++] = utf8[k]; } }
    }
    free(s);
    inBackslash=false;
}

void modify_callback(char** line, i32* cursor_pos, void* ud) {
    char* s = *line;
    i32 pos=utf8str_pos(s, *cursor_pos);
    char p = *(s+(pos-1));

    /* 
     * copy string without \ character 
     */
    if (p=='\\') {
        i32 n = strlen(s), j=0;
        char *new_s = *line = calloc(n-1, 1 );
        for (i32 i=0; i<n; i++) { if (i!=(pos-1)) { new_s[j++] = s[i]; } }
        *cursor_pos-=1;
        inBackslash=true;
    }

    if (inBackslash) {
        switch (p) {
            case 'p': { char utf8[]={0xCF,0x80     }; modify_unicode(utf8, 2, line, pos); break; }
            case '[': { char utf8[]={0xE2,0x86,0x90}; modify_unicode(utf8, 3, line, pos); break; }
        }
    }
}

void completion_callback(char const* context, replxx_completions* lc, int* contextLen, void* ud) {
	char** examples = (char**)( ud );
	size_t i;

	int utf8ContextLen = context_len( context );
	int prefixLen = (int)strlen( context ) - utf8ContextLen;
	*contextLen = utf8str_codepoint_len( context + prefixLen, utf8ContextLen );
	for (i = 0;	examples[i] != NULL; ++i) {
		if (strncmp(context + prefixLen, examples[i], utf8ContextLen) == 0) {
			replxx_add_completion(lc, examples[i]);
		}
	}
}

void colorHook( char const* str_, ReplxxColor* colors_, int size_, void* ud ) {
	int i = 0;
	for ( ; i < size_; ++ i ) {
		if ( isdigit( str_[i] ) ) {
			colors_[i] = REPLXX_COLOR_BRIGHTMAGENTA;
		}
	}
	if ( ( size_ > 0 ) && ( str_[size_ - 1] == '(' ) ) {
		replxx_emulate_key_press( ud, ')' );
		replxx_emulate_key_press( ud, REPLXX_KEY_LEFT );
	}
}

void hintHook(char const* context, replxx_hints* lc, int* contextLen, ReplxxColor* c, void* ud) {
	char** examples = (char**)( ud );
	int i;
	int utf8ContextLen = context_len( context );
	int prefixLen = (int)strlen( context ) - utf8ContextLen;
	*contextLen = utf8str_codepoint_len( context + prefixLen, utf8ContextLen );
	if ( *contextLen > 0 ) {
		for (i = 0;	examples[i] != NULL; ++i) {
			if (strncmp(context + prefixLen, examples[i], utf8ContextLen) == 0) {
				replxx_add_hint(lc, examples[i]);
			}
		}
	}
}

void init() {
#define MAX_EXAMPLE_COUNT 128
    char* examples[MAX_EXAMPLE_COUNT + 1] = {
        "db", "hello", "hallo", "hans", "hansekogge", "seamann", "quetzalcoatl", "quit", "power", NULL
    };
	replxx = replxx_init();
    replxx_install_window_change_handler(replxx);

	const char* file = "./replxx_history.txt";
	replxx_history_load( replxx, file );
    replxx_set_indent_multiline( replxx, 0 );
    replxx_set_modify_callback(replxx, modify_callback, NULL);

	/*replxx_set_completion_callback(replxx, completion_callback, examples );*/
    /*replxx_set_highlighter_callback( replxx, colorHook, replxx );*/
	/*replxx_set_hint_callback( replxx, hintHook, examples );*/
}

const BQNV input() {
    u32 buffer[1024] = {0};
    u32 *code_points = buffer;
    const char* utf8_bytes=replxx_input(replxx, "   ");
	/*char const* result = NULL;*/
    do {
        utf8_bytes = replxx_input( replxx, "    " );
    } while ( ( utf8_bytes == NULL ) && ( errno == EAGAIN ) );

    printf("%s\n",utf8_bytes);
    if (utf8_bytes==NULL) { return bqn_makeF64(0); }

    ux n=strlen(utf8_bytes);
    while (( *code_points++ = decode_code_point(&utf8_bytes) ));

    return bqn_makeC32Vec(n, buffer);
}

/*
 * SQLite
 */
sqlite3 *db;

u32 sqlite_init(const char* name) {
    u32 err=sqlite3_open(name, &db);
    if (err) {
        fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return 1;
    }
    return 0;
}

void sqlite_close() { sqlite3_close(db); }

#define SQLITE_BUFFER_LEN 5000

void sqlite_exec(i32* query) {
    i32 *code_point_cursor = query;  // Copy of the base pointer so we can move this one around.
    char utf8_bytes[SQLITE_BUFFER_LEN];
    char* start_byte=utf8_bytes;
    char *end_byte = utf8_bytes + SQLITE_BUFFER_LEN;

    do {
        encode_code_point(&start_byte, end_byte, *code_point_cursor++);
    } while (*(code_point_cursor - 1));
    *code_point_cursor='\0';

    /*printf("UTF-8: %s\n", utf8_bytes);*/

    char *zErrMsg = 0;
    i32 res = sqlite3_exec(db, utf8_bytes, NULL, NULL, &zErrMsg);
    if (res != SQLITE_OK){
        fprintf(stderr, "SQL error: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
    }
}

u64 sqlite_last_rowid() {
    return sqlite3_last_insert_rowid(db);
}


i32 main(i32 argc, char* argv[]) {
    init();
    /*while (true) { */
        input();
    /*}*/
	replxx_end( replxx );
    return 0;
}
