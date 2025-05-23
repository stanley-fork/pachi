#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "stone.h"

char *
stone2str(enum stone s)
{
	switch (s) {
		case S_BLACK: return "black";
		case S_WHITE: return "white";
		default: return "none";
	}
}

enum stone
str2stone(char *str)
{
	switch (tolower(*str)) {
		case 'b': return S_BLACK;
		case 'w': return S_WHITE;
		default:  assert(0);
	}
}

bool
valid_color(char *str)
{
	char c = tolower(*str);
	return (c == 'b' || c == 'w');
}
