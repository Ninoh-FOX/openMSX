#include "Mixer.hh"
#include "MSXMixer.hh"
#include "NullSoundDriver.hh"
#include "SDLSoundDriver.hh"
#include "MiaoSoundDriver.hh"
#include "CommandController.hh"
#include "CliComm.hh"
#include "MSXException.hh"
#include "one_of.hh"
#include "stl.hh"
#include "unreachable.hh"
#include "build-info.hh"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace openmsx {

#if defined(_WIN32)
constexpr int defaultsamples = 2048;
#else
constexpr int defaultsamples = 1024;
#endif

static EnumSetting<Mixer::SoundDriverType>::Map getSoundDriverMap()
{
	EnumSetting<Mixer::SoundDriverType>::Map soundDriverMap = {
		{ "null", Mixer::SND_NULL },
		{ "sdl",  Mixer::SND_SDL } };
#if OPENMSX_KORIKI
	// Koriki: salida directa por mi_ao, como el driver MIAO de RetroArch
	soundDriverMap.emplace_back("miao", Mixer::SND_MIAO);
#endif
	return soundDriverMap;
}

Mixer::Mixer(Reactor& reactor_, CommandController& commandController_)
	: reactor(reactor_)
	, commandController(commandController_)
	, soundDriverSetting(
		commandController, "sound_driver",
		"select the sound output driver",
#if OPENMSX_KORIKI
		Mixer::SND_MIAO,
#else
		Mixer::SND_SDL,
#endif
		getSoundDriverMap())
	, muteSetting(
		commandController, "mute",
		"(un)mute the emulation sound", false, Setting::DONT_SAVE)
	, masterVolume(
		commandController, "master_volume",
		"master volume", 75, 0, 100)
	, frequencySetting(
		commandController, "frequency",
		"mixer frequency", 44100, 11025, 48000)
	, samplesSetting(
		commandController, "samples",
		"mixer samples", defaultsamples, 64, 8192)
	, muteCount(0)
{
	muteSetting       .attach(*this);
	frequencySetting  .attach(*this);
	samplesSetting    .attach(*this);
	soundDriverSetting.attach(*this);

	// Set correct initial mute state.
	if (muteSetting.getBoolean()) ++muteCount;

	reloadDriver();
}

Mixer::~Mixer()
{
	assert(msxMixers.empty());
	driver.reset();

	soundDriverSetting.detach(*this);
	samplesSetting    .detach(*this);
	frequencySetting  .detach(*this);
	muteSetting       .detach(*this);
}

void Mixer::reloadDriver()
{
	// Destroy old driver before attempting to create a new one. Though
	// this means we end up without driver if creating the new one failed
	// for some reason.

	driver = std::make_unique<NullSoundDriver>();

	auto type = soundDriverSetting.getEnum();
#if OPENMSX_KORIKI
	// Diagnostico: OPENMSX_SOUND_DRIVER=sdl|null elige el driver desde el
	// arranque (antes de abrir mi_ao), sin cambiar el ajuste guardado.
	if (const char* env = getenv("OPENMSX_SOUND_DRIVER")) {
		if (strcmp(env, "sdl") == 0)  type = SND_SDL;
		if (strcmp(env, "null") == 0) type = SND_NULL;
	}
#endif
	try {
		switch (type) {
		case SND_NULL:
			driver = std::make_unique<NullSoundDriver>();
			break;
		case SND_SDL:
			driver = std::make_unique<SDLSoundDriver>(
				reactor,
				frequencySetting.getInt(),
				samplesSetting.getInt());
			break;
#if OPENMSX_KORIKI
		case SND_MIAO:
			driver = std::make_unique<MiaoSoundDriver>(
				reactor,
				frequencySetting.getInt(),
				samplesSetting.getInt());
			break;
#endif
		default:
			UNREACHABLE;
		}
	} catch (MSXException& e) {
		commandController.getCliComm().printWarning(e.getMessage());
	}

	muteHelper();
}

void Mixer::registerMixer(MSXMixer& mixer)
{
	assert(!contains(msxMixers, &mixer));
	msxMixers.push_back(&mixer);
	muteHelper();
}

void Mixer::unregisterMixer(MSXMixer& mixer)
{
	move_pop_back(msxMixers, rfind_unguarded(msxMixers, &mixer));
	muteHelper();
}


void Mixer::mute()
{
	if (muteCount++ == 0) {
		muteHelper();
	}
}

void Mixer::unmute()
{
	assert(muteCount);
	if (--muteCount == 0) {
		muteHelper();
	}
}

void Mixer::muteHelper()
{
	bool isMuted = muteCount || msxMixers.empty();
	unsigned samples = isMuted ? 0 : driver->getSamples();
	unsigned frequency = driver->getFrequency();
	for (auto& m : msxMixers) {
		m->setMixerParams(samples, frequency);
	}

	if (isMuted) {
		driver->mute();
	} else {
		driver->unmute();
	}
}

void Mixer::uploadBuffer(MSXMixer& /*msxMixer*/, float* buffer, unsigned len)
{
	// can only handle one MSXMixer ATM
	assert(!msxMixers.empty());

	driver->uploadBuffer(buffer, len);
}

void Mixer::update(const Setting& setting) noexcept
{
	if (&setting == &muteSetting) {
		if (muteSetting.getBoolean()) {
			mute();
		} else {
			unmute();
		}
	} else if (&setting == one_of(&samplesSetting, &soundDriverSetting, &frequencySetting)) {
		reloadDriver();
	} else {
		UNREACHABLE;
	}
}

} // namespace openmsx
