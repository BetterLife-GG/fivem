#pragma once

#include <ScriptEngine.h>

#include <ClientRegistry.h>
#include <ResourceManager.h>
#include <ServerInstanceBaseRef.h>

template<typename TFn>
inline auto MakeClientFunction(TFn fn, uintptr_t defaultValue = 0)
{
	return [=](fx::ScriptContext & context)
	{
		// get the current resource manager
		auto resourceManager = fx::ResourceManager::GetCurrent();

		// get the owning server instance
		auto instance = resourceManager->GetComponent<fx::ServerInstanceBaseRef>()->Get();

		// get the server's client registry
		auto clientRegistry = instance->GetComponent<fx::ClientRegistry>();

		// parse the client ID (supports string "1" or integer 1)
		uint32_t netId = 0;
		uintptr_t rawId = context.GetArgument<uintptr_t>(0);
		if (rawId < 0x10000)
		{
			netId = static_cast<uint32_t>(rawId);
		}
		else
		{
			const char* id = reinterpret_cast<const char*>(rawId);
			if (id)
			{
				netId = atoi(id);
			}
		}

		if (netId == 0)
		{
			context.SetResult(defaultValue);
			return;
		}

		auto client = clientRegistry->GetClientByNetID(netId);
		if (!client && netId < 2049)
		{
			client = clientRegistry->GetClientBySlotID(netId);
		}

		if (!client)
		{
			context.SetResult(defaultValue);
			return;
		}

		context.SetResult(fn(context, client));
	};
};
