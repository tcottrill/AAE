#ifndef CONFIG_H
#define CONFIG_H

// Renderer backend selection ([main] renderer= in aae.ini, -renderer on the
// command line). Resolved once at startup; no runtime switching (Phase 4 spec §3.1).
#define RENDERER_OPENGL 0
#define RENDERER_VULKAN 1

typedef struct {
	char rompath[256];
	char samplepath[256];
	char artpath[256];
	// (Old kreset/ktest/ktestadv/kpause/ksnap fields removed 2026-05-29 as
	// part of the UI-key remap refactor — they had no consumers. UI hotkeys
	// now live as IPT_UI_* rows in inputport_defaults[] and persist to
	// default.cfg via the same path as game inputs.)

	int drawzero;
	int widescreen;
	int overlay;
	int colordepth;
	int screenw;
	int screenh;
	int windowed;
	int language;
	int translucent;
	float translevel;
	int lives;

	int m_line;
	int m_point;
	int monitor;

	float linewidth;
	float pointsize;

	int gamma;
	int bright;
	int contrast;
	int gain;
	int fire_point_size;
	int explode_point_size;
	//int colorhack;
	int shotsize;
	int cocktail;
	int mainvol;
	int pokeyvol;
	int artwork;
	int bezel;
	int burnin;
	int artcrop;
	int vid_rotate;
	int vecglow;
	int vectrail;

	// --- Modern vector beam renderer (menu + aae.ini; per-game overridable) ---
	float line_smoothing;   // edge AA feather in px (0.4..2.0)
	float corner_strength;  // beam corner/point disc size (0.3..2.5)
	int   shots_textured;   // 0 = procedural shots, 1 = legacy textured shots

	int psnoise;
	int hvnoise;
	int pshiss;
	int noisevol;
	int snappng;

	char* aspect;
	float prescale;
	int anisfilter;
	int priority;
	int forcesync;
	int dblbuffer;
	int showinfo; //Show readme info message
	char* exrompath; //optional path for roms
	char* exartpath; // optional path for artwork files
	int hack;
	int cheat;
	int debug;
	int renderer;         // RENDERER_VULKAN (default) or RENDERER_OPENGL
	// The VIDEO menu's RENDERER item edits THIS, never `renderer` above.
	// init_gl() latches s_active from config.renderer on EVERY game load, so
	// changing the live value mid-session would try to switch chains at the
	// next launch - impossible, because the window and its context were
	// created for one API at startup (winmain's EarlyRendererIsVulkan). This
	// pending value is written to aae.ini on menu exit and only takes effect
	// on the next run, which is exactly what the menu tells the user.
	int renderer_pending;
	int vk_validation;    // [main] vk_validation: 1 = enable Vulkan validation layer (default 0)
	// [main] vk_ssaa: supersample factor for the VULKAN vector beam target.
	// The beam render target is 1024 * vk_ssaa square, and the composite's
	// trilinear minification resolves it. 1 (default) matches the GL chain,
	// whose beam_init(1) has always used a plain 1024 buffer; 2 is visibly
	// smoother but costs 4x the beam fill AND a 4x larger per-frame mip
	// cascade - measured as roughly a 6x frame-rate difference against GL on
	// the same hardware. Read once when the vector post chain initialises,
	// so a change takes effect at the next launch.
	int vk_ssaa;
	// [main] vk_profile: 1 = GPU timestamp profiler for the VULKAN chain
	// (default 0). Wraps each pass of the frame in a VkQueryPool timestamp
	// pair and logs a per-section average-ms / %-of-frame summary to
	// systemlog.txt every 120 measured frames. Read once when the Vulkan
	// chain initialises, so a change takes effect at the next launch.
	// At 0 no query pool is created and not one timestamp is recorded - the
	// command stream is byte-identical to a build without the profiler.
	int vk_profile;
	int debug_profile_code;
	int audio_force_resample;
	int kbleds;
	int samplerate;
	// Output speaker request, [main] speakers: 2 = stereo (default - the
	// Dolphin model: the sound server / soundbar fills the room from clean
	// stereo), 6 = discrete 5.1 with AAE's own pseudo-surround upmix (for
	// hardware that genuinely takes 6-channel PCM), 0 = auto-negotiate.
	// Linux backend only; Windows XAudio2 output is unaffected.
	int speakers;
	// Matrix surround ENCODE for the stereo path, [main] surround_encode
	// (default 1). Arcade audio is near-mono, and every difference-driven
	// upmixer (PipeWire psd, Pro Logic, soundbar processing) derives rears
	// from L-R - which is ~zero for mono content, so the rears stay silent
	// no matter how good the chain is (measured: Dolphin's rich-stereo games
	// light the same upmixer up; our mono content doesn't). This injects a
	// delayed mono ambience ANTIPHASE into L/R, giving those decoders real
	// difference content to send rearward. 0 = plain untouched stereo.
	int surround_encode;

	char* raster_effect;
	// Display aspect override: "AUTO" (natural computed aspect, the default)
	// or "W:H" (e.g. "4:3", "3:4"). [main] game_aspect; per-game overridable.
	char* game_aspect;

	// --- NEW FIELDS for performance options ---
	int useMMCSS;         // 1 = enable MMCSS for main thread
	int boostThread;      // 1 = boost thread priority above normal
	int setTimerRes;      // 1 = enable 1ms timer resolution
	int preventSleep;     // 1 = prevent system sleep during gameplay
	// --- Exit confirmation dialog ---
	// 1 = show YES/NO prompt before exiting (default)
	// 0 = exit immediately with no prompt
	int confirm_exit;
	int flip_gui_controls;
	int starting_monitor;
	

	// --- Mono monitor CRT effect (B/W raster games only) ---
	// Ported from the PET emulator's mono monitor shader. Applied as a
	// post-process on img5a when the driver has VIDEO_TYPE_RASTER_BW.
	// Persisted in aae.ini [monitormono]; per-game ini can override.
	int   mono_enable;            // 0 = off, 1 = on
	float mono_blur_h;            // horizontal beam sigma, source px (0..2.5)
	float mono_blur_v;            // vertical beam sigma, source px (0..1)
	float mono_halation;          // glow strength (0..1)
	float mono_halation_radius;   // glow radius, source px (1..16)
	float mono_scanline;          // beam ripple strength (0..1)
	float mono_contrast;          // video gain / beam overdrive (1..3)
	float mono_brightness;        // black-level lift (0..0.25)
	int   mono_tint;              // 0 = P4 white, 1 = P1 green, 2 = P3 amber

	// --- Multi-mouse: per-player device assignment ---
	// -2 = none, -1 = system (all mice merged, the legacy behavior),
	// 0..RI_MAX_MICE-1 = a specific Raw Input mouse device.
	// Persisted in aae.ini [input] mouse_player1..4.
	int mouse_player[4];

	// Stable identity for specific-device assignments: the full Raw Input
	// device path ("" when the player is NONE/SYSTEM). Windows does not
	// guarantee enumeration order across boots, so when this is set it WINS
	// over the index above -- the device is found wherever it enumerated,
	// and if it is unplugged the player simply gets no input (never a wrong
	// device). Persisted in aae.ini [input] mouse_player1..4_path.
	char mouse_player_path[4][260];

	// --- Joystick: per-player device assignment ---
	// -2 = none, -1 = AUTO (joystick N drives player N, the legacy default),
	// 0.. = a specific joystick slot. Persisted in aae.ini [input]
	// joy_player1..4.
	int joy_player[4];

	// Stable identity for specific assignments ("" for NONE/AUTO): a
	// "DI:{guid}" / "XINPUT:n" / "WINMM:n" string from joystick_get_id().
	// When set it WINS over the index -- the device is found wherever it
	// currently sits, and an unplugged device means no input rather than
	// someone else's stick. DI instance GUIDs are stable per machine, so
	// two identical Ultimarc sticks stay pinned to their players across
	// reboots. Persisted in aae.ini [input] joy_player1..4_id.
	char joy_player_id[4][64];

	// --- Multi-keyboard: per-player device assignment ---
	// Same scheme as the mice. ALL players default to -1 (SYSTEM = merged),
	// which preserves the classic model of several players sharing one
	// keyboard with different keys. Assign specific devices (e.g. two
	// Ultimarc I-PACs) to route each player's GAME keys to its own encoder;
	// coin/start/service bits carry the player-1 tag and follow player 1's
	// device, and UI/menu keys always read the merged state.
	// Persisted in aae.ini [input] kbd_player1..4(+_path).
	int  kbd_player[4];
	char kbd_player_path[4][260];

	// --- Color monitor CRT effect (color raster games only) ---
	// Sibling of the mono monitor pass: same beam/halation/scanline pipeline
	// plus RGB misconvergence, saturation and shadow-mask emulation. Applied
	// on img5a when the driver has VIDEO_TYPE_RASTER_COLOR.
	// Persisted in aae.ini [monitorcolor]; per-game ini can override.
	int   color_enable;           // 0 = off, 1 = on
	float color_blur_h;           // horizontal beam sigma, source px (0..2.5)
	float color_blur_v;           // vertical beam sigma, source px (0..1)
	float color_converge;         // RGB misconvergence, source px (0..2)
	float color_halation;         // glow strength (0..1)
	float color_halation_radius;  // glow radius, source px (1..16)
	float color_scanline;         // scanline strength (0..1)
	float color_contrast;         // video gain / beam overdrive (1..3)
	float color_brightness;       // black-level lift (0..0.25)
	float color_saturation;       // 0 = grayscale, 1 = neutral, 2 = punchy
	int   color_mask_type;        // 0 = aperture grille, 1 = slot mask, 2 = dot triad
	float color_mask_strength;    // shadow-mask depth (0..1)
	float color_mask_scale;       // phosphor stripe width in output px (1..6)

	// --- System rotation (command-line -ror / -rol) ---
	// Stored as ORIENTATION_xxx flags (0 = none, ROT90, ROT180, ROT270).
	// Composed with driver rotation via XOR into Machine->orientation.
	// Persisted in aae.ini [main] system_rotation as an integer (0,1,3,5,6).
	int system_rotation;

}settings;

// This setting required c++ 17 to compile
inline settings config;

void setup_video_config();
void setup_config();
void setup_game_config();
void sanity_check_config();

void my_set_config_int(const char* section, const char* key, int val, int path);
void my_set_config_float(const char* section, const char* key, float val, int path);
void my_set_config_string(const char* section, const char* key, const char* val, int path);

// Parse a "W:H" aspect string ("4:3", "16:9"...). Returns w/h, or 0 for
// "AUTO"/empty/unparsable (callers treat 0 as "no override").
float aspect_from_string(const char* s);

#endif // CONFIG_H
