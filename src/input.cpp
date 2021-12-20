#include <blah/input.h>
#include <blah/app.h>
#include <blah/time.h>
#include <blah/common.h>
#include <blah/numerics/calc.h>
#include "internal/input.h"
#include "internal/platform.h"
#include <cstring>

using namespace Blah;

namespace
{
	InputState g_empty_state;
	ControllerState g_empty_controller;
	Vector<WeakRef<ButtonBinding>> g_buttons;
	Vector<WeakRef<AxisBinding>> g_axes;
	Vector<WeakRef<StickBinding>> g_sticks;
	String g_clipboard;
}

InputState Blah::Input::state;
InputState Blah::Input::last_state;

float Blah::Input::repeat_delay = 0.35f;
float Blah::Input::repeat_interval = 0.025f;

void Input::init()
{
	g_empty_controller.name = "Disconnected";
	for (int i = 0; i < Input::max_controllers; i++)
		g_empty_state.controllers[i].name = g_empty_controller.name;

	Input::last_state = g_empty_state;
	Input::state = g_empty_state;
	g_buttons.dispose();
	g_axes.dispose();
	g_sticks.dispose();
}

void Input::update_state()
{
	// cycle states
	Input::last_state = Input::state;

	// update other state for next frame
	for (int i = 0; i < Input::max_keyboard_keys; i++)
	{
		Input::state.keyboard.pressed[i] = false;
		Input::state.keyboard.released[i] = false;
	}

	for (int i = 0; i < Input::max_mouse_buttons; i++)
	{
		Input::state.mouse.pressed[i] = false;
		Input::state.mouse.released[i] = false;
	}

	Input::state.mouse.wheel = Point::zero;
	Input::state.keyboard.text.clear();

	for (int i = 0; i < Input::max_controllers; i++)
	{
		ControllerState& controller = Input::state.controllers[i];

		if (!controller.is_connected)
			controller.name.clear();

		for (int j = 0; j < Input::max_controller_buttons; j++)
		{
			controller.pressed[j] = false;
			controller.released[j] = false;
		}
	}

	// get clipboard
	g_clipboard = Platform::get_clipboard();
}

void Input::update_bindings()
{
	for (int i = 0; i < g_buttons.size(); i++)
	{
		if (g_buttons[i].use_count() <= 0)
		{
			g_buttons.erase(i);
			i--;
		}
		else if (auto binding = g_buttons[i].lock())
		{
			binding->update();
		}
	}

	for (int i = 0; i < g_axes.size(); i++)
	{
		if (g_axes[i].use_count() <= 0)
		{
			g_axes.erase(i);
			i--;
		}
		else if (auto binding = g_axes[i].lock())
		{
			binding->update();
		}
	}

	for (int i = 0; i < g_sticks.size(); i++)
	{
		if (g_sticks[i].use_count() <= 0)
		{
			g_sticks.erase(i);
			i--;
		}
		else if (auto binding = g_sticks[i].lock())
		{
			binding->update();
		}
	}
}

void MouseState::on_move(const Vec2f& pos, const Vec2f& screen_pos)
{
	position = pos;
	screen_position = screen_pos;

	Point size = Point(App::width(), App::height());
	Point draw = Point(App::draw_width(), App::draw_height());

	draw_position.x = (position.x / (float)size.x) * draw.x;
	draw_position.y = (position.y / (float)size.y) * draw.y;
}

void MouseState::on_press(MouseButton button)
{
	if (button >= 0 && button < Input::max_mouse_buttons)
	{
		down[button] = true;
		pressed[button] = true;
		timestamp[button] = Time::ticks;
	}
}

void MouseState::on_release(MouseButton button)
{
	if (button >= 0 && button < Input::max_mouse_buttons)
	{
		down[button] = false;
		released[button] = true;
	}
}

void KeyboardState::on_press(Key key)
{
	if (key >= 0 && key < Input::max_keyboard_keys)
	{
		down[key] = true;
		pressed[key] = true;
		timestamp[key] = Time::ticks;
	}
}

void KeyboardState::on_release(Key key)
{
	if (key >= 0 && key < Input::max_keyboard_keys)
	{
		down[key] = false;
		released[key] = true;
	}
}

void ControllerState::on_connect(const String& name, bool is_gamepad, int button_count, int axis_count, u16 vendor, u16 product, u16 version)
{
	*this = g_empty_controller;
	this->name = name;
	this->is_connected = true;
	this->is_gamepad = is_gamepad;
	this->button_count = button_count;
	this->axis_count = axis_count;
	this->vendor = vendor;
	this->product = product;
	this->version = version;
}

void ControllerState::on_disconnect()
{
	*this = g_empty_controller;
}

void ControllerState::on_press(Button button)
{
	if (button >= 0 && button < Input::max_controller_buttons && button < button_count)
	{
		down[button] = true;
		pressed[button] = true;
		button_timestamp[button] = Time::ticks;
	}
}

void ControllerState::on_release(Button button)
{
	if (button >= 0 && button < Input::max_controller_buttons && button < button_count)
	{
		down[button] = false;
		released[button] = true;
	}
}

void ControllerState::on_axis(Axis index, float value)
{
	if (index >= 0 && index < Input::max_controller_axis && index < axis_count)
	{
		axis[index] = value;
		axis_timestamp[index] = Time::ticks;
	}
}

bool KeyboardState::ctrl()
{
	return down[Key::LeftControl] || down[Key::RightControl];
}

bool KeyboardState::shift()
{
	return down[Key::LeftShift] || down[Key::RightShift];
}

bool KeyboardState::alt()
{
	return down[Key::LeftAlt] || down[Key::RightAlt];
}

Vec2f Input::mouse()
{
	return state.mouse.position;
}

Vec2f Input::mouse_draw()
{
	return state.mouse.draw_position;
}

Vec2f Input::mouse_screen()
{
	return state.mouse.screen_position;
}

bool Input::pressed(MouseButton button)
{
	return state.mouse.pressed[button];
}

bool Input::down(MouseButton button)
{
	return state.mouse.down[button];
}

bool Input::released(MouseButton button)
{
	return state.mouse.released[button];
}

Point Input::mouse_wheel()
{
	return state.mouse.wheel;
}

bool Input::pressed(Key key)
{
	return state.keyboard.pressed[key];
}

bool Input::down(Key key)
{
	return state.keyboard.down[key];
}

bool Input::released(Key key)
{
	return state.keyboard.released[key];
}

bool Input::repeating(Key key)
{
	if (state.keyboard.pressed[key])
		return true;

	if (state.keyboard.down[key])
	{
		double timestamp = state.keyboard.timestamp[key] / (double)Time::ticks_per_second;
		double current_time = Time::ticks / (double)Time::ticks_per_second;

		if (current_time > timestamp + Input::repeat_delay)
		{
			if (Time::on_interval(current_time - timestamp, Time::delta, Input::repeat_interval, 0.0f))
				return true;
		}
	}

	return false;
}

bool Input::ctrl()
{
	return state.keyboard.ctrl();
}

bool Input::shift()
{
	return state.keyboard.shift();
}

bool Input::alt()
{
	return state.keyboard.alt();
}

const char* Input::name_of(Key key)
{
	switch (key)
	{
		#define DEFINE_KEY(name, value) case Key::name: return #name;
		BLAH_KEY_DEFINITIONS
		#undef DEFINE_KEY
	}

	return "Unknown";
}

const char* Input::name_of(Button button)
{
	switch (button)
	{
		#define DEFINE_BTN(name, value) case Button::name: return #name;
		BLAH_BUTTON_DEFINITIONS
		#undef DEFINE_BTN
	}

	return "Unknown";
}

const String& Input::get_clipboard()
{
	return g_clipboard;
}

void Input::set_clipboard(const String& text)
{
	g_clipboard = text;
	return Platform::set_clipboard(text);
}

ButtonBindingRef Input::register_binding(const ButtonBinding& binding)
{
	auto result = std::make_shared<ButtonBinding>(binding);
	g_buttons.push_back(WeakRef<ButtonBinding>(result));
	return result;
}

AxisBindingRef Input::register_binding(const AxisBinding& binding)
{
	auto result = std::make_shared<AxisBinding>(binding);
	g_axes.push_back(WeakRef<AxisBinding>(result));
	return result;
}

StickBindingRef Input::register_binding(const StickBinding& binding)
{
	auto result = std::make_shared<StickBinding>(binding);
	g_sticks.push_back(WeakRef<StickBinding>(result));
	return result;
}


ButtonBinding::TriggerBind::TriggerBind(Axis axis)
	: axis(axis)
{

}

ButtonBinding::TriggerBind::TriggerBind(int controller, Axis axis, float threshold, bool positive)
	: controller(controller), axis(axis), threshold(threshold), positive(positive)
{

}

bool ButtonBinding::TriggerBind::is_down(float axis_value) const
{
	if ((axis_value > 0 && positive) || (axis_value < 0 && !positive))
	{
		if (Calc::abs(axis_value) >= threshold)
			return true;
	}

	return false;
}

ButtonBinding::ButtonBind::ButtonBind(Button button)
	: button(button) {}

ButtonBinding::ButtonBind::ButtonBind(int controller, Button button)
	: controller(controller), button(button) {}

bool ButtonBinding::pressed() const
{
	if (m_press_consumed)
		return false;

	if (m_last_press_time >= 0 && (Time::seconds - m_last_press_time) <= press_buffer)
		return true;

	return m_pressed;
}

bool ButtonBinding::released() const
{
	if (m_release_consumed)
		return false;

	if (m_last_release_time >= 0 && (Time::seconds - m_last_release_time) <= release_buffer)
		return true;

	return m_released;
}

bool ButtonBinding::down() const
{
	return m_down;
}

float ButtonBinding::value() const
{
	return m_value;
}

int ButtonBinding::sign() const
{
	return (int)Calc::sign(m_value);
}

double ButtonBinding::timestamp() const
{
	return m_last_timestamp;
}

void ButtonBinding::update()
{
	m_press_consumed = false;
	m_release_consumed = false;

	if (get_pressed())
	{
		m_last_timestamp = Time::seconds;
		m_last_press_time = Time::seconds;
		m_pressed = true;
	}
	else
	{
		m_pressed = false;
	}

	if (get_released())
	{
		m_last_release_time = Time::seconds;
		m_released = true;
	}
	else
	{
		m_released = false;
	}

	m_down = get_down();
	m_value = get_value();
}

void ButtonBinding::consume_press()
{
	m_press_consumed = true;
	m_last_press_time = -1;
}

void ButtonBinding::consume_release()
{
	m_release_consumed = true;
	m_last_release_time = -1;
}

ButtonBinding& ButtonBinding::add(Key key)
{
	keys.push_back(key);
	return *this;
}

ButtonBinding& ButtonBinding::add(ButtonBind button)
{
	buttons.push_back(button);
	return *this;
}

ButtonBinding& ButtonBinding::add(TriggerBind trigger)
{
	triggers.push_back(trigger);
	return *this;
}

ButtonBinding& ButtonBinding::add(MouseButton button)
{
	mouse.push_back(button);
	return *this;
}

ButtonBinding& ButtonBinding::add_left_trigger(int controller, float threshold)
{
	triggers.push_back(TriggerBind(controller, Axis::LeftTrigger, threshold, true));
	return *this;
}

ButtonBinding& ButtonBinding::add_right_trigger(int controller, float threshold)
{
	triggers.push_back(TriggerBind(controller, Axis::RightTrigger, threshold, true));
	return *this;
}

ButtonBinding& ButtonBinding::set_controller(int index)
{
	for (auto& it : buttons)
		it.controller = index;
	for (auto& it : triggers)
		it.controller = index;

	return *this;
}

void ButtonBinding::clear()
{
	keys.clear();
	buttons.clear();
	triggers.clear();
	mouse.clear();
}

bool ButtonBinding::get_pressed() const
{
	for (auto& it : keys)
		if (Input::state.keyboard.pressed[it])
			return true;

	for (auto& it : mouse)
		if (Input::state.mouse.pressed[it])
			return true;

	for (auto& it : buttons)
		if (Input::state.controllers[it.controller].pressed[it.button])
			return true;

	for (auto& it : triggers)
	{
		if (it.controller < 0 || it.controller >= Input::max_controllers)
			continue;

		if ((int)it.axis < 0 || (int)it.axis >= Input::max_controller_axis)
			continue;

		if (it.is_down(Input::state.controllers[it.controller].axis[(int)it.axis]) &&
			!it.is_down(Input::last_state.controllers[it.controller].axis[(int)it.axis]))
			return true;
	}

	return false;
}

bool ButtonBinding::get_released() const
{
	for (auto& it : keys)
		if (Input::state.keyboard.released[it])
			return true;

	for (auto& it : mouse)
		if (Input::state.mouse.released[it])
			return true;

	for (auto& it : buttons)
		if (Input::state.controllers[it.controller].released[it.button])
			return true;

	for (auto& it : triggers)
	{
		if (it.controller < 0 || it.controller >= Input::max_controllers)
			continue;

		if ((int)it.axis < 0 || (int)it.axis >= Input::max_controller_axis)
			continue;

		if (!it.is_down(Input::state.controllers[it.controller].axis[(int)it.axis]) &&
			it.is_down(Input::last_state.controllers[it.controller].axis[(int)it.axis]))
			return true;
	}

	return false;
}

bool ButtonBinding::get_down() const
{
	for (auto& it : keys)
		if (Input::state.keyboard.down[it])
			return true;

	for (auto& it : mouse)
		if (Input::state.mouse.down[it])
			return true;

	for (auto& it : buttons)
		if (Input::state.controllers[it.controller].down[it.button])
			return true;

	for (auto& it : triggers)
	{
		if (it.controller < 0 || it.controller >= Input::max_controllers)
			continue;

		if ((int)it.axis < 0 || (int)it.axis >= Input::max_controller_axis)
			continue;

		if (it.is_down(Input::state.controllers[it.controller].axis[(int)it.axis]))
			return true;
	}

	return false;
}

float ButtonBinding::get_value() const
{
	for (auto& it : keys)
		if (Input::state.keyboard.down[it])
			return 1.0f;

	for (auto& it : mouse)
		if (Input::state.mouse.down[it])
			return 1.0f;

	for (auto& it : buttons)
		if (Input::state.controllers[it.controller].down[it.button])
			return 1.0f;

	float highest = 0;

	for (auto& it : triggers)
	{
		if (it.controller < 0 || it.controller >= Input::max_controllers)
			continue;

		if ((int)it.axis < 0 || (int)it.axis >= Input::max_controller_axis)
			continue;

		float raw_value = Input::state.controllers[it.controller].axis[(int)it.axis];

		if (it.is_down(raw_value))
		{
			float mapped_value = Calc::clamped_map(Calc::abs(raw_value), it.threshold, 1.0f, 0.0f, 1.0f);
			if (mapped_value > highest)
				highest = mapped_value;
		}
	}

	return highest;
}

float AxisBinding::value() const
{
	float neg = negative.value();
	float pos = positive.value();

	// neither are down
	if (neg <= 0 && pos <= 0)
		return 0;

	// negative-only is down
	if (neg > 0 && pos <= 0)
		return -neg;

	// positive-only is down
	if (pos > 0 && neg <= 0)
		return pos;

	// both are down:

	// overlap cancel out
	if (overlap == Overlap::Cancel)
		return 0;

	// overlap takes older
	if (overlap == Overlap::Older)
	{
		if (negative.timestamp() < positive.timestamp())
			return -neg;
		else
			return pos;
	}

	// overlap takes newer
	if (negative.timestamp() > positive.timestamp())
		return -neg;
	else
		return pos;
}

int AxisBinding::sign() const
{
	return (int)Calc::sign(value());
}

void AxisBinding::update()
{
	negative.update();
	positive.update();
}

void AxisBinding::consume_press()
{
	negative.consume_press();
	positive.consume_press();
}

void AxisBinding::consume_release()
{
	negative.consume_release();
	positive.consume_release();
}

AxisBinding& AxisBinding::add_left_stick_x(int controller, float threshold)
{
	negative.add(ButtonBinding::TriggerBind(controller, Axis::LeftX, threshold, false));
	positive.add(ButtonBinding::TriggerBind(controller, Axis::LeftX, threshold, true));
	return *this;
}

AxisBinding& AxisBinding::add_left_stick_y(int controller, float threshold)
{
	negative.add(ButtonBinding::TriggerBind(controller, Axis::LeftY, threshold, false));
	positive.add(ButtonBinding::TriggerBind(controller, Axis::LeftY, threshold, true));
	return *this;
}

AxisBinding& AxisBinding::add_right_stick_x(int controller, float threshold)
{
	negative.add(ButtonBinding::TriggerBind(controller, Axis::RightX, threshold, false));
	positive.add(ButtonBinding::TriggerBind(controller, Axis::RightX, threshold, true));
	return *this;
}

AxisBinding& AxisBinding::add_right_stick_y(int controller, float threshold)
{
	negative.add(ButtonBinding::TriggerBind(controller, Axis::RightY, threshold, false));
	positive.add(ButtonBinding::TriggerBind(controller, Axis::RightY, threshold, true));
	return *this;
}

AxisBinding& AxisBinding::set_controller(int index)
{
	negative.set_controller(index);
	positive.set_controller(index);
	return *this;
}

void AxisBinding::clear()
{
	negative.clear();
	positive.clear();
}

Vec2f StickBinding::value() const
{
	Vec2f result = Vec2f(x.value(), y.value());
	if (round_threshold > 0 && result.length() < round_threshold)
		return Vec2f::zero;
	return result;
}

Point StickBinding::sign() const
{
	Vec2f result = value();
	return Point((int)Calc::sign(result.x), (int)Calc::sign(result.y));
}

void StickBinding::update()
{
	x.update();
	y.update();
}

void StickBinding::consume_press()
{
	x.consume_press();
	y.consume_press();
}

void StickBinding::consume_release()
{
	x.consume_release();
	y.consume_release();
}

StickBinding& StickBinding::add_dpad(int controller)
{
	x.negative.add(ButtonBinding::ButtonBind(controller, Button::Left));
	x.positive.add(ButtonBinding::ButtonBind(controller, Button::Right));
	y.negative.add(ButtonBinding::ButtonBind(controller, Button::Up));
	y.positive.add(ButtonBinding::ButtonBind(controller, Button::Down));
	return *this;
}

StickBinding& StickBinding::add_left_stick(int controller, float threshold)
{
	x.add_left_stick_x(controller, threshold);
	y.add_left_stick_y(controller, threshold);
	return *this;
}

StickBinding& StickBinding::add_right_stick(int controller, float threshold)
{
	x.add_right_stick_x(controller, threshold);
	y.add_right_stick_y(controller, threshold);
	return *this;
}

StickBinding& StickBinding::set_controller(int index)
{
	x.set_controller(index);
	y.set_controller(index);
	return *this;
}

void StickBinding::clear()
{
	x.clear();
	y.clear();
}

