#include "SegaG80crypt.h"

 void(*sega_decrypt)(int, unsigned int*);


/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0062                      */
/****************************************************************************/
static void sega_decrypt62(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x03)
	{
	case 0x00:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x01:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x02:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x03:
		/* A */
		i = b;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0063                      */
/****************************************************************************/
static void sega_decrypt63(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x01:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x08:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* A */
		i = b;
		break;
	}

	*lo = i;
}


static void sega_decrypt64(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x03)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x02:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x03:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}
/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0070                      */
/****************************************************************************/
static void sega_decrypt70(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x01:
		/* A */
		i = b;
		break;
	case 0x08:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0076                      */
/****************************************************************************/
static void sega_decrypt76(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x09)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x08:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x09:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971025 - Emulate Sega G80 security chip 315-0082                      */
/****************************************************************************/
static void sega_decrypt82(int pc, unsigned int* lo)
{
	unsigned int i = 0;
	unsigned int b = *lo;

	switch (pc & 0x11)
	{
	case 0x00:
		/* A */
		i = b;
		break;
	case 0x01:
		/* B */
		i = b & 0x03;
		i += ((b & 0x80) >> 1);
		i += ((b & 0x60) >> 3);
		i += ((~b) & 0x10);
		i += ((b & 0x08) << 2);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x10:
		/* C */
		i = b & 0x03;
		i += ((b & 0x80) >> 4);
		i += (((~b) & 0x40) >> 1);
		i += ((b & 0x20) >> 1);
		i += ((b & 0x10) >> 2);
		i += ((b & 0x08) << 3);
		i += ((b & 0x04) << 5);
		i &= 0xFF;
		break;
	case 0x11:
		/* D */
		i = b & 0x23;
		i += ((b & 0xC0) >> 4);
		i += ((b & 0x10) << 2);
		i += ((b & 0x08) << 1);
		i += (((~b) & 0x04) << 5);
		i &= 0xFF;
		break;
	}

	*lo = i;
}

/****************************************************************************/
/* MB 971031 - Emulate no Sega G80 security chip                            */
/****************************************************************************/
static void sega_decrypt0(int pc, unsigned int* lo)
{
	return;
}


void sega_security(int chip)
{

	switch (chip)
	{
	case 62:
		sega_decrypt = sega_decrypt62;
		break;
	case 63:
		sega_decrypt = sega_decrypt63;
		break;
	case 64:
		sega_decrypt = sega_decrypt64;
		break;
	case 70:
		sega_decrypt = sega_decrypt70;
		break;
	case 76:
		sega_decrypt = sega_decrypt76;
		break;
	case 82:
		sega_decrypt = sega_decrypt82;
		break;
	default:
		sega_decrypt = sega_decrypt64;
		break;
	}
}
