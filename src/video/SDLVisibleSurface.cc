#include "SDLVisibleSurface.hh"
#include "SDLOffScreenSurface.hh"
#include "PNG.hh"
#include "SDLSnow.hh"
#include "OSDConsoleRenderer.hh"
#include "OSDGUILayer.hh"
#include "MSXException.hh"
#include "unreachable.hh"
#include "vla.hh"
#include "build-info.hh"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>

namespace openmsx {

SDLVisibleSurface::SDLVisibleSurface(
		unsigned width, unsigned height,
		Display& display_,
		RTScheduler& rtScheduler_,
		EventDistributor& eventDistributor_,
		InputEventGenerator& inputEventGenerator_,
		CliComm& cliComm_,
		VideoSystem& videoSystem_)
	: SDLVisibleSurfaceBase(display_, rtScheduler_, eventDistributor_,
	                        inputEventGenerator_, cliComm_, videoSystem_)
{
	int flags = 0;
	createSurface(width, height, flags);

	renderer.reset(SDL_CreateRenderer(window.get(), -1, 0));
	if (!renderer) {
		std::string_view err = SDL_GetError();
		throw InitException("Could not create renderer: ", err);
	}
	SDL_RenderSetLogicalSize(renderer.get(), width, height);
#if !OPENMSX_KORIKI
	setSDLRenderer(renderer.get());
#endif

	surface.reset(SDL_CreateRGBSurface(
		0, width, height, 32,
		0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000));
	if (!surface) {
		std::string_view err = SDL_GetError();
		throw InitException("Could not create surface: ", err);
	}
	setSDLSurface(surface.get());

#if OPENMSX_KORIKI
	// Koriki (Miyoo Mini): el renderer "Miyoo Mini" de SDL2 no copia los
	// pixeles de las texturas (guarda el puntero), no mezcla alfa y coloca mal
	// las texturas parciales. Asi que el OSD se compone por software sobre
	// 'surface' y al renderer de la Miyoo solo se le sube un frame completo.
	swRenderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
	if (!swRenderer) {
		std::string_view err = SDL_GetError();
		throw InitException("Could not create software renderer: ", err);
	}
	setSDLRenderer(swRenderer.get());

	// Buffers del volcado (formato y pitch de 'surface')
	size_t words = size_t(surface->pitch / 4) * surface->h;
	for (auto& b : bufs) b.assign(words, 0);
	// Diagnostico: OPENMSX_NO_PRESENT_THREAD=1 vuelca sin hilo aparte
	if (!getenv("OPENMSX_NO_PRESENT_THREAD")) {
		presentThread = std::thread([this] { presentLoop(); });
	}
#endif

	texture.reset(SDL_CreateTexture(
		renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		width, height));
	if (!texture) {
		std::string_view err = SDL_GetError();
		throw InitException("Could not create texture: ", err);
	}


	setSDLPixelFormat(*surface->format);

	// In the SDL renderer logical size is the same as physical size.
	gl::ivec2 size(width, height);
	calculateViewPort(size, size);
}

#if OPENMSX_KORIKI
SDLVisibleSurface::~SDLVisibleSurface()
{
	{
		std::lock_guard<std::mutex> lock(presentMutex);
		quitPresent = true;
	}
	presentCond.notify_one();
	if (presentThread.joinable()) presentThread.join();
}

void SDLVisibleSurface::presentLoop()
{
	while (true) {
		{
			std::unique_lock<std::mutex> lock(presentMutex);
			presentCond.wait(lock, [this] { return frameReady || quitPresent; });
			if (quitPresent) return;
			std::swap(readyBuf, frontBuf); // coge el ultimo frame publicado
			frameReady = false;
		}
		// Solo este hilo usa 'renderer' y 'texture'. El renderer de la Miyoo
		// guarda el puntero de UpdateTexture: 'frontBuf' no cambia hasta la
		// siguiente vuelta.
		SDL_UpdateTexture(texture.get(), nullptr, frontBuf->data(), surface->pitch);
		SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
		SDL_RenderPresent(renderer.get());
	}
}

void SDLVisibleSurface::flushFrameBuffer()
{
	// Nada: el frame del MSX ya esta en 'surface' y el OSD se dibuja encima.
	// Se sube todo junto en finish().
}

void SDLVisibleSurface::finish()
{
	SDL_RenderFlush(swRenderer.get()); // termina de dibujar el OSD en 'surface'
	if (!presentThread.joinable()) {
		// Sin hilo (diagnostico): volcado directo
		SDL_UpdateTexture(texture.get(), nullptr, surface->pixels, surface->pitch);
		SDL_RenderCopy(renderer.get(), texture.get(), nullptr, nullptr);
		SDL_RenderPresent(renderer.get());
		return;
	}
	// Copia el frame terminado y lo publica; el hilo de volcado lo muestra.
	memcpy(backBuf->data(), surface->pixels, backBuf->size() * sizeof(uint32_t));
	{
		std::lock_guard<std::mutex> lock(presentMutex);
		std::swap(backBuf, readyBuf); // si el anterior no se mostro, se sustituye
		frameReady = true;
	}
	presentCond.notify_one();
}
#else
void SDLVisibleSurface::flushFrameBuffer()
{
	SDL_Renderer* render = getSDLRenderer();
	SDL_UpdateTexture(texture.get(), nullptr, surface->pixels, surface->pitch);
	SDL_RenderClear(render);
	SDL_RenderCopy(render, texture.get(), nullptr, nullptr);
}

void SDLVisibleSurface::finish()
{
	SDL_RenderPresent(getSDLRenderer());
}
#endif

std::unique_ptr<Layer> SDLVisibleSurface::createSnowLayer()
{
	switch (getPixelFormat().getBytesPerPixel()) {
#if HAVE_16BPP
	case 2:
		return std::make_unique<SDLSnow<uint16_t>>(*this, getDisplay());
#endif
#if HAVE_32BPP
	case 4:
		return std::make_unique<SDLSnow<uint32_t>>(*this, getDisplay());
#endif
	default:
		UNREACHABLE; return nullptr;
	}
}

std::unique_ptr<Layer> SDLVisibleSurface::createConsoleLayer(
		Reactor& reactor, CommandConsole& console)
{
	const bool openGL = false;
	auto [width, height] = getLogicalSize();
	return std::make_unique<OSDConsoleRenderer>(
		reactor, console, width, height, openGL);
}

std::unique_ptr<Layer> SDLVisibleSurface::createOSDGUILayer(OSDGUI& gui)
{
	return std::make_unique<SDLOSDGUILayer>(gui);
}

std::unique_ptr<OutputSurface> SDLVisibleSurface::createOffScreenSurface()
{
	return std::make_unique<SDLOffScreenSurface>(*surface);
}

void SDLVisibleSurface::saveScreenshot(const std::string& filename)
{
	saveScreenshotSDL(*this, filename);
}

void SDLVisibleSurface::saveScreenshotSDL(
	const SDLOutputSurface& output, const std::string& filename)
{
	auto [width, height] = output.getLogicalSize();
	VLA(const void*, rowPointers, height);
	MemBuffer<uint8_t> buffer(width * height * 3);
	for (auto i : xrange(height)) {
		rowPointers[i] = &buffer[width * 3 * i];
	}
	if (SDL_RenderReadPixels(
			output.getSDLRenderer(), nullptr,
			SDL_PIXELFORMAT_RGB24, buffer.data(), width * 3)) {
		throw MSXException("Couldn't acquire screenshot pixels: ", SDL_GetError());
	}
	PNG::save(width, height, rowPointers, filename);
}

void SDLVisibleSurface::clearScreen()
{
	SDL_FillRect(surface.get(), nullptr, 0);
}

void SDLVisibleSurface::fullScreenUpdated(bool /*fullscreen*/)
{
	// nothing
}

} // namespace openmsx
