#pragma once

#include <stdio.h>
#include "types.h"

/*
 * Custom formatted printing
 * %t = Type
 * %l = Location (DebugInfo)
 * %i = Identiifer
 * %s = String
 * %z = size_t
 *
 * If a parameter would not be used (i.e. idents when there are no %i placeholders) they can be NULL
*/
void wyrt_diag(FILE *file, char *const *idents, char *const *strings, const TypeContext *tc, const char *fmt, ...);
