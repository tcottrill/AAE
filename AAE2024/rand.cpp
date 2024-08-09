#include "rand.h"

//use this first function to seed the random number generator,
//call this before any of the other functions
void initrand()
{
	srand((unsigned)(time(0)));
	//srand(rand()*GetTickCount());
}

//generates a psuedo-random integer between 0 and 32767
int randint()
{
	return rand();
}

//generates a psuedo-random integer between 0 and max
int randintm(int max)
{
	return (int)(max * rand() / (RAND_MAX + 1.0));
}

//generates a psuedo-random integer between min and max
int randintmm(int min, int max)
{
	if (min > max)
	{
		return max + (int)((min - max + 1) * rand() / (RAND_MAX + 1.0));
	}
	else
	{
		return min + (int)((max - min + 1) * rand() / (RAND_MAX + 1.0));
	}
}

//generates a psuedo-random float between 0.0 and 0.999...
float randfloat()
{
	return rand() / ((float)(RAND_MAX)+1);
}

//generates a psuedo-random float between 0.0 and max
float randfloatm(float max)
{
	return randfloat() * max;
}

//generates a psuedo-random float between min and max
float randfloatmm(float min, float max)
{
	if (min > max)
	{
		return randfloat() * (min - max) + max;
	}
	else
	{
		return randfloat() * (max - min) + min;
	}
}

//generates a psuedo-random double between 0.0 and 0.999...
double randdouble()
{
	return rand() / ((double)(RAND_MAX)+1);
}

//generates a psuedo-random double between 0.0 and max
double randdoublem(double max)
{
	return randdouble() * max;
}

//generates a psuedo-random double between min and max
double randdoublemm(double min, double max)
{
	if (min > max)
	{
		return randdouble() * (min - max) + max;
	}
	else
	{
		return randdouble() * (max - min) + min;
	}
}

/*
char* randstr(char* str, unsigned long length)
{
	int x;
	//make we were passed a valid pointer
	if (!str)
	{
		return 0;
	}

	//make sure the string is supposed to contain something
	if (!length)
	{
		return 0;
	}

	//put random characters into the string, give both
	//upper and lower case numbers an equal chance at
	//being used
	for ((unsigned long) x=0; x < (length-1);x++)
	{
		if (!randint(1))
		{
			str[x]=(char)randint(65,90);
		}
		else
		{
			str[x]=(char)randint(97,122);
		}
	}

	//null terminate the string
	str[x]=0;

	return str;
}

//flags for the extended randstr function
#define RAND_STR_CAPITOL_LETTERS    0x00000001    //65 through 90
#define RAND_STR_LOWER_CASE_LETTERS    0x00000002    //97 through 122
#define    RAND_STR_NUMBERS        0x00000004    //48 through 57
#define RAND_STR_SYMBOLS_1        0x00000008    //32 through 47
#define RAND_STR_SYMBOLS_2        0x00000010    //58 through 64
#define RAND_STR_SYMBOLS_3        0x00000020    //91 through 96
#define RAND_STR_SYMBOLS_4        0x00000040    //123 through 126
#define RAND_STR_NON_PRINTING        0x00000080    //1 through 31 and 127
#define RAND_STR_EXTENDED_ASCII        0x00000100    //128 through 255

char* randstrb(char* str, unsigned long length, unsigned long flags)
{
	//make we were passed a valid pointer
	if (!str)
	{
		return 0;
	}

	//make sure the string is supposed to contain something
	if (!length)
	{
		return 0;
	}

	//if none of the flags were used then we set flags to use just upper and lower case
	if ((!flags&0x00000001)&&(!flags&0x00000002)&&(!flags&0x00000004)&&
	(!flags&0x00000008)&&(!flags&0x00000010)&&(!flags&0x00000020)&&(!flags&0x00000040)
	&&(!flags&0x00000080)&&(!flags&0x00000100))
	{
		flags=RAND_STR_LOWER_CASE_LETTERS|RAND_STR_CAPITOL_LETTERS;
	}

	char t[9]={0};
	int i=0;

	//each pass of the loop the flags are checked and for each flag that is used
	//then a random character in the flags range is added to a temporary array of
	//characters, after all the flags are checked a random character from the
	//temporary array is chosen to be inserted into the string
	for (unsigned long x=0;x<length-1;x++)
	{
		i=0;

		if (flags&0x1)
		{
			t[i]=randint(65,90);
			i++;
		}

		if (flags&0x2)
		{
			t[i]=randint(97,122);
			i++;
		}

		if (flags&0x4)
		{
			t[i]=randint(48,57);
			i++;
		}

		if (flags&0x8)
		{
			t[i]=randint(32,47);
			i++;
		}

		if (flags&0x10)
		{
			t[i]=randint(58,64);
			i++;
		}

		if (flags&0x20)
		{
			t[i]=randint(91,96);
			i++;
		}

		if (flags&0x40)
		{
			t[i]=randint(123,126);
			i++;
		}

		if (flags&0x80)
		{
			if (!randint(32))
			{
				t[i]=127;
				i++;
			}
			else
			{
				t[i]=randint(1,31);
				i++;
			}
		}

		if (flags&0x100)
		{
			t[i]=randint(127,255);
			i++;
		}

		str[x]=t[randint(i-1)];
	}

	//null terminate the string
	str[x]=0;

	return str;
}
*/