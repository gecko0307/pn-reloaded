#include "stdafx.h"
#include <vector>
#include <locale>
#include <codecvt>
#include "shellapi.h"
#include "shlwapi.h"

extensions::IPN* g_pn = nullptr;

void RunNodeScript(const std::wstring& scriptPath)
{
	TCHAR exePath[MAX_PATH];
	GetModuleFileName(NULL, exePath, MAX_PATH);
	PathRemoveFileSpec(exePath);
	std::wstring scriptFullPath = exePath + std::wstring(L"\\") + scriptPath;

	SECURITY_ATTRIBUTES sa = { sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE };
	HANDLE hRead, hWrite;
	if (!CreatePipe(&hRead, &hWrite, &sa, 0)) return;

	STARTUPINFOW si = { sizeof(si) };
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdOutput = hWrite;
	si.hStdError = hWrite;

	PROCESS_INFORMATION pi = {};

	std::wstring cmd = L"node.exe \"" + scriptFullPath + L"\"";

	std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back(0);

	if (CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		CloseHandle(hWrite);

		char buffer[4096];
		DWORD bytesRead;
		while (ReadFile(hRead, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead)
		{
			buffer[bytesRead] = 0; // null-terminate
			std::wstring output = std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>>().from_bytes(buffer);
			g_pn->GetGlobalOutputWindow()->AddToolOutput(output.data());
		}

		WaitForSingleObject(pi.hProcess, INFINITE);
		CloseHandle(pi.hProcess);
		CloseHandle(pi.hThread);
		CloseHandle(hRead);
	}
	else
	{
		DWORD err = GetLastError();
		g_pn->GetGlobalOutputWindow()->AddToolOutput(L"Failed to start Node.js\n");
		CloseHandle(hRead);
		CloseHandle(hWrite);
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

bool __stdcall pn_init_extension(int iface_version, extensions::IPN* pn)
{
	if(iface_version != PN_EXT_IFACE_VERSION)
		return false;

	g_pn = pn;
	
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
	//
}

BOOL APIENTRY DllMain( HMODULE hModule,
					   DWORD  ul_reason_for_call,
					   LPVOID lpReserved
					 )
{
	return TRUE;
}
