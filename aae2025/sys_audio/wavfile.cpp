#include "wavfile.h"
#include "log.h"


#include <vcruntime_string.h>
/*
Note this simple wav file loader taken from
http://forums.qj.net/psp-development-forum/144147-sample-simple-wav-loader.html
*/

//Originally this only handled wav file loading, but i've hacked in non streaming ogg handling. This should be broken out into its own handler.


#pragma warning (disable : 4018 ) //Signed Unsigned mismatch

wavedef Wave;

int WavFileLoadInternal(unsigned char* wavfile, int size)
{
	int a = 0;

	if (memcmp(wavfile, "RIFF", 4) != 0)
	{
		wrlog("Loaded file is not an WAVE or Ogg, returning error!\n");
		return false;
	}

	Wave.loadtype = 1;
	Wave.channels = *(int16_t*)(wavfile + 0x16);
	Wave.sampleRate = *(uint16_t*)(wavfile + 0x18);
	Wave.blockAlign = *(int16_t*)(wavfile + 0x20);
	Wave.bitPerSample = *(int16_t*)(wavfile + 0x22);

	for (a = 0; memcmp(wavfile + 0x24 + a, "data", 4) != 0; a++)
	{
		if (a == 0xFF) { return false; }
	}

	Wave.dataLength = *(unsigned long*)(wavfile + 0x28 + a);

	if (Wave.dataLength + 0x2c > size) { return false; } // Invalid header size

	if (Wave.channels != 2 && Wave.channels != 1) { return false; } // Invalid # of channels

	if (Wave.sampleRate > 100000 || Wave.sampleRate < 2000) { return false; } // Invalid bitrate

	if (Wave.channels == 2) { Wave.sampleCount = Wave.dataLength / (Wave.bitPerSample >> 2); }
	else { Wave.sampleCount = Wave.dataLength / ((Wave.bitPerSample >> 2) >> 1); }

	if (Wave.sampleCount <= 0) { return false; } //Invalid samplecount

	Wave.data = wavfile + 0x2c;

	return true;
}

/*
bool ReadWaveFile(const char* fileName, std::vector& samples, int32 sampleRate)
{
    //open the file if we can
    FILE* File = fopen(fileName, "rb");
    if (!File)
    {
        return false;
    }

    //read the main chunk ID and make sure it's "RIFF"
    char buffer[5];
    buffer[4] = 0;
    if (fread(buffer, 4, 1, File) != 1 || strcmp(buffer, "RIFF"))
    {
        fclose(File);
        return false;
    }

    //read the main chunk size
    uint32_t nChunkSize;
    if (fread(&nChunkSize, 4, 1, File) != 1)
    {
        fclose(File);
        return false;
    }

    //read the format and make sure it's "WAVE"
    if (fread(buffer, 4, 1, File) != 1 || strcmp(buffer, "WAVE"))
    {
        fclose(File);
        return false;
    }

    long chunkPosFmt = -1;
    long chunkPosData = -1;

    while (chunkPosFmt == -1 || chunkPosData == -1)
    {
        //read a sub chunk id and a chunk size if we can
        if (fread(buffer, 4, 1, File) != 1 || fread(&nChunkSize, 4, 1, File) != 1)
        {
            fclose(File);
            return false;
        }

        //if we hit a fmt
        if (!strcmp(buffer, "fmt "))
        {
            chunkPosFmt = ftell(File) - 8;
        }
        //else if we hit a data
        else if (!strcmp(buffer, "data"))
        {
            chunkPosData = ftell(File) - 8;
        }

        //skip to the next chunk
        fseek(File, nChunkSize, SEEK_CUR);
    }

    //we'll use this handy struct to load in 
    SMinimalWaveFileHeader waveData;

    //load the fmt part if we can
    fseek(File, chunkPosFmt, SEEK_SET);
    if (fread(&waveData.m_szSubChunk1ID, 24, 1, File) != 1)
    {
        fclose(File);
        return false;
    }

    //load the data part if we can
    fseek(File, chunkPosData, SEEK_SET);
    if (fread(&waveData.m_szSubChunk2ID, 8, 1, File) != 1)
    {
        fclose(File);
        return false;
    }

    //verify a couple things about the file data
    if (waveData.m_nAudioFormat != 1 ||       //only pcm data
        waveData.m_nNumChannels  2 ||        //must not have more than 2
        waveData.m_nBitsPerSample > 32 ||     //32 bits per sample max
        waveData.m_nBitsPerSample % 8 != 0 || //must be a multiple of 8 bites
        waveData.m_nBlockAlign > 8)           //blocks must be 8 bytes or lower
    {
        fclose(File);
        return false;
    }

    //figure out how many samples and blocks there are total in the source data
    int nBytesPerBlock = waveData.m_nBlockAlign;
    int nNumBlocks = waveData.m_nSubChunk2Size / nBytesPerBlock;
    int nNumSourceSamples = nNumBlocks * waveData.m_nNumChannels;

    //allocate space for the source samples
    samples.resize(nNumSourceSamples);

    //maximum size of a block is 8 bytes.  4 bytes per samples, 2 channels
    unsigned char pBlockData[8];
    memset(pBlockData, 0, 8);

    //read in the source samples at whatever sample rate / number of channels it might be in
    int nBytesPerSample = nBytesPerBlock / waveData.m_nNumChannels;
    for (int nIndex = 0; nIndex = TPhase(1.0f))
        phase -= TPhase(1.0f);
    while (phase  0.5f ? 1.0f : -1.0f);
}
*/