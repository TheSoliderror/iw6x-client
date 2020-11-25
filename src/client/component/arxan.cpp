#include <std_include.hpp>
#include "loader/component_loader.hpp"
#include "scheduler.hpp"
#include "game/game.hpp"
#include "utils/hook.hpp"

#include <breakpoint.h>


#include "utils/binary_resource.hpp"
#include "utils/string.hpp"

int bps = false;
size_t watchAddr = 0x13FE288;
static bool __installed = false;

namespace arxan
	{

	namespace
	{
		typedef struct _OBJECT_HANDLE_ATTRIBUTE_INFORMATION
		{
			BOOLEAN Inherit;
			BOOLEAN ProtectFromClose;
		} OBJECT_HANDLE_ATTRIBUTE_INFORMATION;

		utils::hook::detour nt_close_hook;
		utils::hook::detour nt_query_information_process_hook;

		NTSTATUS WINAPI nt_query_information_process_stub(const HANDLE handle, const PROCESSINFOCLASS info_class,
		                                                  PVOID info,
		                                                  const ULONG info_length, const PULONG ret_length)
		{
			auto* orig = static_cast<decltype(NtQueryInformationProcess)*>(nt_query_information_process_hook.
				get_original());
			const auto status = orig(handle, info_class, info, info_length, ret_length);

			if (NT_SUCCESS(status))
			{
				if (info_class == ProcessBasicInformation)
				{
					static DWORD explorerPid = 0;
					if (!explorerPid)
					{
						auto* const shell_window = GetShellWindow();
						GetWindowThreadProcessId(shell_window, &explorerPid);
					}

					static_cast<PPROCESS_BASIC_INFORMATION>(info)->Reserved3 = PVOID(DWORD64(explorerPid));
				}
				else if (info_class == 30) // ProcessDebugObjectHandle
				{
					*static_cast<HANDLE*>(info) = nullptr;

					return 0xC0000353;
				}
				else if (info_class == 7) // ProcessDebugPort
				{
					*static_cast<HANDLE*>(info) = nullptr;
				}
				else if (info_class == 31)
				{
					*static_cast<ULONG*>(info) = 1;
				}
			}

			return status;
		}

		NTSTATUS NTAPI nt_close_stub(const HANDLE handle)
		{
			char info[16];
			if (NtQueryObject(handle, OBJECT_INFORMATION_CLASS(4), &info, sizeof(OBJECT_HANDLE_ATTRIBUTE_INFORMATION),
			                  nullptr) >= 0)
			{
				auto* orig = static_cast<decltype(NtClose)*>(nt_close_hook.get_original());
				return orig(handle);
			}

			return STATUS_INVALID_HANDLE;
		}

		jmp_buf* get_buffer()
		{
			static thread_local jmp_buf old_buffer;
			return &old_buffer;
		}

#pragma warning(push)
#pragma warning(disable: 4611)
		void reset_state()
		{
			game::longjmp(get_buffer(), -1);
		}
#pragma warning(pop)

		size_t get_reset_state_stub()
		{
			static auto* stub = utils::hook::assemble([](utils::hook::assembler& a)
			{
				a.sub(rsp, 0x10);
				a.or_(rsp, 0x8);
				a.jmp(reset_state);
			});

			return reinterpret_cast<size_t>(stub);
		}

	thread_local void* value_save;
	thread_local void* address_save;

	bool luuul = false;

		LONG WINAPI exception_filter(const LPEXCEPTION_POINTERS info)
		{
			if (info->ExceptionRecord->ExceptionCode == STATUS_INVALID_HANDLE)
			{
				return EXCEPTION_CONTINUE_EXECUTION;
			}
			
			if (info->ExceptionRecord->ExceptionCode == STATUS_ACCESS_VIOLATION)
			{
				/*const auto address1 = reinterpret_cast<size_t>(info->ExceptionRecord->ExceptionInformation[1]);
				if(address1 == 0x1337)
				{
					info->ContextRecord->EFlags |= 0x100;
					return EXCEPTION_CONTINUE_EXECUTION;
				}*/
				
				const auto address = reinterpret_cast<size_t>(info->ExceptionRecord->ExceptionAddress);
				if((address & ~0xFFFFFFF) == 0x280000000)
				{
					//MessageBoxA(nullptr, "Arxan Exception", "Oh fuck.", MB_ICONERROR);

					//info->ContextRecord->Rip = get_reset_state_stub();
					info->ContextRecord->Rip -= 0x140000000;
					info->ContextRecord->Rsp += 0x8;
					return EXCEPTION_CONTINUE_EXECUTION;
				}
			}

			static thread_local bool wassinglestep = false;
			static thread_local bool should_log = false;
			static thread_local void* lastaddr;
			static thread_local bool trace = false;

			const auto start = 0x140035EA7;
			const auto end = 0x14A8BDC44;

			if(info->ExceptionRecord->ExceptionCode ==STATUS_SINGLE_STEP && __installed)
			{
				return EXCEPTION_CONTINUE_EXECUTION;
			}
			
			if(info->ExceptionRecord->ExceptionCode ==STATUS_SINGLE_STEP && __installed && false) {
				
				if(wassinglestep) {
					wassinglestep = false;
					
										if(trace)
					{
											if(info->ContextRecord->EFlags & 0x100)
											{
												OutputDebugStringA("set\n");
											}

											if(size_t(info->ExceptionRecord->ExceptionAddress) == 0x1400AAD10)
											{
												OutputDebugStringA("set\n");
											}
											
						info->ContextRecord->EFlags |= 0x100;
											wassinglestep = true;
						OutputDebugStringA(utils::string::va("\t--> %p\n", info->ExceptionRecord->ExceptionAddress));
					}

					if(size_t(lastaddr) == end)
					{
						trace = false;
					}

					if(should_log) {
						should_log = false;
					auto val = *(void**)watchAddr;
					OutputDebugStringA(utils::string::va("Ex: %p   Val: %p\n", lastaddr, val));

					if((size_t(val) & ~0xFFFFFFF) == 0x280000000)
					{
						printf("Ded\n");
					}
					}
					
				} else /*if(0x14A9267F6 == size_t(info->ExceptionRecord->ExceptionAddress))*/ {
					//if(0x000000014aad4a8e != size_t(info->ExceptionRecord->ExceptionAddress))
					//OutputDebugStringA(utils::string::va("Ex: %p\n", info->ExceptionRecord->ExceptionAddress));
					//return EXCEPTION_CONTINUE_EXECUTION;
					lastaddr = info->ExceptionRecord->ExceptionAddress;

					if(size_t(lastaddr) == start)
					{
						//OutputDebugStringA("");
						trace = true;
					}
					
					info->ContextRecord->EFlags |= 0x100;
					wassinglestep = true;
					should_log = true;

					if(0x140036CDC == size_t(info->ExceptionRecord->ExceptionAddress)) {
						printf("NOW\n");
					}
				}
				
				//if(bps)
				{
					/*if(*(void**)address_save != value_save) {
						MessageBoxA(0,"It happened!",0,0);
					}*/
					
				}
				//else {
					//info->ContextRecord->EFlags &= ~0x100;
				//	return EXCEPTION_CONTINUE_SEARCH;
				//}
				return EXCEPTION_CONTINUE_EXECUTION;
			}
			
			return EXCEPTION_CONTINUE_SEARCH;
		}

		void hide_being_debugged()
		{
			auto* const peb = PPEB(__readgsqword(0x60));
			peb->BeingDebugged = false;
			*PDWORD(LPSTR(peb) + 0xBC) &= ~0x70;
		}

		void remove_hardware_breakpoints()
		{
			CONTEXT context;
			ZeroMemory(&context, sizeof(context));
			context.ContextFlags = CONTEXT_DEBUG_REGISTERS;

			auto* const thread = GetCurrentThread();
			GetThreadContext(thread, &context);

			context.Dr0 = 0;
			context.Dr1 = 0;
			context.Dr2 = 0;
			context.Dr3 = 0;
			context.Dr6 = 0;
			context.Dr7 = 0;

			SetThreadContext(thread, &context);
		}

		BOOL WINAPI set_thread_context_stub(const HANDLE thread, CONTEXT* context)
		{
			if (!game::environment::is_sp()
				&& game::dwGetLogOnStatus(0) == game::DW_LIVE_CONNECTED
				&& context->ContextFlags == CONTEXT_DEBUG_REGISTERS)
			{
				return TRUE;
			}

			return SetThreadContext(thread, context);
		}

		void dw_frame_stub(const int index)
		{
			const auto status = game::dwGetLogOnStatus(index);

			if (status == game::DW_LIVE_CONNECTING)
			{
				// dwLogOnComplete
				reinterpret_cast<void(*)(int)>(0x1405894D0)(index);
			}
			else if (status == game::DW_LIVE_DISCONNECTED)
			{
				// dwLogOnStart
				reinterpret_cast<void(*)(int)>(0x140589E10)(index);
			}
			else
			{
				// dwLobbyPump
				//reinterpret_cast<void(*)(int)>(0x1405918E0)(index);

				// DW_Frame
				reinterpret_cast<void(*)(int)>(0x14000F9A6)(index);
			}
		}
	}

    inline void SetBits(ULONG_PTR& dw, int lowBit, int bits, int newValue)
    {
        int mask = (1 << bits) - 1; // e.g. 1 becomes 0001, 2 becomes 0011, 3 becomes 0111

        dw = (dw & ~(mask << lowBit)) | (newValue << lowBit);
    }

	void setBP(void* addr)
	{
		CONTEXT cxt;
            cxt.ContextFlags = CONTEXT_DEBUG_REGISTERS;
			auto hThread = GetCurrentThread();
		
           GetThreadContext(hThread, &cxt);

auto index = 0;
                SetBits(cxt.Dr7, index * 2, 1, 1);


                    switch (index)
                    {
                    case 0: cxt.Dr0 = (DWORD_PTR)addr; break;
                    case 1: cxt.Dr1 = (DWORD_PTR)addr; break;
                    case 2: cxt.Dr2 = (DWORD_PTR)addr; break;
                    case 3: cxt.Dr3 = (DWORD_PTR)addr; break;
                    }

                    SetBits(cxt.Dr7, 16 + (index * 4), 2, 1);
                    SetBits(cxt.Dr7, 18 + (index * 4), 2, 8);

           SetThreadContext(hThread, &cxt);
	}

	void* addr;

	void install_lul(void* lul)
	{
		setBP((void*)watchAddr);
		//scheduler::once([lul]() {
			//setBP(lul);
		//}, scheduler::pipeline::server);
		auto* xx = &bps;
		
		auto x = LoadLibraryA("PhysXDevice64.dll");
		auto y = LoadLibraryA("PhysXUpdateLoader64.dll");

		scheduler::once([lul]() {
			//HWBreakpoint::Set((void*)0x149DCFBC5, HWBreakpoint::Condition::Write);
		}, scheduler::pipeline::async);
	}

	void* get_stack_backup()
	{
		static thread_local char backup[0x1000];
		return backup;
	}
	
	void backup_stack(void* addr)
	{
		memcpy(get_stack_backup(), addr, 0x1000);
	}

	void restore_stack(void* addr)
	{
		memcpy(addr, get_stack_backup(), 0x1000);
	}

	namespace lul_
		{
			utils::binary_resource runner_file(RUNNER, "runner.exe");

			void debug_self()
			{
				STARTUPINFOA startup_info;
				PROCESS_INFORMATION process_info;

				ZeroMemory(&startup_info, sizeof(startup_info));
				ZeroMemory(&process_info, sizeof(process_info));
				startup_info.cb = sizeof(startup_info);

				const auto runner = runner_file.get_extracted_file();
				auto* arguments = const_cast<char*>(utils::string::va("\"%s\" -debug -proc %d", runner.data(),
				                                                      GetCurrentProcessId()));
				CreateProcessA(runner.data(), arguments, nullptr, nullptr, false, NULL, nullptr, nullptr,
				               &startup_info, &process_info);

				if (process_info.hThread && process_info.hThread != INVALID_HANDLE_VALUE)
				{
					CloseHandle(process_info.hThread);
				}

				if (process_info.hProcess && process_info.hProcess != INVALID_HANDLE_VALUE)
				{
					CloseHandle(process_info.hProcess);
				}
			}
		}
	
#pragma warning(push)
#pragma warning(disable: 4611)
	int save_state_intenal()
	{
		
		if(!__installed){
			__installed = true;
		install_lul(_AddressOfReturnAddress());
			lul_::debug_self();
			//AddVectoredExceptionHandler(1, exception_filter);
		}
		bps = true;
		address_save = _AddressOfReturnAddress();
		value_save = _ReturnAddress();
		
		//*(int*)0x1337 = 0;
		
		luuul = true;
		addr = _AddressOfReturnAddress();
		printf("Pre: %p %p\n",_AddressOfReturnAddress(),  _ReturnAddress());

		backup_stack(_AddressOfReturnAddress());
		
		const auto recovered = game::_setjmp(get_buffer());
		if(recovered)
		{
			restore_stack(_AddressOfReturnAddress());
			
			printf("Recovering from arxan error...\n");
			MessageBoxA(0,0,0,0);
		}
		printf("Post: %p %p\n",_AddressOfReturnAddress(),  _ReturnAddress());

		bps = false;
		//HWBreakpoint::ClearAll();
		return recovered;
	}
#pragma warning(pop)

	bool save_state()
	{
		return save_state_intenal() != 0;
	}

	void* memmv( void* _Dst, void const* _Src,  size_t _Size)
    {
		if(size_t(_Dst) <= size_t(addr) &&size_t(_Dst) + _Size >= size_t(addr) && bps) 
		{
			printf("OK");
		}
		
	   return memmove(_Dst, _Src, _Size);
    }

	class component final : public component_interface
	{
	public:
		void* load_import(const std::string& library, const std::string& function) override
		{
			if (function == "SetThreadContext")
			{
				return set_thread_context_stub;
			}

			return nullptr;
		}

		void post_load() override
		{
			hide_being_debugged();
			scheduler::loop(hide_being_debugged, scheduler::pipeline::async);

			const utils::nt::library ntdll("ntdll.dll");
			nt_close_hook.create(ntdll.get_proc<void*>("NtClose"), nt_close_stub);
			nt_query_information_process_hook.create(ntdll.get_proc<void*>("NtQueryInformationProcess"),
			                                         nt_query_information_process_stub);

			AddVectoredExceptionHandler(1, exception_filter);
		}

		void post_unpack() override
		{
			// cba to implement sp, not sure if it's even needed
			if (game::environment::is_sp()) return;

			utils::hook::jump(0x1404FE1E0, 0x1404FE2D0); // idk
			utils::hook::jump(0x140558C20, 0x140558CB0); // dwNetPump
			utils::hook::jump(0x140591850, 0x1405918E0); // dwLobbyPump
			utils::hook::jump(0x140589480, 0x140589490); // dwGetLogonStatus
			
			utils::hook::jump(0x140730160, memmv);

			// Fix arxan crashes
			// Are these opaque predicates?
			utils::hook::nop(0x14AE2B384, 6); // 0000000140035EA7
			utils::hook::nop(0x14A31E98E, 4); // 000000014B1A892E
			utils::hook::nop(0x14A920E10, 4); // 000000014AEF4F39
			utils::hook::nop(0x14A1A2425, 4); // 000000014A0B52A8
			utils::hook::nop(0x14AE07CEA, 4); // 000000014A143BFF
			
			/*const auto result = "48 B8 ? ? ? ? ? ? ? ? 48 0F 46 D8"_sig;
			for(size_t i = 0; i < result.count(); ++i)
			{
				//utils::hook::nop(result.get(i) + 10, 4);
				//utils::hook::set<DWORD>(result.get(i) + 10, 0x90C38948);
			}*/

			// These two are inlined with their synchronization. Need to work around that
			//utils::hook::jump(0x14015EB9A, 0x140589E10); // dwLogOnStart
			//utils::hook::call(0x140588306, 0x1405894D0); // dwLogOnComplete

			// Unfinished for now
			//utils::hook::jump(0x1405881E0, dw_frame_stub);

			scheduler::on_game_initialized(remove_hardware_breakpoints, scheduler::pipeline::main);
		}
	};
}

REGISTER_COMPONENT(arxan::component)
