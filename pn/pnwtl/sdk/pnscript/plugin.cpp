#include "stdafx.h"
#include <vector>
#include <thread>
#include <locale>
#include <codecvt>
#include <mutex>
#include <unordered_set>
#include <atomic>
#include "shellapi.h"
#include "shlwapi.h"

extensions::IPN* g_pn = nullptr;
std::atomic<bool> g_stopPipeThread{ false };

std::mutex g_clientMutex;
std::unordered_set<HANDLE> g_clientThreads;

std::wstring GetPipeName()
{
	return L"\\\\.\\pipe\\PNScriptPipe_" + std::to_wstring(GetCurrentProcessId());
}

void PipeClientThread(HANDLE hPipe)
{
	char buffer[4096];
	DWORD bytesRead;

	while (!g_stopPipeThread)
	{
		BOOL success = ReadFile(hPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr);
		if (!success || bytesRead == 0) break;

		buffer[bytesRead] = 0;
		std::wstring command = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(buffer);
		g_pn->GetGlobalOutputWindow()->AddToolOutput(command.c_str());

		// TODO: parse command
	}

	FlushFileBuffers(hPipe);
	DisconnectNamedPipe(hPipe);
	CloseHandle(hPipe);

	std::lock_guard<std::mutex> lock(g_clientMutex);
	g_clientThreads.erase(hPipe);
}

void PipeServerThread()
{
	while (!g_stopPipeThread)
	{
		HANDLE hPipe = CreateNamedPipeW(
			GetPipeName().c_str(),
			PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
			PIPE_UNLIMITED_INSTANCES,
			4096, 4096, 0, nullptr
		);

		if (hPipe == INVALID_HANDLE_VALUE) break;

		OVERLAPPED ov = {};
		ov.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
		if (!ov.hEvent)
		{
			CloseHandle(hPipe);
			break;
		}

		BOOL connected = ConnectNamedPipe(hPipe, &ov);
		if (!connected)
		{
			if (GetLastError() == ERROR_IO_PENDING)
			{
				DWORD wait = WaitForSingleObject(ov.hEvent, 100);
				if (wait == WAIT_TIMEOUT && g_stopPipeThread)
				{
					CancelIo(hPipe);
					CloseHandle(hPipe);
					CloseHandle(ov.hEvent);
					break;
				}
			}
		}

		CloseHandle(ov.hEvent);

		std::lock_guard<std::mutex> lock(g_clientMutex);
		std::thread clientThread(PipeClientThread, hPipe);
		g_clientThreads.insert(hPipe);
		clientThread.detach();
	}
}

void RunNodeScript(const std::wstring& scriptPath)
{
	TCHAR exePath[MAX_PATH];
	GetModuleFileName(NULL, exePath, MAX_PATH);
	PathRemoveFileSpec(exePath);
	std::wstring scriptFullPath = exePath + std::wstring(L"\\") + scriptPath;

	SECURITY_ATTRIBUTES sa = { sizeof(sa), nullptr, TRUE };
	HANDLE hStdOutRead, hStdOutWrite, hStdErrRead, hStdErrWrite;

	if (!CreatePipe(&hStdOutRead, &hStdOutWrite, &sa, 0) ||
		!CreatePipe(&hStdErrRead, &hStdErrWrite, &sa, 0))
		return;

	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput = hStdOutWrite;
	si.hStdError = hStdErrWrite;

	PROCESS_INFORMATION pi = {};

	std::wstring cmd = L"node.exe \"" + scriptFullPath + L"\" \"" + GetPipeName() + L"\"";
	std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back(0);

	if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		CloseHandle(hStdOutWrite);
		CloseHandle(hStdErrWrite);

		// stdout
		std::thread([hStdOutRead]() {
			char buffer[4096];
			DWORD bytesRead;
			while (ReadFile(hStdOutRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead)
			{
				buffer[bytesRead] = 0;
				std::wstring output = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(buffer);
				g_pn->GetGlobalOutputWindow()->AddToolOutput(output.c_str());
			}
			CloseHandle(hStdOutRead);
		}).detach();

		// stderr
		std::thread([hStdErrRead]() {
			char buffer[4096];
			DWORD bytesRead;
			while (ReadFile(hStdErrRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead)
			{
				buffer[bytesRead] = 0;
				std::wstring errorOutput = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(buffer);
				g_pn->GetGlobalOutputWindow()->AddToolOutput(errorOutput.c_str());
			}
			CloseHandle(hStdErrRead);
		}).detach();

		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
	}
	else
	{
		g_pn->GetGlobalOutputWindow()->AddToolOutput(L"Failed to start Node.js\n");
		CloseHandle(hStdOutRead); CloseHandle(hStdOutWrite);
		CloseHandle(hStdErrRead); CloseHandle(hStdErrWrite);
	}
}

class PNSEventSink: public extensions::IAppEventSink
{
	public:
	
	/// Called when a new document is opened/created
	void OnNewDocument(extensions::IDocumentPtr& doc) override
	{
		//
	}
	
	/// Called when PN is closing (you are about to be unloaded!)
	void OnAppClose() override
	{
		//
	}

	/// Called when the user switches to a different document
	void OnDocSelected(extensions::IDocumentPtr& doc) override
	{
		//
	}

	/// Called when the very first Scintilla window is created, used for loading external lexers
	void OnFirstEditorCreated(HWND hWndScintilla) override
	{
		//
	}
};

class PNSMenuItems: public extensions::IMenuItems
{
	public:
	PNSMenuItems(const std::vector<std::wstring>& scripts)
	{
		for (size_t i = 0; i < scripts.size(); ++i)
		{
			extensions::MenuItem item;
			item.Type = extensions::miItem;
			item.Title = _wcsdup(scripts[i].c_str());
			item.UserData = (extensions::cookie_t)i;
			item.Handler = [i, scripts](extensions::cookie_t) {
				RunNodeScript(scripts[i]);
			};
			item.SubItems = nullptr;
			m_items.push_back(item);
		}
	}

	~PNSMenuItems()
	{
		for (auto& item : m_items)
			free(item.Title);
	}

	int GetItemCount() const override
	{
		return (int)m_items.size();
	}

	const extensions::MenuItem& GetItem(int index) const override
	{
		return m_items[index];
	}

	private:
	std::vector<extensions::MenuItem> m_items;
};

std::vector<std::wstring> GetScriptsFromFolder(const std::wstring& folderPath)
{
	TCHAR exePath[MAX_PATH];
	GetModuleFileName(NULL, exePath, MAX_PATH);
	PathRemoveFileSpec(exePath);
	std::wstring scriptsSearchPath = exePath + std::wstring(L"\\scripts\\*.js");

	std::vector<std::wstring> scripts;
	WIN32_FIND_DATAW findData;

	HANDLE hFind = FindFirstFileW(scriptsSearchPath.c_str(), &findData);
	if (hFind == INVALID_HANDLE_VALUE)
		return scripts;

	do
	{
		if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
		{
			scripts.push_back(folderPath + L"\\" + findData.cFileName);
		}
	}
	while (FindNextFileW(hFind, &findData));

	FindClose(hFind);
	return scripts;
}

bool __stdcall pn_init_extension(int iface_version, extensions::IPN* pn) {
	if (iface_version != PN_EXT_IFACE_VERSION)
		return false;

	g_pn = pn;
	g_stopPipeThread = false;
	std::thread(PipeServerThread).detach();

	extensions::IAppEventSinkPtr sink(new PNSEventSink());
	g_pn->AddEventSink(sink);

	std::vector<std::wstring> scripts = GetScriptsFromFolder(L"scripts");
	PNSMenuItems* menuItems = new PNSMenuItems(scripts);
	g_pn->AddPluginMenuItems(menuItems);

	// You can control various basic functionality right from the pn instance:

	// pn->NewDocument(NULL);
	// pn->OpenDocument("Sample.cpp", "java"); // Open Sample.cpp with the java scheme
	// pn->OpenDocument("Sample.cpp", NULL);   // Open Sample.cpp with the default scheme for *.cpp
	// pn->GetCurrentDocument()->GetFileName();

	// Documents have events too:

	// extensions::IDocumentEventSinkPtr docSink(new DocEvents(pn->GetCurrentDocument()));
	// pn->GetCurrentDocument()->AddEventSink(docSink);
	// extensions::IDocumentEventSinkPtr editSink(new EditEvents(pn->GetCurrentDocument()));
	// pn->GetCurrentDocument()->AddEventSink(editSink);

	return true;
}

void __declspec(dllexport) __stdcall pn_get_extension_info(PN::BaseString& name, PN::BaseString& version)
{
	name = "PNScript";
	version = "0.1";
}

void __declspec(dllexport) __stdcall pn_exit_extension()
{
	g_stopPipeThread = true;

	std::lock_guard<std::mutex> lock(g_clientMutex);
	for (auto h : g_clientThreads)
	{
		CancelIo(h);
		CloseHandle(h);
	}
	g_clientThreads.clear();
}

BOOL APIENTRY DllMain( HMODULE hModule,
					   DWORD  ul_reason_for_call,
					   LPVOID lpReserved
					 )
{
	return TRUE;
}
