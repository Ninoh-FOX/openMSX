#include "SDLSoundDriver.hh"
#include "Reactor.hh"
#include "MSXMotherBoard.hh"
#include "RealTime.hh"
#include "GlobalSettings.hh"
#include "ThrottleManager.hh"
#include "MSXException.hh"
#include "Math.hh"
#include "Timer.hh"
#include <algorithm>
#include <cassert>
#include <cstring>

namespace openmsx {

SDLSoundDriver::SDLSoundDriver(Reactor& reactor_,
                               unsigned wantedFreq, unsigned wantedSamples)
	: reactor(reactor_)
	, muted(true)
{
	SDL_AudioSpec desired;
	desired.freq     = wantedFreq;
	desired.samples  = Math::ceil2(wantedSamples);
	desired.channels = 2; // stereo
#if OPENMSX_AUDIO_S16
	// Miyoo Mini (driver SDL2 "Miyoo Mini"): solo acepta 16 bits con signo
	// y no informa del formato, asi que SDL no convertiria desde float.
	desired.format   = AUDIO_S16SYS;
#else
	desired.format   = AUDIO_F32SYS;
#endif
	desired.callback = audioCallbackHelper; // must be a static method
	desired.userdata = this;

	SDL_AudioSpec obtained;
	deviceID = SDL_OpenAudioDevice(nullptr, SDL_FALSE, &desired, &obtained,
	                               SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
	if (!deviceID) {
		throw MSXException("Unable to open SDL audio: ", SDL_GetError());
	}

	frequency = obtained.freq;
	fragmentSize = obtained.samples;

#if OPENMSX_AUDIO_S16
	mixBufferSize = 3 * (obtained.size / sizeof(int16_t)) + 2;
#else
	mixBufferSize = 3 * (obtained.size / sizeof(float)) + 2;
#endif
	mixBuffer.resize(mixBufferSize);
	reInit();
}

SDLSoundDriver::~SDLSoundDriver()
{
	SDL_CloseAudioDevice(deviceID);
}

void SDLSoundDriver::reInit()
{
	SDL_LockAudioDevice(deviceID);
	readIdx  = 0;
	writeIdx = 0;
	SDL_UnlockAudioDevice(deviceID);
}

void SDLSoundDriver::mute()
{
	if (!muted) {
		muted = true;
		SDL_PauseAudioDevice(deviceID, 1);
	}
}

void SDLSoundDriver::unmute()
{
	if (muted) {
		muted = false;
		reInit();
		SDL_PauseAudioDevice(deviceID, 0);
	}
}

unsigned SDLSoundDriver::getFrequency() const
{
	return frequency;
}

unsigned SDLSoundDriver::getSamples() const
{
	return fragmentSize;
}

#if OPENMSX_AUDIO_S16
void SDLSoundDriver::audioCallbackHelper(void* userdata, uint8_t* strm, int len)
{
	assert((len & 3) == 0); // stereo, 16 bit
	static_cast<SDLSoundDriver*>(userdata)->
		audioCallbackS16(reinterpret_cast<int16_t*>(strm), len / sizeof(int16_t));
}

static inline int16_t floatToS16(float f)
{
	int v = int(f * 32768.0f);
	return int16_t(std::clamp(v, -32768, 32767));
}

void SDLSoundDriver::audioCallbackS16(int16_t* stream, unsigned len)
{
	assert((len & 1) == 0); // stereo
	unsigned available = getBufferFilled();
	unsigned num = std::min(len, available);
	for (unsigned i = 0; i < num; ++i) {
		stream[i] = floatToS16(mixBuffer[readIdx]);
		if (++readIdx == mixBufferSize) readIdx = 0;
	}
	if (num < len) {
		// buffer underrun
		memset(&stream[num], 0, (len - num) * sizeof(int16_t));
	}
}
#else
void SDLSoundDriver::audioCallbackHelper(void* userdata, uint8_t* strm, int len)
{
	assert((len & 7) == 0); // stereo, 32 bit float
	static_cast<SDLSoundDriver*>(userdata)->
		audioCallback(reinterpret_cast<float*>(strm), len / sizeof(float));
}
#endif

unsigned SDLSoundDriver::getBufferFilled() const
{
	int result = writeIdx - readIdx;
	if (result < 0) result += mixBufferSize;
	assert((0 <= result) && (unsigned(result) < mixBufferSize));
	return result;
}

unsigned SDLSoundDriver::getBufferFree() const
{
	// we can't distinguish completely filled from completely empty
	// (in both cases readIx would be equal to writeIdx), so instead
	// we define full as '(writeIdx + 2) == readIdx' (note that index
	// increases in steps of 2 (stereo)).
	int result = mixBufferSize - 2 - getBufferFilled();
	assert((0 <= result) && (unsigned(result) < mixBufferSize));
	return result;
}

void SDLSoundDriver::audioCallback(float* stream, unsigned len)
{
	assert((len & 1) == 0); // stereo
	unsigned available = getBufferFilled();
	unsigned num = std::min(len, available);
	if ((readIdx + num) < mixBufferSize) {
		memcpy(stream, &mixBuffer[readIdx], num * sizeof(float));
		readIdx += num;
	} else {
		unsigned len1 = mixBufferSize - readIdx;
		memcpy(stream, &mixBuffer[readIdx], len1 * sizeof(float));
		unsigned len2 = num - len1;
		memcpy(&stream[len1], &mixBuffer[0], len2 * sizeof(float));
		readIdx = len2;
	}
	int missing = len - available;
	if (missing > 0) {
		// buffer underrun
		memset(&stream[available], 0, missing * sizeof(float));
	}
}

void SDLSoundDriver::uploadBuffer(float* buffer, unsigned len)
{
	SDL_LockAudioDevice(deviceID);
	len *= 2; // stereo
	unsigned free = getBufferFree();
	if (len > free) {
		auto* board = reactor.getMotherBoard();
		if (board && !board->getMSXMixer().isSynchronousMode() && // when not recording
		    reactor.getGlobalSettings().getThrottleManager().isThrottled()) {
			do {
				SDL_UnlockAudioDevice(deviceID);
				Timer::sleep(5000); // 5ms
				SDL_LockAudioDevice(deviceID);
				board->getRealTime().resync();
				free = getBufferFree();
			} while (len > free);
		} else {
			// drop excess samples
			len = free;
		}
	}
	assert(len <= free);
	if ((writeIdx + len) < mixBufferSize) {
		memcpy(&mixBuffer[writeIdx], buffer, len * sizeof(float));
		writeIdx += len;
	} else {
		unsigned len1 = mixBufferSize - writeIdx;
		memcpy(&mixBuffer[writeIdx], buffer, len1 * sizeof(float));
		unsigned len2 = len - len1;
		memcpy(&mixBuffer[0], &buffer[len1], len2 * sizeof(float));
		writeIdx = len2;
	}

	SDL_UnlockAudioDevice(deviceID);
}

} // namespace openmsx
