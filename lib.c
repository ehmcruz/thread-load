#include <stdlib.h>

#include "lib.h"

char* libtload_env_get_str(char *envname)
{
	char *p;
	
	p = getenv(envname);
	if (!p || *p == 0) {
		return NULL;
	}
	
	return p;
}

char* libtload_strtok (char *str, char *tok, char del, uint32_t bsize)
{
	char *p;
	uint32_t i;

	for (p=str, i=0; *p && *p != del; p++, i++) {
		ASSERT(i < (bsize-1))
		*tok = *p;
		tok++;
	}

	*tok = 0;
	
	if (*p)
		return p + 1;
	else if (p != str)
		return p;
	else
		return NULL;
}
