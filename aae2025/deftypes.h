#ifdef __cplusplus
extern "C" {
#endif
	/******************************************************************************/
	/*																			  */
	/*							   RAINE DATATYPES								  */
	/*																			  */
	/******************************************************************************/

#ifndef __DEFTYPES_H__
#define __DEFTYPES_H__

#define DEF_INLINE		__inline

#if _MSC_VER > 1000
#pragma warning( disable : 4005 4057 4127 4131 4142 4146 4152 4232 4245 4706 4710 4711 4761 )
// C4005 - 'identifier' : macro redefinition
// C4057 - 'operator' : 'identifier1' indirection to slightly different base types from 'identifier2'
// C4127 - conditional expression is constant
// C4131 - 'function' : uses old-style declarator
//		   --- This one is needed since the unzip (zlib) declares using ANSI standard (old style).
// C4142 - benign redefinition of type
// C4146 - unary minus operator applied to unsigned type, result still unsigned
// C4152 - non standard extension, function/data ptr conversion in expression
//		   --- This one is needed for almost all of the files, when MSVC is put on warning level 4,
//			   It complains about almost every hard coded string in the source complaining about typecast
//			   required etc.  It also complains on function pointer assignments that are not typecasted correctly.
// C4232 - nonstandard extension used : 'identifier' : address of dllimport 'dllimport' is not static, identity not guaranteed
// C4245 - 'conversion' : conversion from 'type1' to 'type2', signed/unsigned mismatch
// C4706 - assignment within conditional expression
//		   --- For cleanliness these should actually be split out into multiple lines, also makes
//			   debugging these lines a lot easier.	Most occurances of this were testing memory allocation.
// C4710 - function 'function' not inlined
// C4711 - function 'function' selected for inline expansion
// C4761 - integral size mismatch in argument : conversion supplied
//		   --- This one occurs for a few of the libraries that are used due to parameter type mis-match.
//			   The compiler does however straigten these out, but the warning is issued so it can be resolved
//			   correctly and no compiler stupidity can break the program.
#endif // _MSC_VER > 1000

	typedef unsigned char UINT8;   //  8-bit unsigned
	typedef unsigned short UINT16; // 16-bit unsigned
	typedef unsigned int UINT32;   // 32-bit unsigned
	//typedef __int64 UINT64; 	   // 64-bit unsigned

	typedef signed char INT8;	   //  8-bit signed
	typedef signed short INT16;    // 16-bit signed
	typedef signed int INT32;	   // 32-bit signed
	//typedef __int64 INT64;		   // 64-bit signed

//	typedef unsigned int	CPTR;	  // 32-bit pointer

	typedef int BOOL;



//// NOTE TO SELF, I think these should be moved to the cpu_fw.h file in cpu_code 
#ifndef _MEMORYREADWRITEBYTE_
#define _MEMORYREADWRITEBYTE_

	struct MemoryWriteByte
	{
		UINT32 lowAddr;
		UINT32 highAddr;
		void (*memoryCall)(UINT32, UINT8, struct MemoryWriteByte*);
		void* pUserArea;
	};

	struct MemoryWriteWord
	{
		UINT32 lowAddr;
		UINT32 highAddr;
		void (*memoryCall)(UINT32, UINT16, struct MemoryWriteWord*);
		void* pUserArea;
	};

	struct MemoryReadByte
	{
		UINT32 lowAddr;
		UINT32 highAddr;
		UINT8(*memoryCall)(UINT32, struct MemoryReadByte*);
		void* pUserArea;
	};

	struct MemoryReadWord
	{
		UINT32 lowAddr;
		UINT32 highAddr;
		UINT16(*memoryCall)(UINT32, struct MemoryReadWord*);
		void* pUserArea;
	};

	struct z80PortWrite
	{
		UINT16 lowIoAddr;
		UINT16 highIoAddr;
		void(*IOCall)(UINT16, UINT8, struct z80PortWrite*);
		void* pUserArea;
	};

	struct z80PortRead
	{
		UINT16 lowIoAddr;
		UINT16 highIoAddr;
		UINT16(*IOCall)(UINT16, struct z80PortRead*);
		void* pUserArea;
	};

#endif // _MEMORYREADWRITEBYTE_

#ifndef TRUE
#define TRUE	1
#define FALSE	0
#endif

#ifndef MIN
#define MIN(x,y)	 (((x) < (y)) ? (x) : (y))
#define MAX(x,y)	 (((x) > (y)) ? (x) : (y))
#define MID(x,y,z)	 MAX((x), MIN((y), (z)))
#endif

#define bitget(p,m) ((p) & (m))
#define bitset(p,m) ((p) |= (m))
#define bitclr(p,m) ((p) &= ~(m))
#define bitflp(p,m) ((p) ^= (m))
#define bit_write(c,p,m) (c ? bit_set(p,m) : bit_clear(p,m))
	//#define BIT(x) (0x01 << (x))
	//#define LONGBIT(x) ((unsigned long)0x00000001 << (x))

#endif

#ifdef __cplusplus
}
#endif
