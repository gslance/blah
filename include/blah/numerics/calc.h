#pragma once
#include <blah/common.h>

namespace Blah
{
	namespace Calc
	{
		constexpr float PI = 3.141592653f;
		constexpr float TAU = PI * 2.0f;
		constexpr float RIGHT = 0;
		constexpr float LEFT = PI;
		constexpr float UP = PI / -2;
		constexpr float DOWN = PI / 2;

		float approach(float t, float target, float delta);

		float map(float t, float old_min, float old_max, float new_min, float new_max);

		float clamped_map(float t, float old_min, float old_max, float new_min, float new_max);

		int sign(int x);

		float sign(float x);

		int abs(int x);

		float abs(float x);

		template<class T, class TMin, class TMax>
		T clamp(T value, TMin min, TMax max) { return value < min ? static_cast<T>(min) : (value > max ? static_cast<T>(max) : value); }

		template<class T>
		T min(T a, T b) { return  (T)(a < b ? a : b); }

		template<class T, typename ... Args>
		T min(const T& a, const T& b, const Args&... args) { return Calc::min(a, Calc::min(b, args...)); }

		template<class T>
		T max(T a, T b) { return (T)(a > b ? a : b); }

		template<class T, typename ... Args>
		T max(const T& a, const T& b, const Args&... args) { return Calc::max(a, Calc::max(b, args...)); }

		float round(float x);

		float floor(float x);

		float ceiling(float x);

		float mod(float x, float m);

		float sin(float x);

		float cos(float x);

		float tan(float x);

		float atan2(float y, float x);

		float pow(float x, float n);

		float sqrt(float x);

		float snap(float val, float interval);

		float angle_diff(float radians_a, float radians_b);
		
		float angle_lerp(float radians_a, float radians_b, float p);

		float lerp(float a, float b, float t);

		bool is_big_endian();

		bool is_little_endian();
	};
}
