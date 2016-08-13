#include "stdafx.h"
#include "module_handler.h"
#include "traceMisc.h"
#include "trace_handler.h"
#include "thread_graph_data.h"
#include "GUIManagement.h"

void __stdcall module_handler::ThreadEntry(void* pUserData) {
	return ((module_handler*)pUserData)->PID_thread();
}

//listen to mod data for given PID
void module_handler::PID_thread()
{
	wstring pipename(L"\\\\.\\pipe\\rioThreadMod");
	pipename.append(std::to_wstring(PID));

	const wchar_t* szName = pipename.c_str();
	std::wcout << "[vis mod handler] creating mod thread " << szName << endl;
	wprintf(L"creating mod thread %s\n", szName);

	HANDLE hPipe = CreateNamedPipe(szName,
		PIPE_ACCESS_INBOUND, PIPE_TYPE_MESSAGE | PIPE_WAIT,
		255, 64, 56 * 1024, 300, NULL);

	int conresult = ConnectNamedPipe(hPipe, NULL);
	printf("[vis mod handler]connect result: %d, GLE:%d. Waiting for input...\n", conresult,GetLastError());
	TraceVisGUI* widgets = (TraceVisGUI *) clientState->widgets;
	widgets->addPID(PID);
	char buf[400] = { 0 };
	int PIDcount = 0;

	while (true)
	{
		DWORD bread = 0;
		if (!ReadFile(hPipe, buf, 399, &bread, NULL)) {
			printf("Failed to read metadata pipe for PID:%d\n", PID);
			return;
		}
		buf[bread] = 0;

		if (!bread)
		{
			int err = GetLastError();
			if (err != ERROR_BROKEN_PIPE)
				printf("threadpipe PIPE ERROR: %d\n", err);
			printf("\t!----------pid mod pipe %d broken------------\n", PID);
			piddata->active = false;
			return;
		}
		else
		{	
			if (buf[0] == 'T' && buf[1] == 'I')
			{
				int TID = 0;
				if (!extract_integer(buf, string("TI"), &TID))
				{
					printf("\tMODHANDLER TI: ERROR GOT TI BUT NO EX XTRACT!\n");
					continue;
				}
				DWORD threadID = 0;

				thread_trace_handler *TID_thread = new thread_trace_handler;
				TID_thread->PID = PID;
				TID_thread->TID = TID;
				TID_thread->piddata = piddata;

				thread_graph_data *tgraph = new thread_graph_data;

				tgraph->tid = TID; //todo: dont need this
				if (!obtainMutex(piddata->graphsListMutex, "Module Handler")) return;
				if (piddata->graphs.count(TID) > 0)
					printf("\n\n\t\tDUPICATE THREAD ID! TODO:MOVE TO INACTIVE\n\n");
				piddata->graphs.insert(make_pair(TID, (void*)tgraph));
				ReleaseMutex(piddata->graphsListMutex);

				HANDLE hOutThread = CreateThread(
					NULL, 0, (LPTHREAD_START_ROUTINE)TID_thread->ThreadEntry,
					(LPVOID)TID_thread, 0, &threadID);

				continue;
			}

			if (buf[0] == 's' && buf[1] == '!' && bread > 8)
			{
				char *next_token = NULL;
				unsigned int modnum = atoi(strtok_s(buf + 2, "@", &next_token));
				char *symname = strtok_s(next_token, "@", &next_token);
				//if (string(symname) == "ZwAllocateVirtualMemory")
				//{
					//printf("symbol %s-",symname);
				//}
				char *address_s = strtok_s(next_token, "@", &next_token);
				long address = 0;
				sscanf_s(address_s, "%x", &address);
				if (!address | !symname | (next_token - buf != bread)) continue;
				if (modnum > piddata->modpaths.size()) {
					printf("Bad mod number in s!\n");
					continue;
				}
				piddata->modsyms[modnum][address] = symname;
				continue;
			}

			if (buf[0] == 'm' && buf[1] == 'n' && bread > 8)
			{
				char *next_token = NULL;

				char *path = NULL;
				if (buf[2] == '@' && buf[3] == '@')
				{
					path = (char*)malloc(5);
					snprintf(path, 5, "NULL");
					next_token = buf + 4;
				}
				else 
					path = strtok_s(buf + 2, "@", &next_token);

				char *modnum_s = strtok_s(next_token, "@", &next_token);
				long modnum = -1;
				sscanf_s(modnum_s, "%d", &modnum);

				if (piddata->modpaths.count(modnum) > 0) {
					printf("Bad modnum! in mn %s", buf);
					continue;
				}

				char *startaddr_s = strtok_s(next_token, "@", &next_token);
				long startaddr = 0;
				sscanf_s(startaddr_s, "%x", &startaddr);

				char *endaddr_s = strtok_s(next_token, "@", &next_token);
				long endaddr = 0;
				sscanf_s(endaddr_s, "%x", &endaddr);

				char *skipped_s = strtok_s(next_token, "@", &next_token);
				if (*skipped_s == '1')
					piddata->activeMods.insert(piddata->activeMods.begin() + modnum, MOD_UNINSTRUMENTED);
				else
					piddata->activeMods.insert(piddata->activeMods.begin() + modnum, MOD_ACTIVE);

				if (!startaddr | !endaddr | (next_token - buf != bread)) {
					printf("ERROR! Processing mn line: %s\n", buf);
					continue;
				}

				piddata->modpaths[modnum] = string(path);
				continue;
			}
			printf("<PID %d mod> (%d b): %s", PID, bread, buf);
		}
	}
}