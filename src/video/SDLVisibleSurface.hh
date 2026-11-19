#ifndef SDLVISIBLESURFACE_HH
#define SDLVISIBLESURFACE_HH

#include "SDLVisibleSurfaceBase.hh"
#if OPENMSX_KORIKI
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#endif

namespace openmsx {

class SDLVisibleSurface final : public SDLVisibleSurfaceBase
{
public:
	SDLVisibleSurface(unsigned width, unsigned height,
	                  Display& display,
	                  RTScheduler& rtScheduler,
	                  EventDistributor& eventDistributor,
	                  InputEventGenerator& inputEventGenerator,
	                  CliComm& cliComm,
	                  VideoSystem& videoSystem);
#if OPENMSX_KORIKI
	~SDLVisibleSurface() override;
#endif

	static void saveScreenshotSDL(const SDLOutputSurface& output,
	                              const std::string& filename);

	// OutputSurface
	void saveScreenshot(const std::string& filename) override;
	void flushFrameBuffer() override;
	void clearScreen() override;

	// VisibleSurface
	void finish() override;
	[[nodiscard]] std::unique_ptr<Layer> createSnowLayer() override;
	[[nodiscard]] std::unique_ptr<Layer> createConsoleLayer(
		Reactor& reactor, CommandConsole& console) override;
	[[nodiscard]] std::unique_ptr<Layer> createOSDGUILayer(OSDGUI& gui) override;
	[[nodiscard]] std::unique_ptr<OutputSurface> createOffScreenSurface() override;
	void fullScreenUpdated(bool fullscreen) override;

private:
	SDLRendererPtr renderer;
	SDLSurfacePtr surface;
	SDLTexturePtr texture;
#if OPENMSX_KORIKI
	// Renderer por software que dibuja el OSD sobre 'surface'. Declarado
	// despues de 'surface' para que se destruya antes.
	SDLRendererPtr swRenderer;

	// Volcado en un hilo aparte (como GFX_FlipThread del RetroArch de la
	// Miyoo): el blit MI_GFX y el cambio de pagina no bloquean la emulacion.
	// Triple buffer: el hilo principal escribe en 'backBuf', lo publica como
	// 'readyBuf' y el hilo muestra 'frontBuf'. Nunca se espera al hilo.
	void presentLoop();
	std::vector<uint32_t> bufs[3];
	std::vector<uint32_t>* backBuf  = &bufs[0];
	std::vector<uint32_t>* readyBuf = &bufs[1];
	std::vector<uint32_t>* frontBuf = &bufs[2];
	std::mutex presentMutex;
	std::condition_variable presentCond;
	bool frameReady = false;
	bool quitPresent = false;
	std::thread presentThread;
#endif
};

} // namespace openmsx

#endif
