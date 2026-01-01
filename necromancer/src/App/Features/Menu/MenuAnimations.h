#pragma once

#include <map>
#include <string>
#include <functional>
#include <cmath>

// Easing functions for smooth animations
namespace Easing
{
	inline float Linear(float t) { return t; }
	
	inline float EaseInOut(float t) {
		return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t;
	}
	
	inline float EaseOut(float t) {
		return t * (2.0f - t);
	}
	
	inline float EaseIn(float t) {
		return t * t;
	}
	
	inline float Bounce(float t) {
		if (t < 0.5f) {
			return EaseIn(t * 2.0f) * 0.5f;
		}
		t = (t - 0.5f) * 2.0f;
		if (t < 1.0f / 2.75f) {
			return 0.5f + 7.5625f * t * t * 0.5f;
		} else if (t < 2.0f / 2.75f) {
			t -= 1.5f / 2.75f;
			return 0.5f + (7.5625f * t * t + 0.75f) * 0.5f;
		} else if (t < 2.5f / 2.75f) {
			t -= 2.25f / 2.75f;
			return 0.5f + (7.5625f * t * t + 0.9375f) * 0.5f;
		} else {
			t -= 2.625f / 2.75f;
			return 0.5f + (7.5625f * t * t + 0.984375f) * 0.5f;
		}
	}
	
	inline float Elastic(float t) {
		if (t == 0.0f || t == 1.0f) return t;
		float p = 0.3f;
		float s = p / 4.0f;
		return std::pow(2.0f, -10.0f * t) * std::sin((t - s) * (2.0f * 3.14159f) / p) + 1.0f;
	}
}

using EasingFunction = std::function<float(float)>;

// Animation controller for smooth transitions
class CAnimationController
{
private:
	struct Animation
	{
		float startValue;
		float endValue;
		float currentValue;
		float duration;
		float elapsed;
		EasingFunction easing;
		bool active;
		bool loop;
	};
	
	std::map<std::string, Animation> m_animations;
	
public:
	void StartAnimation(const std::string& id, float start, float end, float duration, EasingFunction easing = Easing::EaseInOut, bool loop = false)
	{
		Animation anim;
		anim.startValue = start;
		anim.endValue = end;
		anim.currentValue = start;
		anim.duration = duration;
		anim.elapsed = 0.0f;
		anim.easing = easing;
		anim.active = true;
		anim.loop = loop;
		
		m_animations[id] = anim;
	}
	
	void Update(float deltaTime)
	{
		for (auto& pair : m_animations)
		{
			auto& anim = pair.second;
			if (!anim.active) continue;
			
			anim.elapsed += deltaTime;
			
			if (anim.elapsed >= anim.duration)
			{
				if (anim.loop)
				{
					anim.elapsed = 0.0f;
					anim.currentValue = anim.startValue;
				}
				else
				{
					anim.currentValue = anim.endValue;
					anim.active = false;
				}
			}
			else
			{
				float t = anim.elapsed / anim.duration;
				float easedT = anim.easing(t);
				anim.currentValue = anim.startValue + (anim.endValue - anim.startValue) * easedT;
			}
		}
	}
	
	float GetValue(const std::string& id)
	{
		auto it = m_animations.find(id);
		if (it != m_animations.end())
			return it->second.currentValue;
		return 0.0f;
	}
	
	bool IsActive(const std::string& id)
	{
		auto it = m_animations.find(id);
		if (it != m_animations.end())
			return it->second.active;
		return false;
	}
	
	void Stop(const std::string& id)
	{
		auto it = m_animations.find(id);
		if (it != m_animations.end())
			it->second.active = false;
	}
	
	void Clear()
	{
		m_animations.clear();
	}
};

// Particle system for click effects
struct Particle
{
	float x, y;
	float vx, vy;
	float lifetime;
	float maxLifetime;
	byte alpha;
	int size;
	Color_t color;
};

class CParticleSystem
{
private:
	std::vector<Particle> m_particles;
	
public:
	void Emit(float x, float y, int count, Color_t color)
	{
		for (int i = 0; i < count; i++)
		{
			Particle p;
			p.x = x;
			p.y = y;
			
			// Random velocity in all directions
			float angle = (static_cast<float>(rand()) / RAND_MAX) * 2.0f * 3.14159f;
			float speed = 50.0f + (static_cast<float>(rand()) / RAND_MAX) * 100.0f;
			p.vx = std::cos(angle) * speed;
			p.vy = std::sin(angle) * speed;
			
			p.maxLifetime = 0.3f + (static_cast<float>(rand()) / RAND_MAX) * 0.3f;
			p.lifetime = p.maxLifetime;
			p.alpha = 255;
			p.size = 2 + rand() % 2;
			p.color = color;
			
			m_particles.push_back(p);
		}
	}
	
	void Update(float deltaTime)
	{
		for (auto it = m_particles.begin(); it != m_particles.end();)
		{
			it->lifetime -= deltaTime;
			
			if (it->lifetime <= 0.0f)
			{
				it = m_particles.erase(it);
				continue;
			}
			
			// Update position
			it->x += it->vx * deltaTime;
			it->y += it->vy * deltaTime;
			
			// Apply gravity
			it->vy += 200.0f * deltaTime;
			
			// Fade out
			float lifeRatio = it->lifetime / it->maxLifetime;
			it->alpha = static_cast<byte>(255.0f * lifeRatio);
			
			++it;
		}
	}
	
	void Render()
	{
		for (const auto& p : m_particles)
		{
			Color_t col = p.color;
			col.a = p.alpha;
			H::Draw->Rect(static_cast<int>(p.x), static_cast<int>(p.y), p.size, p.size, col);
		}
	}
	
	void Clear()
	{
		m_particles.clear();
	}
};
