#pragma once

#include <set>
#include <string>
#include <string_view>
#include <shared_mutex>
#include <mutex>

namespace fx
{
class StateBagPolicyManager
{
private:
	static inline std::shared_mutex m_mutex;
	static inline std::set<std::string, std::less<>> m_protectedKeys = {};

public:
	static inline bool AddProtectedKey(const std::string& key)
	{
		if (key.empty())
		{
			return false;
		}

		std::unique_lock lock(m_mutex);
		return m_protectedKeys.insert(key).second;
	}

	static inline bool RemoveProtectedKey(const std::string& key)
	{
		if (key.empty())
		{
			return false;
		}

		std::unique_lock lock(m_mutex);
		return m_protectedKeys.erase(key) > 0;
	}

	static inline bool IsKeyProtected(std::string_view key)
	{
		if (key.rfind("server:", 0) == 0)
		{
			return true;
		}

		std::shared_lock lock(m_mutex);
		return m_protectedKeys.find(key) != m_protectedKeys.end();
	}
};
}
