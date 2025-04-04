//
// SHA-1.h: interface for the CSHA1 class.
//
// Created by Mark Muir 2005/1/5
// Copyright 2005 Mark Muir. All rights reserved.
//
// Concrete sub-class of CHash; object to perform hashing based on the National
// Standards Agency (NSA) SHA-1 (Secure Hash Algorithm 1) algorithm. The 160-bit
// SHA-1 hash (digest) for files or arbitrary text strings is returned as a
// NULL-terminated string containing the hexadecimal represtentation. This
// implementation conforms to FIPS-180-1.

#ifndef __SHA_1_H__INCLUDED__
#define __SHA_1_H__INCLUDED__

#include <stdio.h>
#include <cstdint>
#include <string.h>

#pragma warning(disable: 4996)

class CSHA1
{
protected:
	uint32_t m_ulState[5];		// The state values that are updated to form the digest
	uint32_t m_ulTotal[2];		// Count of the number of bits of input data operated on
	uint8_t  m_ucBufferSHA[64];	// Buffer for the remainder of data passed to Update()
								// N.B. Only complete blocks of 64 bytes can be transformed
	char m_szDigest[41];		// The hex digest/hash string

protected:
	void Start();
	void Transform(uint8_t data[64]);
	void Update(uint8_t* input, uint32_t length);
	void Finish();

public:
	const char* CalculateHash(unsigned char* szText, int len);

};


//---------------------------------------
// SHA-1 Algorithm Transformation Macros
//---------------------------------------

// Get_uint32_t()
// Constructs a 32-bit big-endian value from the array of 8-bit values
// given by b, starting at index i. The result is stored in n.
#define Get_uint32(n, b, i)					\
{											\
	(n) = ((uint32_t)(b)[(i)    ] << 24)		\
		| ((uint32_t)(b)[(i) + 1] << 16)		\
		| ((uint32_t)(b)[(i) + 2] <<  8)		\
		| ((uint32_t)(b)[(i) + 3]      );		\
}

// Put_uint32_t()
// Decodes the 32-bit big-endian value given as n into the array of
// 8-bit values given by b, starting at index i.
#define Put_uint32(n, b, i)					\
{											\
	(b)[(i)    ] = (uint8_t)((n) >> 24);		\
	(b)[(i) + 1] = (uint8_t)((n) >> 16);		\
	(b)[(i) + 2] = (uint8_t)((n) >>  8);		\
	(b)[(i) + 3] = (uint8_t)((n)      );		\
}

// RotateLeft()
// Rotates the 32-bit unsigned integer given by x left n bits.
#define RotateLeft(x, n)	((x << n) | (x >> (32 - n)))

// R()
// Used to expand (partially in-place) the 16 32-bit words in array
// x[] into 80 32-bit words. The index of the word to be generated
// is given by the position parameter t. The 32-bit unsigned integer
// temporary variable temp must exist at the place of call.
#define R(t)									\
(												\
	temp = x[(t- 3) & 0x0f] ^ x[(t-8) & 0x0f]	\
		 ^ x[(t-14) & 0x0f] ^ x[t	  & 0x0f],	\
	(x[t & 0x0f] = RotateLeft(temp, 1))			\
)

// Basic transform functions - one for each of the four rounds
#define F1(x, y, z)		(z ^ (x & (y ^ z)))
#define F2(x, y, z)		(x ^ y ^ z)
#define F3(x, y, z)		((x & y) | (z & (x | y)))
#define F4(x, y, z)		(x ^ y ^ z)

// Constants used in the transformations - one for each round
#define K1				0x5a827999
#define K2				0x6ed9eba1
#define K3				0x8f1bbcdc
#define K4				0xca62c1d6

// P()
// SHA-1 transformation used in each of the four rounds, with
// a different transformation function specified each time.
#define P(a, b, c, d, e, x, F, K)									\
	e += RotateLeft(a,5) + F(b,c,d) + K + x; b = RotateLeft(b,30);


//---------------------------------
// Instance Method Implementations
//---------------------------------

// CalculateHash()
// Returns the text string containing the SHA-1 digest for the given text message.
const char* CSHA1::CalculateHash(unsigned char* szText, int len)
{
	// Create the digest for the given text string
	Start();
	Update((unsigned char*)szText, len);
	Finish();
	// Return the hex digest string
	return m_szDigest;
}


void CSHA1::Start()
{
	// Reset the count of the number of data bits transformed
	m_ulTotal[0] = 0;
	m_ulTotal[1] = 0;

	// Initialise the state variables
	m_ulState[0] = 0x67452301;
	m_ulState[1] = 0xefcdab89;
	m_ulState[2] = 0x98badcfe;
	m_ulState[3] = 0x10325476;
	m_ulState[4] = 0xc3d2e1f0;
}

// Transform()
// Transform the next (512-bit) block of data.
void CSHA1::Transform(uint8_t data[64])
{
	uint32_t temp, x[16], a, b, c, d, e;

	// Break the block into sixteen 32-bit big-endian words 
	Get_uint32(x[0], data, 0);
	Get_uint32(x[1], data, 4);
	Get_uint32(x[2], data, 8);
	Get_uint32(x[3], data, 12);
	Get_uint32(x[4], data, 16);
	Get_uint32(x[5], data, 20);
	Get_uint32(x[6], data, 24);
	Get_uint32(x[7], data, 28);
	Get_uint32(x[8], data, 32);
	Get_uint32(x[9], data, 36);
	Get_uint32(x[10], data, 40);
	Get_uint32(x[11], data, 44);
	Get_uint32(x[12], data, 48);
	Get_uint32(x[13], data, 52);
	Get_uint32(x[14], data, 56);
	Get_uint32(x[15], data, 60);

	// Initialise the hash value for this block
	a = m_ulState[0];
	b = m_ulState[1];
	c = m_ulState[2];
	d = m_ulState[3];
	e = m_ulState[4];

	// Round 1
	P(a, b, c, d, e, x[0], F1, K1);
	P(e, a, b, c, d, x[1], F1, K1);
	P(d, e, a, b, c, x[2], F1, K1);
	P(c, d, e, a, b, x[3], F1, K1);
	P(b, c, d, e, a, x[4], F1, K1);
	P(a, b, c, d, e, x[5], F1, K1);
	P(e, a, b, c, d, x[6], F1, K1);
	P(d, e, a, b, c, x[7], F1, K1);
	P(c, d, e, a, b, x[8], F1, K1);
	P(b, c, d, e, a, x[9], F1, K1);
	P(a, b, c, d, e, x[10], F1, K1);
	P(e, a, b, c, d, x[11], F1, K1);
	P(d, e, a, b, c, x[12], F1, K1);
	P(c, d, e, a, b, x[13], F1, K1);
	P(b, c, d, e, a, x[14], F1, K1);
	P(a, b, c, d, e, x[15], F1, K1);
	P(e, a, b, c, d, R(16), F1, K1);
	P(d, e, a, b, c, R(17), F1, K1);
	P(c, d, e, a, b, R(18), F1, K1);
	P(b, c, d, e, a, R(19), F1, K1);

	// Round 2
	P(a, b, c, d, e, R(20), F2, K2);
	P(e, a, b, c, d, R(21), F2, K2);
	P(d, e, a, b, c, R(22), F2, K2);
	P(c, d, e, a, b, R(23), F2, K2);
	P(b, c, d, e, a, R(24), F2, K2);
	P(a, b, c, d, e, R(25), F2, K2);
	P(e, a, b, c, d, R(26), F2, K2);
	P(d, e, a, b, c, R(27), F2, K2);
	P(c, d, e, a, b, R(28), F2, K2);
	P(b, c, d, e, a, R(29), F2, K2);
	P(a, b, c, d, e, R(30), F2, K2);
	P(e, a, b, c, d, R(31), F2, K2);
	P(d, e, a, b, c, R(32), F2, K2);
	P(c, d, e, a, b, R(33), F2, K2);
	P(b, c, d, e, a, R(34), F2, K2);
	P(a, b, c, d, e, R(35), F2, K2);
	P(e, a, b, c, d, R(36), F2, K2);
	P(d, e, a, b, c, R(37), F2, K2);
	P(c, d, e, a, b, R(38), F2, K2);
	P(b, c, d, e, a, R(39), F2, K2);

	// Round 3
	P(a, b, c, d, e, R(40), F3, K3);
	P(e, a, b, c, d, R(41), F3, K3);
	P(d, e, a, b, c, R(42), F3, K3);
	P(c, d, e, a, b, R(43), F3, K3);
	P(b, c, d, e, a, R(44), F3, K3);
	P(a, b, c, d, e, R(45), F3, K3);
	P(e, a, b, c, d, R(46), F3, K3);
	P(d, e, a, b, c, R(47), F3, K3);
	P(c, d, e, a, b, R(48), F3, K3);
	P(b, c, d, e, a, R(49), F3, K3);
	P(a, b, c, d, e, R(50), F3, K3);
	P(e, a, b, c, d, R(51), F3, K3);
	P(d, e, a, b, c, R(52), F3, K3);
	P(c, d, e, a, b, R(53), F3, K3);
	P(b, c, d, e, a, R(54), F3, K3);
	P(a, b, c, d, e, R(55), F3, K3);
	P(e, a, b, c, d, R(56), F3, K3);
	P(d, e, a, b, c, R(57), F3, K3);
	P(c, d, e, a, b, R(58), F3, K3);
	P(b, c, d, e, a, R(59), F3, K3);

	// Round 4
	P(a, b, c, d, e, R(60), F4, K4);
	P(e, a, b, c, d, R(61), F4, K4);
	P(d, e, a, b, c, R(62), F4, K4);
	P(c, d, e, a, b, R(63), F4, K4);
	P(b, c, d, e, a, R(64), F4, K4);
	P(a, b, c, d, e, R(65), F4, K4);
	P(e, a, b, c, d, R(66), F4, K4);
	P(d, e, a, b, c, R(67), F4, K4);
	P(c, d, e, a, b, R(68), F4, K4);
	P(b, c, d, e, a, R(69), F4, K4);
	P(a, b, c, d, e, R(70), F4, K4);
	P(e, a, b, c, d, R(71), F4, K4);
	P(d, e, a, b, c, R(72), F4, K4);
	P(c, d, e, a, b, R(73), F4, K4);
	P(b, c, d, e, a, R(74), F4, K4);
	P(a, b, c, d, e, R(75), F4, K4);
	P(e, a, b, c, d, R(76), F4, K4);
	P(d, e, a, b, c, R(77), F4, K4);
	P(c, d, e, a, b, R(78), F4, K4);
	P(b, c, d, e, a, R(79), F4, K4);

	// Add this block's hash value to the result so far
	m_ulState[0] += a;
	m_ulState[1] += b;
	m_ulState[2] += c;
	m_ulState[3] += d;
	m_ulState[4] += e;
}

// Update()
// Continues the digest operation with this next arbitrary length of data.
void CSHA1::Update(uint8_t* input, uint32_t length)
{
	uint32_t left, fill;

	if (!length) return;

	// Calculate the offset in the buffer for where the beginning of
	// the data should be written (number of bytes modulo 64).
	left = m_ulTotal[0] & 0x3f;
	// Calculate how much space will remain in the buffer
	fill = 64 - left;

	// Increment the count of the data bits processed
	m_ulTotal[0] += length;						// Lower 32-bits
	if (m_ulTotal[0] < length) m_ulTotal[1] ++;	// Upper 32-bits

	if (left && length >= fill)
	{
		// There is sufficient data to fill the buffer, so fill the rest
		// of the buffer and transform
		memcpy(m_ucBufferSHA + left, input, fill);
		Transform(m_ucBufferSHA);
		length -= fill;
		input += fill;
		// Buffer the remaining data
		left = 0;
	}

	// Now transform each remaining 64-byte (512-bit) block
	// of input data, bypassing the buffer
	while (length >= 64)
	{
		Transform(input);
		length -= 64;
		input += 64;
	}

	// Buffer any remaining data
	memcpy(m_ucBufferSHA + left, input, length);
}

// Finish()
// Terminates the hashing operation, arriving at the final hash value.
void CSHA1::Finish()
{
	static uint8_t padding[64] = { 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
									0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
									0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
									0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
	uint32_t last, padn;
	uint32_t high, low;
	uint8_t  msglen[8]{};

	// Save the number of bits (before padding)
	high = (m_ulTotal[0] >> 29)
		| (m_ulTotal[1] << 3);
	low = (m_ulTotal[0] << 3);
	Put_uint32(high, msglen, 0);
	Put_uint32(low, msglen, 4);

	// Pad so that the last 8 bytes align with the end of a block.
	// These will contain the count of the total number of bits.
	last = m_ulTotal[0] & 0x3f;
	padn = (last < 56) ? (56 - last) : (120 - last);
	Update(padding, padn);

	// Append the length of the data (number of bits)
	Update(msglen, 8);

	// m_ulState now contains the final digest, so construct
	// the hex digest string from the state.
	uint8_t digest[4] = {};
	for (int i = 0; i < 5; i++)
	{
		Put_uint32(m_ulState[i], digest, 0);
		sprintf(m_szDigest + i * 8, "%02x%02x%02x%02x",
			digest[0], digest[1], digest[2], digest[3]);
	}
}


#endif	// __SHA_1_H__INCLUDED_
