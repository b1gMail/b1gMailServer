#ifndef _RESDATA_H_
#define _RESDATA_H_

#include <string.h>

extern const char *_resData[], *_resNames[];
extern const int _resLengths[];

inline const char *getResource(const char *name, int *length = NULL)
{
    for(int i=0; _resNames[i] != NULL; i++)
    {
        if(strcasecmp(_resNames[i], name) == 0)
        {
            if(length != NULL)
                *length = _resLengths[i];
            return(_resData[i]);
        }
    }

    if(length != NULL)
        *length = 0;
    return(NULL);
}

#endif
