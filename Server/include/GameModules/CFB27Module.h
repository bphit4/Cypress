#pragma once
#include <IGameModule.h>

#ifdef CYPRESS_CFB27
namespace Cypress
{
	class CFB27Module : public IGameModule {
	public:
		void InitGameHooks() override;
		void InitMemPatches() override;
		void InitDedicatedServerPatches(class Cypress::Server* pServer) override;
		void RegisterCommands() override;
	};
}
#endif
