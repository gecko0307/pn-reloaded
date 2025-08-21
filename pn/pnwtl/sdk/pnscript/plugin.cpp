#include "stdafx.h"
#include <fstream>
#include <vector>
#include <locale>
#include <codecvt>
#include <ShlObj.h>
#include "shellapi.h"
#include "shlwapi.h"
#include "Scintilla.h"
//#include "SciLexer.h"
#include "json/json.h"

extensions::IPN* g_pn = nullptr;

std::string RunNodeScript(const std::wstring& scriptPath, const std::string& inputText)
{
	TCHAR exePath[MAX_PATH];
	GetModuleFileName(NULL, exePath, MAX_PATH);
	PathRemoveFileSpec(exePath);
	std::wstring fullPath = exePath + std::wstring(L"\\") + scriptPath;

	wchar_t tempPath[MAX_PATH];
	if (!GetTempPathW(MAX_PATH, tempPath))
		return "ERROR: Failed to get temp path\n";

	wchar_t tempIn[MAX_PATH], tempOut[MAX_PATH];
	GetTempFileNameW(tempPath, L"pnin", 0, tempIn);
	GetTempFileNameW(tempPath, L"pnout", 0, tempOut);

	std::ofstream ofs(tempIn, std::ios::binary);
	ofs.write(inputText.data(), inputText.size());
	ofs.close();

	std::wstring cmd = L"node.exe \"" + fullPath + L"\" \"" + std::wstring(tempIn) + L"\" \"" + std::wstring(tempOut) + L"\"";
	std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back(0);

	STARTUPINFOW si{};
	PROCESS_INFORMATION pi{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESHOWWINDOW;
	si.wShowWindow = SW_HIDE;

	if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
	{
		return "ERROR: Failed to launch Node.js\n";
	}

	WaitForSingleObject(pi.hProcess, INFINITE);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);

	std::ifstream ifs(tempOut, std::ios::binary);
	std::string result((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

	DeleteFileW(tempIn);
	DeleteFileW(tempOut);

	return result;
}

void RunNodeFilter(const std::wstring& scriptPath)
{
	extensions::IDocumentPtr doc = g_pn->GetCurrentDocument();
	if (!doc) return;

	HWND hWndScintilla = doc->GetScintillaHWND();
	if (!hWndScintilla) return;

	LRESULT length = SendMessage(hWndScintilla, SCI_GETLENGTH, 0, 0);
	std::string input;
	input.resize(static_cast<size_t>(length));

	Scintilla::TextRange tr;
	tr.chrg.cpMin = 0;
	tr.chrg.cpMax = static_cast<LONG>(length);
	tr.lpstrText = &input[0];
	SendMessage(hWndScintilla, SCI_GETTEXTRANGE, 0, (LPARAM)&tr);

	std::string result = RunNodeScript(scriptPath, input);

	extensions::IDocumentPtr newDoc = g_pn->NewDocument(nullptr);
	if (!newDoc) return;

	HWND hNewScintilla = newDoc->GetScintillaHWND();
	if (!hNewScintilla) return;

	SendMessage(hNewScintilla, SCI_SETTEXT, 0, (LPARAM)result.c_str());
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
				RunNodeFilter(scripts[i]);
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

	extensions::IAppEventSinkPtr sink(new PNSEventSink());
	g_pn->AddEventSink(sink);

	std::vector<std::wstring> scripts = GetScriptsFromFolder(L"scripts");
	PNSMenuItems* menuItems = new PNSMenuItems(scripts);
	g_pn->AddPluginMenuItems(menuItems);

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
