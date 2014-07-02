#ifndef __MAMEDEF_H__
#define __MAMEDEF_H__

#include "../stdtype.h"

/* offsets and addresses are 32-bit (for now...) */
typedef UINT32	offs_t;

/* stream_sample_t is used to represent a single sample in a sound stream */
typedef INT32	stream_sample_t;

#ifndef M_PI
#define M_PI	3.14159265358979323846
#endif

#ifdef _DEBUG
#define logerror	printf
#else
#define logerror
#endif

extern stream_sample_t* DUMMYBUF[];

#endif	// __MAMEDEF_H__
