#ifndef MIAOSOUNDDRIVER_HH
#define MIAOSOUNDDRIVER_HH

#if OPENMSX_KORIKI

#include "SoundDriver.hh"
#include <cstdint>
#include <vector>

namespace openmsx {

class Reactor;

/** Salida de audio directa por mi_ao (Miyoo Mini), sin pasar por SDL.
  * Sigue el driver MIAO del RetroArch de Koriki
  * (audio/drivers/audioio_miyoomini.c): envio no bloqueante con
  * MI_AO_SendFrame y solo se duerme lo que sobra en la cola. */
class MiaoSoundDriver final : public SoundDriver
{
public:
	MiaoSoundDriver(Reactor& reactor, unsigned wantedFreq, unsigned wantedSamples);
	~MiaoSoundDriver() override;
	MiaoSoundDriver(const MiaoSoundDriver&) = delete;
	MiaoSoundDriver& operator=(const MiaoSoundDriver&) = delete;

	void mute() override;
	void unmute() override;
	[[nodiscard]] unsigned getFrequency() const override;
	[[nodiscard]] unsigned getSamples() const override;
	void uploadBuffer(float* buffer, unsigned len) override;

private:
	void sendBytes(const void* data, uint32_t bytes);

	Reactor& reactor;
	unsigned frequency;
	unsigned fragmentSize;  // muestras estereo por bloque
	uint32_t queueBytes;    // cola objetivo en mi_ao (bytes)
	bool muted = true;
	std::vector<int16_t> pcm;
};

} // namespace openmsx

#endif // OPENMSX_KORIKI
#endif
