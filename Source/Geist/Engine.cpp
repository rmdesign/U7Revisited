#include <Geist/Globals.h>
#include <Geist/Engine.h>
#include <Geist/ResourceManager.h>
#include <Geist/StateMachine.h>
#include <Geist/ScriptingSystem.h>
#include <Geist/SoundSystem.h>
#include <Geist/InputSystem.h>
#include <Geist/Logging.h>
#include <sstream>
#include <fstream>
#include <time.h>
#include <algorithm>

// Aspect-ratio lock added to our patched raylib (rcore_desktop.c).
extern "C" void SetWindowAspectRatio(int numer, int denom);

extern float g_DrawScale;
extern float g_LetterboxX;
extern float g_LetterboxY;

Rectangle GetGuiBlitDest()
{
	float w = g_Engine->m_ScreenWidth  - 2.0f * g_LetterboxX;
	float h = g_Engine->m_ScreenHeight - 2.0f * g_LetterboxY;
	return { g_LetterboxX, g_Engine->m_ScreenHeight - g_LetterboxY, w, -h };
}

using namespace std;

void Engine::Init(const std::string &configfile)
{
	Log("Starting Engine::Init()");
	m_Done = false;
	m_ConfigFileName = configfile;
	m_EngineConfig.Load(configfile);

	g_ResourceManager = make_unique<ResourceManager>();
	g_ResourceManager->Init(configfile);
	g_StateMachine = make_unique<StateMachine>();
	g_StateMachine->Init(configfile);
	g_ScriptingSystem = make_unique<ScriptingSystem>();
	g_ScriptingSystem->Init(configfile);
	g_InputSystem = make_unique<InputSystem>();
	g_InputSystem->Init(configfile);

	m_GameUpdates = 0;

	m_CurrentFrame = 0;

	m_debugDrawing = false;

	m_RenderWidth = m_EngineConfig.GetNumber("h_renderres");
	m_RenderHeight = m_EngineConfig.GetNumber("v_renderres");

	int requestedW = (int)m_EngineConfig.GetNumber("h_res");
	int requestedH = (int)m_EngineConfig.GetNumber("v_res");

	//  Initialize Raylib and the screen.
	std::string windowTitle = m_EngineConfig.GetString("name");
	SetConfigFlags(FLAG_WINDOW_HIGHDPI | FLAG_WINDOW_RESIZABLE);

	// Open at a conservative initial size so we can query the monitor before final sizing.
	int initialW = std::min(requestedW, 1280);
	int initialH = std::min(requestedH, 720);
	InitWindow(initialW, initialH, windowTitle.c_str());
	SetExitKey(KEY_NULL); // We'll handle exiting with ESC

	// Now we can query the active monitor and pick a final size that fits.
	// Window must keep the configured aspect (e.g. 16:9 from 1920x1080) — the GUI
	// is rendered into a fixed-aspect RTT and hit-test math assumes uniform scale.
	int monitor = GetCurrentMonitor();
	int monitorW = GetMonitorWidth(monitor);
	int monitorH = GetMonitorHeight(monitor);
	if (monitorW > 0 && monitorH > 0 && requestedH > 0)
	{
		float aspect = (float)requestedW / (float)requestedH;
		int maxW = (monitorW * 9) / 10;
		int maxH = (monitorH * 9) / 10;
		// Fit-inside: pick the dimension that limits us first, derive the other from aspect.
		int targetW, targetH;
		if ((float)maxW / aspect <= (float)maxH) {
			targetW = maxW;
			targetH = (int)((float)maxW / aspect);
		} else {
			targetH = maxH;
			targetW = (int)((float)maxH * aspect);
		}
		// Don't open larger than configured.
		if (targetW > requestedW) { targetW = requestedW; targetH = requestedH; }

		SetWindowMinSize(640, (int)(640.0f / aspect));
		SetWindowSize(targetW, targetH);
		SetWindowPosition((monitorW - targetW) / 2, (monitorH - targetH) / 2);
	}

	// Lock window aspect to the configured render aspect so resizing keeps it 16:9.
	SetWindowAspectRatio(requestedW, requestedH);

	if (g_Engine->m_EngineConfig.GetNumber("full_screen") == 1)
	{
		// Borderless windowed is reliable across macOS/Linux/Windows, unlike GLFW's
		// exclusive fullscreen (which on macOS leaves the framebuffer at the requested
		// size, producing a bottom-left quadrant render).
		ToggleBorderlessWindowed();
	}

	m_ScreenWidth  = GetScreenWidth();
	m_ScreenHeight = GetScreenHeight();

	SetTargetFPS(60);

	//  Relies on Raylib, so let's set it up after Raylib has started.
	g_SoundSystem = make_unique<SoundSystem>();
	g_SoundSystem->Init(configfile);

	HideCursor(); // We'll use our own.

	Log("Done with Engine::Init()");
}

void Engine::Shutdown()
{
	g_StateMachine->Shutdown();
	g_ResourceManager->Shutdown();
	g_InputSystem->Shutdown();
	CloseAudioDevice();
}

void Engine::Update()
{
	if (IsWindowResized())
	{
		m_ScreenWidth  = GetScreenWidth();
		m_ScreenHeight = GetScreenHeight();

		// Uniform scale = min(width-scale, height-scale). Lets the GUI canvas keep
		// its native aspect; the leftover screen-space becomes letterbox bars.
		extern float g_DrawScale;
		extern float g_LetterboxX;
		extern float g_LetterboxY;
		if (m_RenderWidth > 0 && m_RenderHeight > 0)
		{
			float sx = m_ScreenWidth  / m_RenderWidth;
			float sy = m_ScreenHeight / m_RenderHeight;
			g_DrawScale = std::min(sx, sy);
			g_LetterboxX = (m_ScreenWidth  - m_RenderWidth  * g_DrawScale) * 0.5f;
			g_LetterboxY = (m_ScreenHeight - m_RenderHeight * g_DrawScale) * 0.5f;
		}
	}

	g_InputSystem->Update();
	g_ResourceManager->Update();
	g_StateMachine->Update();
	g_ScriptingSystem->Update();
	g_SoundSystem->Update();

	if (WindowShouldClose())
	{
		m_Done = true;
	}

	// F12 takes a screenshot
	if (IsKeyPressed(KEY_F12))
	{
		CaptureScreenshot();
	}

	// Alt+Enter toggles borderless windowed fullscreen.
	if ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER))
	{
		ToggleBorderlessWindowed();
	}

	// F9 toggles the debug drawing
	if (IsKeyPressed(KEY_F9))
	{
		m_debugDrawing = !m_debugDrawing;
	}

	++m_GameUpdates;
}

void Engine::Draw()
{
	BeginDrawing();
	ClearBackground(BLACK);
	g_ResourceManager->Draw();
	g_StateMachine->Draw();
	g_ScriptingSystem->Draw();
	g_InputSystem->Draw();
	EndDrawing();
}

void Engine::CaptureScreenshot()
{
	char filename[40];
	struct tm *timenow;

	time_t now = time(NULL);
	timenow = gmtime(&now);

	strftime(filename, sizeof(filename), "screenshot_%Y-%m-%d_%H_%M_%S.png", timenow);
	TakeScreenshot(filename);
}
