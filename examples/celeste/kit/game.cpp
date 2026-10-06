// Celeste Classic on the kit: ccleste's game code (../game/celeste.c, a C port of the
// PICO-8 cartridge by Maddy Thorson and Noel Berry) behind the kit's contract.
//
// The cartridge draws through sixteen PICO-8 calls, which land here: a 128x128 picture
// in the PICO-8 palette, rasterised on the CPU as the SDL front end of ccleste did.
// The picture is drawn with the kit's Vulkan renderer as rectangles of one colour,
// every PICO-8 pixel an 8x8 block of its 1920x1080 canvas: on the console's 3840x2160
// surface a block is 16 pixels on whole pixels, so it is sharp however the sampler
// filters. Its sound goes through the kit's mixer: the cartridge's effects as clips,
// and its five songs as a stream, on the console's audio output.
//
// Copyright (C) 2026 Mihawk
// SPDX-License-Identifier: GPL-3.0-or-later

#include "kit.hpp"

#include "platform.h"
#include "ps5_samples.h"
#include "third_party/stb/vorbis.h"

#include <array>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <vector>

// The cartridge's own files; the C header guards its names with extern "C"
#include "../game/celeste.h"
#include "../game/tilemap.h"

#ifndef PS5_APP_ROOT
#define PS5_APP_ROOT "/app0"
#endif

namespace {

using namespace hui;

constexpr int kSize = 128;     // the PICO-8 screen
constexpr std::uint32_t kBlack = 0xff000000u; // the picture's black, RGBA8
constexpr float kBlock = 8.0f; // virtual pixels (of the kit's 1920x1080) in one PICO-8 pixel
constexpr float kStep = 1.0f / 30.0f; // the cartridge runs at 30 updates a second
constexpr float kMusicGain = 0.8f;

// ------------------------------------------------------------------ the picture

const std::uint32_t kBasePalette[16] = {
	0x000000, 0x1d2b53, 0x7e2553, 0x008751, 0xab5236, 0x5f574f, 0xc2c3c7, 0xfff1e8,
	0xff004d, 0xffa300, 0xffec27, 0x00e436, 0x29adff, 0x83769c, 0xff77a8, 0xffccaa,
};

// An indexed BMP (1, 4 or 8 bits a pixel) as one palette index per pixel, top row first
struct Bitmap {
	int width = 0;
	int height = 0;
	std::vector<std::uint8_t> pixels;

	std::uint8_t at(int x, int y) const
	{
		return x >= 0 && y >= 0 && x < width && y < height ? pixels[(size_t)y * width + x] : 0;
	}
};

std::uint32_t le32(const std::string &d, size_t at)
{
	return (std::uint8_t)d[at] | ((std::uint8_t)d[at + 1] << 8) | ((std::uint8_t)d[at + 2] << 16) |
		((std::uint32_t)(std::uint8_t)d[at + 3] << 24);
}

bool load_bitmap(const std::string &path, Bitmap &out)
{
	std::string d;
	if (!save::read_file(path, &d) || d.size() < 54 || d[0] != 'B' || d[1] != 'M') {
		say("celeste: %s: not a readable BMP", path.c_str());
		return false;
	}
	const std::uint32_t offset = le32(d, 10);
	const int width = (int)le32(d, 18);
	int height = (int)le32(d, 22);
	const int bits = d[28];
	const bool bottom_up = height > 0;
	height = std::abs(height);
	if (width <= 0 || height <= 0 || (bits != 1 && bits != 4 && bits != 8) || le32(d, 30) != 0) {
		say("celeste: %s: unsupported BMP (%d bits)", path.c_str(), bits);
		return false;
	}
	const size_t pitch = ((size_t)width * bits + 31) / 32 * 4;
	if (offset + pitch * height > d.size()) {
		say("celeste: %s: truncated", path.c_str());
		return false;
	}
	out.width = width;
	out.height = height;
	out.pixels.assign((size_t)width * height, 0);
	for (int y = 0; y < height; y++) {
		const size_t row = offset + pitch * (size_t)(bottom_up ? height - 1 - y : y);
		for (int x = 0; x < width; x++) {
			const std::uint8_t byte = (std::uint8_t)d[row + (size_t)x * bits / 8];
			const int shift = 8 - bits - (int)((size_t)x * bits % 8);
			out.pixels[(size_t)y * width + x] = (byte >> shift) & ((1 << bits) - 1);
		}
	}
	return true;
}

// -------------------------------------------------------------------- the sound

// A mono or stereo 16-bit PCM WAV as stereo floats at 48 kHz
bool load_wav(const std::string &path, std::vector<float> &stereo)
{
	std::string d;
	if (!save::read_file(path, &d) || d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 ||
		std::memcmp(d.data() + 8, "WAVE", 4) != 0) {
		return false;
	}
	int channels = 0, bits = 0;
	std::uint32_t rate = 0;
	size_t data = 0, length = 0;
	for (size_t at = 12; at + 8 <= d.size();) {
		const size_t size = le32(d, at + 4);
		if (std::memcmp(d.data() + at, "fmt ", 4) == 0 && at + 24 <= d.size()) {
			channels = (std::uint8_t)d[at + 10];
			rate = le32(d, at + 12);
			bits = (std::uint8_t)d[at + 22];
		} else if (std::memcmp(d.data() + at, "data", 4) == 0) {
			data = at + 8;
			length = std::min(size, d.size() - data);
			break;
		}
		at += 8 + size + (size & 1);
	}
	if (channels < 1 || channels > 2 || bits != 16 || rate == 0 || length == 0) {
		return false;
	}
	const size_t frames = length / 2 / channels;
	auto sample = [&](size_t frame, int channel) {
		const char *p = d.data() + data + (frame * channels + channel) * 2;
		return (float)(std::int16_t)((std::uint8_t)p[0] | ((std::uint8_t)p[1] << 8)) / 32768.0f;
	};
	const size_t out_frames = (size_t)((double)frames * audio::kSampleRate / rate);
	stereo.assign(out_frames * 2, 0.0f);
	for (size_t i = 0; i < out_frames; i++) {
		const double at = (double)i * rate / audio::kSampleRate;
		const size_t a = std::min((size_t)at, frames - 1);
		const size_t b = std::min(a + 1, frames - 1);
		const float t = (float)(at - (double)a);
		for (int c = 0; c < 2; c++) {
			const int src = std::min(c, channels - 1);
			stereo[i * 2 + c] = sample(a, src) + (sample(b, src) - sample(a, src)) * t;
		}
	}
	return true;
}

// The cartridge's effects and songs, and what is playing of them
class Sound {
public:
	explicit Sound(audio::Mixer &mixer) : mixer_(mixer) {}
	~Sound() { stop_all(); }

	void load(const std::string &folder)
	{
		static const int ids[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 13, 14, 15, 16, 23, 35, 37, 38, 40, 50, 51, 54, 55 };
		int loaded = 0;
		for (const int id : ids) {
			std::vector<float> stereo;
			if (!load_wav(folder + "/snd" + std::to_string(id) + ".wav", stereo)) {
				say("celeste: snd%d.wav is missing or not 16-bit PCM", id);
				continue;
			}
			effects_[id].samples = std::move(stereo);
			effects_[id].clip = { effects_[id].samples.data(), effects_[id].samples.size() / 2 };
			loaded++;
		}
		int songs = 0;
		for (int i = 0; i < 5; i++) {
			std::string file;
			Song &song = songs_[i];
			if (!save::read_file(folder + "/mus" + std::to_string(i * 10) + ".ogg", &file, 8u << 20)) {
				continue;
			}
			short *pcm = nullptr;
			const int frames = stb_vorbis_decode_memory((const unsigned char *)file.data(), (int)file.size(),
				&song.channels, &song.rate, &pcm);
			if (frames > 1 && pcm != nullptr && song.channels >= 1 && song.channels <= 2) {
				song.pcm.assign(pcm, pcm + (size_t)frames * song.channels);
				songs++;
			}
			std::free(pcm);
		}
		say("celeste: %d effects, %d songs", loaded, songs);
	}

	void sfx(int id)
	{
		if (id >= 0 && id < (int)effects_.size() && effects_[id].clip.samples != nullptr) {
			mixer_.play_clip(&effects_[id].clip, audio::PlayParams{ 1.0f, 1.0f, 0.0f, audio::Bus::sfx });
		}
	}

	// music(index, fade ms): a song from its start, faded in; -1 fades the music out
	void music(int index, int fade_ms)
	{
		const float fade = (float)std::max(fade_ms, 0) / 1000.0f;
		if (index < 0) {
			if (current_ < 0) {
				return;
			}
			if (fade <= 0.0f) {
				silence();
			} else {
				mixer_.set_stream_gain(0, 0.0f, fade);
				fading_out_ = fade;
			}
			return;
		}
		const int song = index / 10;
		if (song >= (int)songs_.size() || songs_[song].pcm.empty()) {
			return;
		}
		retire(std::move(ring_));
		ring_ = std::make_unique<audio::StreamRing>(1u << 16);
		current_ = song;
		position_ = 0.0;
		fading_out_ = 0.0f;
		mixer_.swap_stream(0, ring_.get());
		mixer_.set_stream_gain(0, 0.0f, 0.0f);
		mixer_.set_stream_gain(0, kMusicGain, fade);
		pump(0.0f);
	}

	void pause(bool paused)
	{
		paused_ = paused;
		if (current_ >= 0 && fading_out_ <= 0.0f) {
			mixer_.set_stream_gain(0, paused ? 0.0f : kMusicGain, 0.1f);
		}
	}

	// Once a frame: keeps the song decoded a little ahead of the audio thread
	void pump(float dt)
	{
		for (auto it = retired_.begin(); it != retired_.end();) {
			it->age += dt;
			it = it->age > 0.5f ? retired_.erase(it) : it + 1;
		}
		if (fading_out_ > 0.0f) {
			fading_out_ -= dt;
			if (fading_out_ <= 0.0f) {
				silence();
			}
		}
		if (current_ < 0 || !ring_ || paused_) {
			return;
		}
		const Song &song = songs_[current_];
		const size_t total = song.pcm.size() / song.channels;
		const double step = (double)song.rate / audio::kSampleRate;
		float chunk[1024 * 2];
		while (ring_->available() < 24000 && ring_->space() >= 1024) {
			for (int i = 0; i < 1024; i++) {
				const size_t a = (size_t)position_;
				const size_t b = (a + 1) % total;
				const float t = (float)(position_ - (double)a);
				for (int c = 0; c < 2; c++) {
					const int ch = std::min(c, song.channels - 1);
					const float x = song.pcm[a * song.channels + ch];
					const float y = song.pcm[b * song.channels + ch];
					chunk[i * 2 + c] = (x + (y - x) * t) / 32768.0f;
				}
				position_ += step;
				if (position_ >= (double)total) {
					position_ -= (double)total;
				}
			}
			ring_->write(chunk, 1024);
		}
	}

	// Silence at once, and a pause long enough for the audio thread to let go of what it
	// played from, since these clips and rings are about to go
	void stop_all()
	{
		mixer_.stop_all();
		silence();
		retired_.clear();
		sleep_for_audio_thread();
	}

private:
	struct Effect {
		std::vector<float> samples;
		audio::Clip clip;
	};
	struct Song {
		std::vector<std::int16_t> pcm;
		int channels = 1;
		int rate = 22050;
	};
	struct Retired {
		std::unique_ptr<audio::StreamRing> ring;
		float age = 0.0f;
	};

	static void sleep_for_audio_thread()
	{
		const timespec pause{ 0, 30 * 1000 * 1000 };
		nanosleep(&pause, nullptr);
	}

	void retire(std::unique_ptr<audio::StreamRing> ring)
	{
		if (ring) {
			retired_.push_back({ std::move(ring), 0.0f });
		}
	}

	void silence()
	{
		mixer_.swap_stream(0, nullptr);
		retire(std::move(ring_));
		current_ = -1;
		fading_out_ = 0.0f;
	}

	audio::Mixer &mixer_;
	std::array<Effect, 64> effects_;
	std::array<Song, 5> songs_;
	std::unique_ptr<audio::StreamRing> ring_;
	std::vector<Retired> retired_;
	int current_ = -1;
	double position_ = 0.0; // frames into the song
	float fading_out_ = 0.0f;
	bool paused_ = false;
};

// ------------------------------------------------------------------- the game

class Game final : public app::Concept {
public:
	static Game *instance;

	explicit Game(ps5ui::Kit &kit) : kit_(kit), sound_(kit.mixer)
	{
		instance = this;
		const std::string folder = kit.assets + "/../celeste";
		ready_ = load_bitmap(folder + "/gfx.bmp", sprites_) && load_bitmap(folder + "/font.bmp", font_);
		sound_.load(folder);
		for (int i = 0; i < 16; i++) {
			// RGBA8 with red in the lowest byte, as the picture keeps it
			base_palette_[i] = 0xff000000u | ((kBasePalette[i] & 0xff) << 16) | (kBasePalette[i] & 0xff00) |
				((kBasePalette[i] >> 16) & 0xff);
		}
		reset_palette();
		picture_.assign((size_t)kSize * kSize, 0xff000000u);
		Celeste_P8_set_call_func(reinterpret_cast<Celeste_P8_cb_func_t>(&Game::call));
		Celeste_P8_set_rndseed((unsigned)(std::time(nullptr) ^ (std::uint64_t)(now_seconds() * 1e3)));
		Celeste_P8_init();
	}

	~Game() override
	{
		sound_.stop_all();
		instance = nullptr;
	}

	const app::ConceptInfo &info() const override
	{
		static const app::ConceptInfo info{ "celeste", "Celeste Classic", "The PICO-8 cartridge on the kit",
			"examples/celeste/kit/game.cpp", audio::SoundSet::glass, gfx::Color::rgb(0xff004d), {} };
		return info;
	}

	void update(const InputFrame &, float dt, app::Feedback &feedback) override
	{
		sound_.pump(dt);
		if (!ps5TestRun) {
			read_pad();
		}
		if (pause_pressed_) {
			pause_pressed_ = false;
			paused_ = !paused_;
			sound_.pause(paused_);
		}
		clock_ += dt;
		int steps = 0;
		while (clock_ >= kStep && steps < 2) {
			clock_ -= kStep;
			steps++;
			step();
			sliding_ = !paused_ && Celeste_P8_is_wall_sliding();
		}
		if (clock_ > kStep) {
			clock_ = 0.0f; // a stall is not caught up with
		}
		if (steps > 0) {
			build_runs();
		}
		if (death_) {
			death_ = false;
			feedback.rumble(0.8f, 0.25f);
		}
		// A faint tick every 0.2 s of a wall slide
		slide_clock_ = sliding_ ? slide_clock_ - dt : 0.0f;
		if (sliding_ && slide_clock_ <= 0.0f) {
			slide_clock_ = 0.2f;
			feedback.rumble(0.1f, 0.04f);
		}
		if (dash_) {
			dash_ = false;
			feedback.rumble(0.05f, 0.25f); // a light tick; a death's rumble wins if both happen
		}
	}

	void draw(app::Frame &frame) const override
	{
		// The picture is drawn as rectangles, so it is as sharp as the surface is
		// large (3840x2160 on the console: a block is 16 pixels on whole pixels,
		// which the kit's shader fills exactly, with nothing to blend)
		gfx::DrawList &list = frame.scene;
		const float side = kSize * kBlock;
		const float left = (gfx::kVirtualWidth - side) * 0.5f, top = (gfx::kVirtualHeight - side) * 0.5f;
		for (const Run &run : runs_) {
			list.rounded_rect({ left + run.x * kBlock, top + run.y * kBlock, run.w * kBlock, run.h * kBlock }, 0.0f,
				gfx::Color{ (float)(run.rgba & 0xff) / 255.0f, (float)((run.rgba >> 8) & 0xff) / 255.0f,
					(float)((run.rgba >> 16) & 0xff) / 255.0f, 1.0f });
		}
	}

private:
	// ---- input: the PICO-8 buttons are bits 0 left, 1 right, 2 up, 3 down, 4 jump, 5 dash
	static std::uint16_t buttons_of(std::uint32_t pad_bits)
	{
		std::uint16_t b = 0;
		if (pad_bits & PAD_LEFT) b |= 1 << 0;
		if (pad_bits & PAD_RIGHT) b |= 1 << 1;
		if (pad_bits & PAD_UP) b |= 1 << 2;
		if (pad_bits & PAD_DOWN) b |= 1 << 3;
		if (pad_bits & (PAD_CROSS | PAD_TRIANGLE)) b |= 1 << 4;
		if (pad_bits & (PAD_SQUARE | PAD_CIRCLE)) b |= 1 << 5;
		return b;
	}

	void read_pad()
	{
		const pad &p = ps5_pad();
		std::uint16_t held = buttons_of(p.held);
		if (p.left_x < -0.5f) held |= 1 << 0;
		if (p.left_x > 0.5f) held |= 1 << 1;
		if (p.left_y < -0.5f) held |= 1 << 2;
		if (p.left_y > 0.5f) held |= 1 << 3;
		held_ = held;
		// A tap shorter than a game step still counts as one step of the button
		tapped_ |= buttons_of(p.pressed);
		if (p.pressed & PAD_OPTIONS) {
			pause_pressed_ = true;
		}
		if (p.pressed & PAD_L1) {
			screenshake_ = !screenshake_;
		}
	}

	void step()
	{
		buttons_ = held_ | tapped_;
		tapped_ = 0;
		if (paused_) {
			const int x0 = kSize / 2 - 12, y0 = 8;
			fill_rect(x0 - 1, y0 - 1, 6 * 4 + x0 + 1, 6 + y0 + 1, 6);
			fill_rect(x0, y0, 6 * 4 + x0, 6 + y0, 0);
			print("paused", x0 + 1, y0 + 1, 7);
		} else {
			Celeste_P8_update();
			Celeste_P8_draw();
		}
	}

	// The picture as the fewest rectangles of one colour that a greedy pass finds: runs
	// along a row, joined to the run above when it spans the same columns. Black is
	// the screen's clear colour, so it is not drawn.
	void build_runs()
	{
		runs_.clear();
		int open[kSize]; // the run starting at each column in the row above, or -1
		std::fill_n(open, kSize, -1);
		for (int y = 0; y < kSize; y++) {
			int next[kSize];
			std::fill_n(next, kSize, -1);
			for (int x = 0; x < kSize;) {
				const std::uint32_t c = picture_[(size_t)y * kSize + x];
				int end = x + 1;
				while (end < kSize && picture_[(size_t)y * kSize + end] == c) {
					end++;
				}
				if (c != kBlack) {
					const int above = open[x];
					if (above >= 0 && runs_[above].w == end - x && runs_[above].rgba == c) {
						runs_[above].h++;
						next[x] = above;
					} else {
						runs_.push_back({ x, y, end - x, 1, c });
						next[x] = (int)runs_.size() - 1;
					}
				}
				x = end;
			}
			std::copy_n(next, kSize, open);
		}
	}

	// ---- the PICO-8 drawing calls
	std::uint32_t color(int index) const { return palette_[index & 15]; }

	void reset_palette()
	{
		std::copy(base_palette_, base_palette_ + 16, palette_);
	}

	void plot(int x, int y, std::uint32_t c)
	{
		if ((unsigned)x < (unsigned)kSize && (unsigned)y < (unsigned)kSize) {
			picture_[(size_t)y * kSize + x] = c;
		}
	}

	void fill_rect(int x0, int y0, int x1, int y1, int col)
	{
		const std::uint32_t c = color(col);
		for (int y = std::max(y0, 0); y <= std::min(y1, kSize - 1); y++) {
			for (int x = std::max(x0, 0); x <= std::min(x1, kSize - 1); x++) {
				picture_[(size_t)y * kSize + x] = c;
			}
		}
	}

	// An 8x8 cell of a sheet: its set pixels drawn in their own palette colour, or in col
	void blit(const Bitmap &sheet, int cell, int x, int y, int col, bool flip_x, bool flip_y)
	{
		const int sx = 8 * (cell % 16), sy = 8 * (cell / 16);
		for (int j = 0; j < 8; j++) {
			for (int i = 0; i < 8; i++) {
				const std::uint8_t p = sheet.at(sx + (flip_x ? 7 - i : i), sy + (flip_y ? 7 - j : j));
				if (p != 0) {
					plot(x + i, y + j, color(col >= 0 ? col : p));
				}
			}
		}
	}

	void print(const char *text, int x, int y, int col)
	{
		for (; *text; text++, x += 4) {
			blit(font_, *text & 0x7f, x, y, col, false, false);
		}
	}

	void line(int x0, int y0, int x1, int y1, int col)
	{
		const std::uint32_t c = color(col);
		const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
		if (!dx && !dy) {
			return;
		}
		const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
		int err = dx - dy;
		while (x0 != x1 || y0 != y1) {
			plot(x0, y0, c);
			const int e2 = 2 * err;
			if (e2 > -dy) {
				err -= dy;
				x0 += sx;
			}
			if (e2 < dx) {
				err += dx;
				y0 += sy;
			}
		}
	}

	void circle_fill(int cx, int cy, int r, int col)
	{
		if (r <= 3) {
			// The cartridge only uses the small ones: a plus sign, then a rounded square
			const int inner = r <= 1 ? 1 : r <= 2 ? 2 : 3;
			fill_rect(cx - inner, cy - (r <= 1 ? 0 : 1), cx + inner, cy + (r <= 1 ? 0 : 1), col);
			fill_rect(cx - (r <= 1 ? 0 : 1), cy - inner, cx + (r <= 1 ? 0 : 1), cy + inner, col);
			if (r == 3) {
				fill_rect(cx - 2, cy - 2, cx + 2, cy + 2, col);
			}
			return;
		}
		int f = 1 - r, ddf_x = 1, ddf_y = -2 * r, x = 0, y = r;
		line(cx, cy - y, cx, cy + r, col);
		line(cx + r, cy, cx - r, cy, col);
		while (x < y) {
			if (f >= 0) {
				y--;
				ddf_y += 2;
				f += ddf_y;
			}
			x++;
			ddf_x += 2;
			f += ddf_x;
			line(cx + x, cy + y, cx - x, cy + y, col);
			line(cx + x, cy - y, cx - x, cy - y, col);
			line(cx + y, cy + x, cx - y, cy + x, col);
			line(cx + y, cy - x, cx - y, cy - x, col);
		}
	}

	static int tile_flag(int tile, int flag)
	{
		return tile < (int)sizeof(tile_flags) && (tile_flags[tile] & (1 << flag)) != 0;
	}

	// The cartridge calls the console through this one function
	static int call(int what, ...)
	{
		Game &g = *instance;
		if (!g.screenshake_) {
			g.camera_x_ = g.camera_y_ = 0;
		}
		va_list args;
		va_start(args, what);
		int result = 0;
		switch ((CELESTE_P8_CALLBACK_TYPE)what) {
		case CELESTE_P8_MUSIC: {
			const int index = va_arg(args, int);
			const int fade = va_arg(args, int);
			g.sound_.music(index, fade);
			break;
		}
		case CELESTE_P8_SPR: {
			const int sprite = va_arg(args, int);
			const int x = va_arg(args, int);
			const int y = va_arg(args, int);
			va_arg(args, int); // columns and rows: the cartridge only draws single sprites
			va_arg(args, int);
			const bool flip_x = va_arg(args, int) != 0;
			const bool flip_y = va_arg(args, int) != 0;
			if (sprite >= 0) {
				g.blit(g.sprites_, sprite, x - g.camera_x_, y - g.camera_y_, -1, flip_x, flip_y);
			}
			break;
		}
		case CELESTE_P8_BTN: {
			const int b = va_arg(args, int);
			result = (g.buttons_ >> b) & 1;
			break;
		}
		case CELESTE_P8_SFX: {
			const int id = va_arg(args, int);
			g.sound_.sfx(id);
			if (id == 0) {
				g.death_ = true;
			} else if (id == 3) {
				g.dash_ = true; // the dash's own sound
			}
			break;
		}
		case CELESTE_P8_PAL: {
			const int a = va_arg(args, int);
			const int b = va_arg(args, int);
			if (a >= 0 && a < 16 && b >= 0 && b < 16) {
				g.palette_[a] = g.base_palette_[b];
			}
			break;
		}
		case CELESTE_P8_PAL_RESET:
			g.reset_palette();
			break;
		case CELESTE_P8_CIRCFILL: {
			const int cx = va_arg(args, int) - g.camera_x_;
			const int cy = va_arg(args, int) - g.camera_y_;
			const int r = va_arg(args, int);
			const int col = va_arg(args, int);
			g.circle_fill(cx, cy, r, col);
			break;
		}
		case CELESTE_P8_PRINT: {
			const char *text = va_arg(args, const char *);
			const int x = va_arg(args, int) - g.camera_x_;
			const int y = va_arg(args, int) - g.camera_y_;
			const int col = va_arg(args, int) % 16;
			g.print(text, x, y, col);
			break;
		}
		case CELESTE_P8_RECTFILL: {
			const int x0 = va_arg(args, int) - g.camera_x_;
			const int y0 = va_arg(args, int) - g.camera_y_;
			const int x1 = va_arg(args, int) - g.camera_x_;
			const int y1 = va_arg(args, int) - g.camera_y_;
			const int col = va_arg(args, int);
			g.fill_rect(x0, y0, x1, y1, col);
			break;
		}
		case CELESTE_P8_LINE: {
			const int x0 = va_arg(args, int) - g.camera_x_;
			const int y0 = va_arg(args, int) - g.camera_y_;
			const int x1 = va_arg(args, int) - g.camera_x_;
			const int y1 = va_arg(args, int) - g.camera_y_;
			const int col = va_arg(args, int);
			g.line(x0, y0, x1, y1, col);
			break;
		}
		case CELESTE_P8_MGET: {
			const int tx = va_arg(args, int);
			const int ty = va_arg(args, int);
			result = tilemap_data[tx + ty * 128];
			break;
		}
		case CELESTE_P8_CAMERA: {
			if (g.screenshake_) {
				g.camera_x_ = va_arg(args, int);
				g.camera_y_ = va_arg(args, int);
			}
			break;
		}
		case CELESTE_P8_FGET: {
			const int tile = va_arg(args, int);
			const int flag = va_arg(args, int);
			result = tile_flag(tile, flag);
			break;
		}
		case CELESTE_P8_MAP: {
			const int mx = va_arg(args, int), my = va_arg(args, int);
			const int tx = va_arg(args, int), ty = va_arg(args, int);
			const int mw = va_arg(args, int), mh = va_arg(args, int);
			const int mask = va_arg(args, int);
			for (int x = 0; x < mw; x++) {
				for (int y = 0; y < mh; y++) {
					const int tile = tilemap_data[x + mx + (y + my) * 128];
					if (mask == 0 || (mask == 4 && tile_flags[tile] == 4) || tile_flag(tile, mask != 4 ? mask - 1 : mask)) {
						g.blit(g.sprites_, tile, tx + x * 8 - g.camera_x_, ty + y * 8 - g.camera_y_, -1, false, false);
					}
				}
			}
			break;
		}
		}
		va_end(args);
		return result;
	}

	ps5ui::Kit &kit_;
	Sound sound_;
	Bitmap sprites_, font_;
	bool ready_ = false;
	std::uint32_t base_palette_[16] = {};
	std::uint32_t palette_[16] = {};
	std::vector<std::uint32_t> picture_;
	struct Run {
		int x, y, w, h;
		std::uint32_t rgba;
	};
	std::vector<Run> runs_;
	int camera_x_ = 0, camera_y_ = 0;
	bool screenshake_ = true;
	bool paused_ = false;
	bool pause_pressed_ = false;
	bool death_ = false;
	bool dash_ = false;
	bool sliding_ = false;
	float slide_clock_ = 0.0f;
	std::uint16_t held_ = 0, tapped_ = 0, buttons_ = 0;
	float clock_ = 0.0f;
};

Game *Game::instance = nullptr;

} // namespace

namespace ps5ui {

std::unique_ptr<hui::app::Concept> make_screen(hui::app::Context &, Kit &kit)
{
	return std::make_unique<Game>(kit);
}

} // namespace ps5ui
