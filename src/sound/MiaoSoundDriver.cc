#include "MiaoSoundDriver.hh"

#if OPENMSX_KORIKI

#include "Reactor.hh"
#include "MSXMotherBoard.hh"
#include "MSXMixer.hh"
#include "RealTime.hh"
#include "GlobalSettings.hh"
#include "ThrottleManager.hh"
#include "MSXException.hh"
#include "Math.hh"
#include <algorithm>
#include <unistd.h>
#include <mi_ao.h>

namespace openmsx {

static constexpr MI_AUDIO_DEV AO_DEV = 0;
static constexpr MI_AO_CHN    AO_CHN = 0;
static constexpr unsigned BYTES_PER_FRAME = 4; // estereo, 16 bits

MiaoSoundDriver::MiaoSoundDriver(Reactor& reactor_,
                                 unsigned wantedFreq, unsigned wantedSamples)
	: reactor(reactor_)
{
	// Frecuencias que acepta mi_ao (como freqtable en audioio_miyoomini.c)
	static constexpr unsigned freqs[] = {
		8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000 };
	frequency = 48000;
	for (auto f : freqs) {
		if (wantedFreq <= f) { frequency = f; break; }
	}
	fragmentSize = std::clamp(Math::ceil2(wantedSamples), 256u, 2048u);
	// Dos bloques en cola: suficiente para no cortar y poca latencia.
	queueBytes = 2 * fragmentSize * BYTES_PER_FRAME;

	MI_AUDIO_Attr_t attr = {};
	attr.eBitwidth      = E_MI_AUDIO_BIT_WIDTH_16;
	attr.eWorkmode      = E_MI_AUDIO_MODE_I2S_MASTER;
	attr.eSamplerate    = static_cast<MI_AUDIO_SampleRate_e>(frequency);
	attr.eSoundmode     = E_MI_AUDIO_SOUND_MODE_STEREO;
	attr.u32ChnCnt      = 2;
	attr.u32PtNumPerFrm = fragmentSize;
	if (MI_AO_SetPubAttr(AO_DEV, &attr) != MI_SUCCESS ||
	    MI_AO_Enable(AO_DEV) != MI_SUCCESS ||
	    MI_AO_EnableChn(AO_DEV, AO_CHN) != MI_SUCCESS) {
		MI_AO_Disable(AO_DEV);
		throw MSXException("Unable to open Miyoo audio (mi_ao). "
		                   "Is audioserver still running?");
	}
	MI_AO_SetMute(AO_DEV, FALSE);
	MI_AO_ClearChnBuf(AO_DEV, AO_CHN);

	// Bloque de silencio inicial, como hace MIAO
	pcm.assign(size_t(fragmentSize) * 2, 0);
	sendBytes(pcm.data(), fragmentSize * BYTES_PER_FRAME);
}

MiaoSoundDriver::~MiaoSoundDriver()
{
	MI_AO_ClearChnBuf(AO_DEV, AO_CHN);
	MI_AO_DisableChn(AO_DEV, AO_CHN);
	MI_AO_Disable(AO_DEV);
}

void MiaoSoundDriver::mute()
{
	if (!muted) {
		muted = true;
		MI_AO_ClearChnBuf(AO_DEV, AO_CHN);
	}
}

void MiaoSoundDriver::unmute()
{
	muted = false;
}

unsigned MiaoSoundDriver::getFrequency() const
{
	return frequency;
}

unsigned MiaoSoundDriver::getSamples() const
{
	return fragmentSize;
}

void MiaoSoundDriver::sendBytes(const void* data, uint32_t bytes)
{
	MI_AUDIO_Frame_t frame = {};
	frame.eBitwidth    = E_MI_AUDIO_BIT_WIDTH_16;
	frame.eSoundmode   = E_MI_AUDIO_SOUND_MODE_STEREO;
	frame.apVirAddr[0] = const_cast<void*>(data);
	frame.u32Len       = bytes;
	MI_AO_SendFrame(AO_DEV, AO_CHN, &frame, 0); // 0 = no bloquear
}

void MiaoSoundDriver::uploadBuffer(float* buffer, unsigned len)
{
	if (muted || len == 0) return;

	// float [-1,1] -> 16 bits con signo, estereo intercalado
	unsigned n = len * 2;
	if (pcm.size() < n) pcm.resize(n);
	for (unsigned i = 0; i < n; ++i) {
		int v = int(buffer[i] * 32768.0f);
		pcm[i] = int16_t(std::clamp(v, -32768, 32767));
	}
	uint32_t bytes = len * BYTES_PER_FRAME;

	MI_AO_ChnState_t st = {};
	MI_AO_QueryChnStat(AO_DEV, AO_CHN, &st);
	int avail = int(queueBytes) - int(st.u32ChnBusyNum);

	auto* board = reactor.getMotherBoard();
	bool block = board && !board->getMSXMixer().isSynchronousMode() && // no grabando
	             reactor.getGlobalSettings().getThrottleManager().isThrottled();

	if (avail < int(bytes) && block) {
		// Como MIAO: se envia todo y se duerme solo el exceso de la cola.
		sendBytes(pcm.data(), bytes);
		MI_AO_QueryChnStat(AO_DEV, AO_CHN, &st);
		if (st.u32ChnBusyNum > queueBytes) {
			uint64_t us = uint64_t(st.u32ChnBusyNum - queueBytes) * 1000000
			            / (uint64_t(frequency) * BYTES_PER_FRAME);
			if (us) {
				usleep(useconds_t(us));
				// Que el reloj de openMSX no cuente esta espera como retraso
				board->getRealTime().resync();
			}
		}
	} else {
		// Hay sitio (o avance rapido): se envia lo que quepa
		uint32_t toSend = std::min<uint32_t>(bytes, uint32_t(std::max(avail, 0)));
		toSend &= ~(BYTES_PER_FRAME - 1);
		if (toSend) sendBytes(pcm.data(), toSend);
	}
}

} // namespace openmsx

#endif // OPENMSX_KORIKI
