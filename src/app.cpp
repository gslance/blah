#include <blah/app.h>
#include <blah/common.h>
#include <blah/time.h>
#include <blah/graphics/target.h>
#include "internal/platform.h"
#include "internal/graphics.h"
#include "internal/input.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

using namespace Blah;

Config::Config()
{
	name = nullptr;
	width = 0;
	height = 0;
	target_framerate = 60;
	max_updates = 5;

	on_startup = nullptr;
	on_shutdown = nullptr;
	on_update = nullptr;
	on_render = nullptr;
	on_exit_request = App::exit;
	on_log = nullptr;
}

namespace
{
	Config app_config;
	bool app_is_running = false;
	bool app_is_exiting = false;
	u64 time_last;
	u64 time_accumulator = 0;

	void app_iterate()
	{
		// update at a fixed timerate
		// TODO: allow a non-fixed step update?
		{
			u64 time_target = (u64)((1.0 / app_config.target_framerate) * Time::ticks_per_second);
			u64 time_curr = Platform::ticks();
			u64 time_diff = time_curr - time_last;
			time_last = time_curr;
			time_accumulator += time_diff;

			// do not let us run too fast
			while (time_accumulator < time_target)
			{
				int milliseconds = (int)(time_target - time_accumulator) / (Time::ticks_per_second / 1000);
				Platform::sleep(milliseconds);

				time_curr = Platform::ticks();
				time_diff = time_curr - time_last;
				time_last = time_curr;
				time_accumulator += time_diff;
			}

			// Do not allow us to fall behind too many updates
			// (otherwise we'll get spiral of death)
			u64 time_maximum = app_config.max_updates * time_target;
			if (time_accumulator > time_maximum)
				time_accumulator = time_maximum;

			// do as many updates as we can
			while (time_accumulator >= time_target)
			{
				time_accumulator -= time_target;

				Time::delta = (1.0f / app_config.target_framerate);

				if (Time::pause_timer > 0)
				{
					Time::pause_timer -= Time::delta;
					if (Time::pause_timer <= -0.0001)
						Time::delta = -Time::pause_timer;
					else
						continue;
				}

				Time::previous_ticks = Time::ticks;
				Time::ticks += time_target;
				Time::previous_seconds = Time::seconds;
				Time::seconds += Time::delta;

				Input::update_state();
				Platform::update(Input::state);
				Input::update_bindings();
				Graphics::update();

				if (app_config.on_update != nullptr)
					app_config.on_update();
			}
		}

		// render
		{
			Graphics::before_render();

			if (app_config.on_render != nullptr)
				app_config.on_render();

			Graphics::after_render();
			Platform::present();
		}
	}

}

bool App::run(const Config* c)
{
	BLAH_ASSERT(!app_is_running, "The Application is already running");
	BLAH_ASSERT(c != nullptr, "The Application requires a valid Config");
	BLAH_ASSERT(c->name != nullptr, "The Application Name cannot be null");
	BLAH_ASSERT(c->width > 0 && c->height > 0, "The Width and Height must be larget than 0");
	BLAH_ASSERT(c->max_updates > 0, "Max Updates must be >= 1");
	BLAH_ASSERT(c->target_framerate > 0, "Target Framerate must be >= 1");

	app_config = *c;
	app_is_running = true;
	app_is_exiting = false;

	// initialize the system
	if (!Platform::init(app_config))
	{
		Log::error("Failed to initialize Platform module");
		return false;
	}

	// initialize graphics
	if (!Graphics::init())
	{
		Log::error("Failed to initialize Graphics module");
		return false;
	}

	// input
	Input::init();

	// prepare by updating input & platform once
	Input::update_state();
	Platform::update(Input::state);

	// startup
	if (app_config.on_startup != nullptr)
		app_config.on_startup();

	time_last = Platform::ticks();
	time_accumulator = 0;

	// display window
	Platform::ready();

	// Begin main loop
	// Emscripten requires the main loop be separated into its own call

#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop(app_iterate, 0, 1);
#else
	while (!app_is_exiting)
		app_iterate();
#endif

	// shutdown
	if (app_config.on_shutdown != nullptr)
		app_config.on_shutdown();

	Graphics::shutdown();
	Platform::shutdown();

	// clear static state
	app_is_running = false;
	app_is_exiting = false;

	Time::ticks = 0;
	Time::seconds = 0;
	Time::previous_ticks = 0;
	Time::previous_seconds = 0;
	Time::delta = 0;

	return true;
}

void App::exit()
{
	if (!app_is_exiting && app_is_running)
		app_is_exiting = true;
}

const Config& App::config()
{
	return app_config;
}

const char* App::path()
{
	return Platform::app_path();
}

const char* App::user_path()
{
	return Platform::user_path();
}

const char* App::get_title()
{
	return Platform::get_title();
}

void App::set_title(const char* title)
{
	Platform::set_title(title);
}

Point App::get_position()
{
	Point result;
	Platform::get_position(&result.x, &result.y);
	return result;
}

void App::set_position(Point point)
{
	Platform::set_position(point.x, point.y);
}

Point App::get_size()
{
	Point result;
	Platform::get_size(&result.x, &result.y);
	return result;
}

void App::set_size(Point point)
{
	Platform::set_size(point.x, point.y);
}

int App::width()
{
	return get_size().x;
}

int App::height()
{
	return get_size().y;
}

int App::draw_width()
{
	int w, h;
	Platform::get_draw_size(&w, &h);
	return w;
}

int App::draw_height()
{
	int w, h;
	Platform::get_draw_size(&w, &h);
	return h;
}

float App::content_scale()
{
	return Platform::get_content_scale();
}

void App::fullscreen(bool enabled)
{
	Platform::set_fullscreen(enabled);
}

Renderer App::renderer()
{
	return Graphics::renderer();
}

const RendererFeatures& Blah::App::renderer_features()
{
	return Graphics::features();
}

namespace
{
	// A dummy Frame Buffer that represents the Back Buffer
	// it doesn't actually contain any textures or details.
	class BackBuffer final : public Target
	{
		Attachments empty_textures;

		Attachments& textures() override
		{
			BLAH_ASSERT(false, "Backbuffer doesn't have any textures");
			return empty_textures;
		}

		const Attachments& textures() const override
		{
			BLAH_ASSERT(false, "Backbuffer doesn't have any textures");
			return empty_textures;
		}

		int width() const override
		{
			return App::draw_width();
		}

		int height() const override
		{
			return App::draw_height();
		}

		void clear(Color color, float depth, u8 stencil, ClearMask mask) override
		{
			Graphics::clear_backbuffer(color, depth, stencil, mask);
		}
	};

}

const TargetRef App::backbuffer = TargetRef(new BackBuffer());