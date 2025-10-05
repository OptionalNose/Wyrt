#include "types.h"

#include <assert.h>
#include <stdarg.h>

void wyrt_diag(FILE *file, char *const *idents, char *const *strings, const TypeContext *tc, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	fmt--;
	while(*++fmt) {
		switch(*fmt) {
		default: fputc(*fmt, file); break;
		case '%':
			switch(*++fmt) {
			default: assert(0);
			case '%': fputc('%', file); break;
			case 'l': lexer_print_debug_to_file(file, va_arg(args, DebugInfo*)); break; 
			case 's': fputs(strings[va_arg(args, size_t)], file); break;
			case 'i': fputs(idents[va_arg(args, size_t)], file); break;
			case 't': type_print(file, tc, va_arg(args, Type), idents); break;
			case 'z': fprintf(file, "%zi", va_arg(args, size_t)); break;
			}
			break;
		}
	}
	va_end(args);
}
