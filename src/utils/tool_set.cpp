// =============================================================================
// src/utils/tool_set.cpp   --   Comprehensive built-in tool implementations
// =============================================================================

#include "utils/tool_set.hpp"
#include "indexer/indexer_tools.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "nlohmann/json.hpp"

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <direct.h>
#  include <sys/stat.h>
#else
#  include <dirent.h>
#  include <fnmatch.h>
#  include <sys/stat.h>
#  include <sys/sysinfo.h>
#  include <sys/utsname.h>
#  include <unistd.h>
#  include <pwd.h>
#endif

namespace agent {

// -- Path resolution ----------------------------------------------------------

std::string resolve_path(const std::string& raw) {
    std::string p = raw;
    while (!p.empty() && (p.front()==' '||p.front()=='"'||p.front()=='\'')) p=p.substr(1);
    while (!p.empty() && (p.back() ==' '||p.back() =='"'||p.back() =='\'')) p.pop_back();

    std::string lower = p;
    for (auto& c : lower) if (c>='A'&&c<='Z') c=(char)(c-'A'+'a');

    bool is_desktop = (lower=="desktop"||lower=="桌面"||lower=="~/desktop");

#ifdef _WIN32
    auto expand = [](const std::string& s)->std::string {
        char buf[MAX_PATH]={}; ExpandEnvironmentStringsA(s.c_str(),buf,MAX_PATH); return buf;
    };
    if (is_desktop || lower=="桌面") return expand("%USERPROFILE%\\Desktop");
    auto sw = [&](const std::string& pfx){
        return lower.size()>=pfx.size() && lower.compare(0,pfx.size(),pfx)==0;
    };
    if (sw("<home>")||sw("$home")||sw("~/")||p=="~")
        return expand("%USERPROFILE%") + (p.find('/')!=std::string::npos ?
               "\\"+p.substr(p.find('/')+1) : "");
    if (p.find('%')!=std::string::npos) return expand(p);
    return p;
#else
    const char* he=std::getenv("HOME"); std::string home=he?he:"";
    if (is_desktop) return home.empty()?"Desktop":home+"/Desktop";
    if (p=="~") return home;
    auto sw=[&](const std::string& pfx){
        return lower.size()>=pfx.size()&&lower.compare(0,pfx.size(),pfx)==0;
    };
    if (sw("<home>")||sw("$home")||p.compare(0,2,"~/")==0)
        return home+(p.find('/')!=std::string::npos?"/"+p.substr(p.find('/')+1):"");
    if (p.compare(0,6,"$HOME/")==0) return home+"/"+p.substr(6);
    return p;
#endif
}


static bool is_command_blocked(const std::string& cmd) {
    // Lower-case for matching
    std::string low=cmd;
    for(auto& c:low) if(c>='A'&&c<='Z') c=(char)(c-'A'+'a');
    // Block destructive / dangerous patterns
    static const char* kBlocked[]={
        "rm -rf","del /f /s","del /s /f","format ","mkfs",
        "dd if=","shutdown","reboot","halt","init 0","init 6",
        "rmdir /s","rd /s","> /dev/","> nul",
        "reg delete","regedit",":(){","fork bomb",
        "wget http","curl http","powershell -enc","cmd /c powershell",
        nullptr
    };
    for(int i=0;kBlocked[i];++i)
        if(low.find(kBlocked[i])!=std::string::npos) return true;
    return false;
}


// -- Filesystem ---------------------------------------------------------------

std::string tool_list_dir(const std::string& path) {
    if (path.find("..")!=std::string::npos)
        throw std::runtime_error("list_dir: path traversal denied");
    std::string dir = resolve_path(path);
    std::ostringstream out;

#ifdef _WIN32
    // Use Wide API for correct UTF-8 handling of Chinese directory names
    auto utf8_to_wide = [](const std::string& s) -> std::wstring {
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n <= 0) return L"";
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
        while (!w.empty() && w.back() == L'\0') w.pop_back();
        return w;
    };
    auto wide_to_utf8 = [](const WCHAR* ws) -> std::string {
        int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return "?";
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], n, nullptr, nullptr);
        while (!s.empty() && s.back() == '\0') s.pop_back();
        return s;
    };
    std::wstring wdir = utf8_to_wide(dir);
    std::wstring wpat = wdir + L"\\*";
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(wpat.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) {
        // Fallback: try Desktop
        WCHAR wbuf[MAX_PATH] = {};
        ExpandEnvironmentStringsW(L"%USERPROFILE%\\Desktop", wbuf, MAX_PATH);
        wdir = wbuf; wpat = wdir + L"\\*";
        h = FindFirstFileW(wpat.c_str(), &ffd);
        if (h == INVALID_HANDLE_VALUE)
            throw std::runtime_error("list_dir: cannot open '" + dir + "'");
        dir = wide_to_utf8(wbuf);
    }
    out << "[" << dir << "]\n";
    do {
        std::string name = wide_to_utf8(ffd.cFileName);
        if (name == "." || name == "..") continue;
        bool isdir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        long long sz = isdir ? 0 : (((long long)ffd.nFileSizeHigh << 32) | ffd.nFileSizeLow);
        out << (isdir ? "[DIR] " : "      ") << name;
        if (!isdir) out << "  (" << sz << " bytes)";
        out << "\n";
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
#else
    DIR* dp=opendir(dir.c_str());
    if(!dp){
        const char* he=std::getenv("HOME");
        if(he){std::string fb=std::string(he)+"/Desktop";dp=opendir(fb.c_str());if(dp)dir=fb;}
    }
    if(!dp) throw std::runtime_error("list_dir: cannot open '"+dir+"'. Hint: use \"Desktop\"");
    out<<"Directory: "<<dir<<"\n";
    struct dirent* ep;
    while((ep=readdir(dp))!=nullptr){
        std::string name=ep->d_name;
        if(name=="."||name=="..")continue;
        std::string full=dir+"/"+name;
        struct stat st{}; stat(full.c_str(),&st);
        if(S_ISDIR(st.st_mode)) out<<"[DIR]  "<<name<<"\n";
        else out<<"[FILE] "<<name<<"  ("<<(long long)st.st_size<<" bytes)\n";
    }
    closedir(dp);
#endif
    std::string r=out.str();
    if(r.find("[DIR]")==std::string::npos&&r.find("[FILE]")==std::string::npos)
        r+="(empty directory)\n";
    r += "\n[IMPORTANT: Use exact paths shown above. Never use <HOME>, $HOME, %USERPROFILE%, or /path/to/ placeholders.]";
    return r;
}

std::string tool_run_command(const std::string& cmd) {
    if(cmd.find("..")!=std::string::npos&&cmd.find("cd ")!=std::string::npos)
        throw std::runtime_error("run_command: directory traversal attempt denied");
    if(is_command_blocked(cmd))
        throw std::runtime_error("run_command: command blocked for safety: "+cmd.substr(0,60));

    // Capture command output
    std::string result;
    int exit_code = 0;

#ifdef _WIN32
    // On Windows, use Wide API to handle Chinese paths in commands correctly
    // Prefix with "cmd /u /c " to get Unicode output, then convert
    std::string full_cmd = "cmd /u /c " + cmd + " 2>&1";
    // Convert UTF-8 command to Wide
    int wlen = MultiByteToWideChar(CP_UTF8, 0, full_cmd.c_str(), -1, nullptr, 0);
    std::wstring wcmd(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, full_cmd.c_str(), -1, &wcmd[0], wlen);

    // Create pipe for output
    HANDLE hRead, hWrite;
    SECURITY_ATTRIBUTES sa = {sizeof(sa), nullptr, TRUE};
    if (CreatePipe(&hRead, &hWrite, &sa, 0)) {
        STARTUPINFOW si = {};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = hWrite;
        si.hStdError  = hWrite;
        si.hStdInput  = INVALID_HANDLE_VALUE;
        PROCESS_INFORMATION pi = {};
        if (CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, TRUE,
                           CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            CloseHandle(hWrite);
            // Read all output
            char buf[4096];
            DWORD nRead;
            std::string raw;
            while (ReadFile(hRead, buf, sizeof(buf)-1, &nRead, nullptr) && nRead > 0) {
                raw.append(buf, nRead);
            }
            WaitForSingleObject(pi.hProcess, 30000);
            DWORD ec = 0; GetExitCodeProcess(pi.hProcess, &ec); exit_code = (int)ec;
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            CloseHandle(hRead);
            // cmd /u output is UTF-16LE; convert to UTF-8
            if (raw.size() >= 2) {
                const WCHAR* wout = (const WCHAR*)raw.data();
                int n = WideCharToMultiByte(CP_UTF8, 0, wout, (int)(raw.size()/2),
                                             nullptr, 0, nullptr, nullptr);
                if (n > 0) {
                    result.resize(n);
                    WideCharToMultiByte(CP_UTF8, 0, wout, (int)(raw.size()/2),
                                        &result[0], n, nullptr, nullptr);
                } else {
                    result = raw;  // fallback: use as-is
                }
            } else {
                result = raw;
            }
        } else {
            CloseHandle(hWrite); CloseHandle(hRead);
            // Fallback to _popen if CreateProcess failed
            FILE* fp = _popen((cmd + " 2>&1").c_str(), "r");
            if (fp) {
                char buf2[4096];
                while (fgets(buf2, sizeof(buf2), fp)) result += buf2;
                exit_code = _pclose(fp);
            }
        }
    } else {
        // Pipe creation failed — use _popen
        FILE* fp = _popen((cmd + " 2>&1").c_str(), "r");
        if (fp) {
            char buf2[4096];
            while (fgets(buf2, sizeof(buf2), fp)) result += buf2;
            exit_code = _pclose(fp);
        }
    }
#else
    FILE* fp = popen((cmd + " 2>&1").c_str(), "r");
    if (!fp) throw std::runtime_error("run_command: failed to execute: " + cmd);
    char buf[4096];
    while (fgets(buf, sizeof(buf), fp)) result += buf;
    exit_code = pclose(fp);
#endif

    // Trim trailing whitespace
    while (!result.empty() && (result.back()=='\n'||result.back()=='\r'||result.back()==' '))
        result.pop_back();

    if (result.empty()) result = "(no output)";
    // Cap output to prevent context explosion in LLM calls
    static const size_t kMaxCmdOutput = 4096;
    if (result.size() > kMaxCmdOutput) {
        result = result.substr(0, kMaxCmdOutput) +
                 "\n[OUTPUT TRUNCATED: " + std::to_string(result.size()) + " bytes total]";
    }
    result += "\n[exit code: " + std::to_string(exit_code) + "]";
    result += "\n[Command completed. If output contains useful paths or info, use them in your response.]";
    return result;
}

std::string tool_stat_file(const std::string& path) {
    if(path.find("..")!=std::string::npos)
        throw std::runtime_error("stat_file: path traversal denied");
    std::string p=resolve_path(path);
    std::ostringstream j;

#ifdef _WIN32
    // Wide API for UTF-8 Chinese paths
    WIN32_FILE_ATTRIBUTE_DATA fa;
    {
        int wlen2 = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
        std::vector<wchar_t> wp2(wlen2);
        MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wp2.data(), wlen2);
        if(!GetFileAttributesExW(wp2.data(),GetFileExInfoStandard,&fa)) {
            DWORD e=GetLastError();
            throw std::runtime_error("stat_file: cannot access '"+p+"' (error "+std::to_string(e)+")");
        }
    }
    bool is_dir=(fa.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
    ULONGLONG sz=((ULONGLONG)fa.nFileSizeHigh<<32)|fa.nFileSizeLow;
    SYSTEMTIME st; FileTimeToSystemTime(&fa.ftLastWriteTime,&st);
    char mtime[64];
    snprintf(mtime,sizeof(mtime),"%04d-%02d-%02dT%02d:%02d:%02dZ",
             st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
    j<<"{\"path\":\""<<p<<"\",\"type\":\""<<(is_dir?"dir":"file")
     <<"\",\"size\":"<<(is_dir?0:sz)<<",\"modified\":\""<<mtime<<"\"}";
#else
    struct stat st{};
    if(::stat(p.c_str(),&st)!=0)
        throw std::runtime_error("stat_file: cannot access '"+p+"'");
    bool is_dir=S_ISDIR(st.st_mode);
    struct tm tm_buf{}; gmtime_r(&st.st_mtime,&tm_buf);
    char mtime[64];
    snprintf(mtime,sizeof(mtime),"%04d-%02d-%02dT%02d:%02d:%02dZ",
             tm_buf.tm_year+1900,tm_buf.tm_mon+1,tm_buf.tm_mday,
             tm_buf.tm_hour,tm_buf.tm_min,tm_buf.tm_sec);
    j<<"{\"path\":\""<<p<<"\",\"type\":\""<<(is_dir?"dir":"file")
     <<"\",\"size\":"<<(long long)st.st_size<<",\"modified\":\""<<mtime<<"\"}";
#endif
    return j.str();
}

std::string tool_find_files(const std::string& input) {
    size_t nl=input.find('\n');
    std::string dir_path = (nl==std::string::npos)?".":input.substr(0,nl);
    std::string pattern  = (nl==std::string::npos)?input:input.substr(nl+1);
    if(pattern.empty())pattern="*";
    dir_path=resolve_path(dir_path);
    if(dir_path.find("..")!=std::string::npos)
        throw std::runtime_error("find_files: path traversal denied");

    std::ostringstream out;
    int count=0;
    const int kMaxResults=200;

#ifdef _WIN32
    // Wide API: handles UTF-8 Chinese directory paths on Windows
    std::string _pfx = dir_path+"\\"+pattern;
    int _fwl=MultiByteToWideChar(CP_UTF8,0,_pfx.c_str(),-1,nullptr,0);
    std::vector<wchar_t> _fpw(_fwl);
    MultiByteToWideChar(CP_UTF8,0,_pfx.c_str(),-1,_fpw.data(),_fwl);
    WIN32_FIND_DATAW ffd; HANDLE h=FindFirstFileW(_fpw.data(),&ffd);
    if(h!=INVALID_HANDLE_VALUE){
        do {
            int _nl=WideCharToMultiByte(CP_UTF8,0,ffd.cFileName,-1,nullptr,0,nullptr,nullptr);
            std::string name(_nl,'\0');
            WideCharToMultiByte(CP_UTF8,0,ffd.cFileName,-1,&name[0],_nl,nullptr,nullptr);
            while(!name.empty()&&name.back()=='\0') name.pop_back();
            if(name=="."||name=="..")continue;
            out<<dir_path<<"\\"<<name<<"\n"; ++count;
        } while(FindNextFileW(h,&ffd)&&count<kMaxResults);
        FindClose(h);
    }
#else
    DIR* dp=opendir(dir_path.c_str());
    if(!dp) throw std::runtime_error("find_files: cannot open '"+dir_path+"'");
    struct dirent* ep;
    while((ep=readdir(dp))!=nullptr&&count<kMaxResults){
        std::string name=ep->d_name;
        if(name=="."||name=="..") continue;
        if(fnmatch(pattern.c_str(),name.c_str(),0)==0){
            out<<dir_path<<"/"<<name<<"\n"; ++count;
        }
    }
    closedir(dp);
#endif
    if(count==0) return "(no files matching '"+pattern+"' in '"+dir_path+"')";
    return out.str();
}

std::string tool_read_file(const std::string& path) {
    if(path.find("..")!=std::string::npos)
        throw std::runtime_error("read_file: path traversal denied");
    std::string p=resolve_path(path);

#ifdef _WIN32
    // Wide API: handles UTF-8 Chinese paths correctly (std::ifstream(char*) uses ANSI)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
    if (wlen <= 0) throw std::runtime_error("read_file: path encoding error: " + p);
    std::vector<wchar_t> wp(wlen);
    MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, wp.data(), wlen);
    FILE* fraw = _wfopen(wp.data(), L"rb");
    if (!fraw) throw std::runtime_error("read_file: cannot open: " + p);
    // Read via C FILE* (Wide-opened, so handles all Unicode paths)
    std::fseek(fraw, 0, SEEK_END);
    long fsz = std::ftell(fraw);
    std::fseek(fraw, 0, SEEK_SET);
    const size_t kMaxBytes = 65536;
    size_t to_read = (fsz > 0) ? std::min((size_t)fsz, kMaxBytes) : kMaxBytes;
    std::string content(to_read, '\0');
    size_t nread = std::fread(&content[0], 1, to_read, fraw);
    std::fclose(fraw);
    content.resize(nread);
    if (fsz > (long)kMaxBytes)
        content += "\n\n[TRUNCATED: file is " + std::to_string(fsz) +
                   " bytes; showing first " + std::to_string(kMaxBytes) + " bytes]";
    return content;
#else
    std::ifstream f(p, std::ios::binary);
    if(!f.is_open()) throw std::runtime_error("read_file: cannot open: "+p);
    std::string content((std::istreambuf_iterator<char>(f)),
                         std::istreambuf_iterator<char>());
    const size_t kMaxBytes=65536;
    if(content.size()>kMaxBytes)
        return content.substr(0,kMaxBytes)+
               "\n\n[TRUNCATED: file is "+std::to_string(content.size())+
               " bytes; showing first "+std::to_string(kMaxBytes)+" bytes]";
    return content;
#endif
}

std::string tool_write_file(const std::string& input) {
    size_t nl=input.find('\n');
    if(nl==std::string::npos) throw std::runtime_error("write_file: expected 'path\\ncontent'");
    std::string path=resolve_path(input.substr(0,nl));
    std::string content=input.substr(nl+1);
    if(path.find("..")!=std::string::npos)
        throw std::runtime_error("write_file: '..' not allowed in path");
    // Create parent directories
#ifdef _WIN32
    {
        std::string dir=path;
        size_t pos=dir.rfind('\\'); if(pos==std::string::npos) pos=dir.rfind('/');
        if(pos!=std::string::npos){
            dir=dir.substr(0,pos);
            std::string cmd="mkdir \""+dir+"\" 2>nul";
            (void)system(cmd.c_str());
        }
    }
#else
    {
        std::string dir=path;
        size_t pos=dir.rfind('/');
        if(pos!=std::string::npos){
            dir=dir.substr(0,pos);
            std::string cmd="mkdir -p \""+dir+"\" 2>/dev/null";
            (void)system(cmd.c_str());
        }
    }
#endif
#ifdef _WIN32
    {
        int _wl=MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,nullptr,0);
        std::vector<wchar_t> _wp(_wl);
        MultiByteToWideChar(CP_UTF8,0,path.c_str(),-1,_wp.data(),_wl);
        FILE* _fw=_wfopen(_wp.data(),L"wb");
        if(!_fw) throw std::runtime_error("write_file: cannot open for writing: "+path);
        std::fwrite(content.data(),1,content.size(),_fw);
        std::fclose(_fw);
    }
#else
    std::ofstream f(path);
    if(!f.is_open()) throw std::runtime_error("write_file: cannot open for writing: "+path);
    f<<content;
#endif
    return "Written "+std::to_string(content.size())+" bytes to "+path;
}

std::string tool_delete_file(const std::string& raw_path) {
    std::string path = raw_path;
    // Trim leading/trailing whitespace and quotes
    while (!path.empty() && (path.front()==' '||path.front()=='"')) path=path.substr(1);
    while (!path.empty() && (path.back() ==' '||path.back() =='"')) path.pop_back();

    if(path.find("..")!=std::string::npos)
        throw std::runtime_error("delete_file: path traversal denied");

    // HITL: require explicit confirmation prefix "CONFIRMED:<path>"
    static const char* CONFIRM = "CONFIRMED:";
    if (path.rfind(CONFIRM, 0) != 0) {
        // Return a special string — NOT an error, but a confirmation request
        return "[HITL] delete_file requires confirmation.\n"
               "This will permanently delete: " + path + "\n"
               "To proceed, reissue the task with input: CONFIRMED:" + path + "\n"
               "To cancel, respond with a different action.";
    }
    path = path.substr(std::strlen(CONFIRM));
    std::string p = resolve_path(path);

#ifdef _WIN32
    {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, nullptr, 0);
        if (wlen > 0) {
            std::wstring wp(wlen, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, p.c_str(), -1, &wp[0], wlen);
            if (!DeleteFileW(wp.c_str()))
                throw std::runtime_error("delete_file: failed (error " +
                    std::to_string(GetLastError()) + ")");
        }
    }
#else
    if (::remove(p.c_str()) != 0)
        throw std::runtime_error("delete_file: failed");
#endif
    return "Deleted: " + p;
}

// -- System API ---------------------------------------------------------------

std::string tool_get_env(const std::string& name) {
    const char* v=std::getenv(name.c_str());
    return v?std::string(v):"(not set)";
}

std::string tool_get_sysinfo(const std::string&) {
    std::ostringstream j;
    j<<"{";
#ifdef _WIN32
    char buf[256]={};
    DWORD sz=sizeof(buf);
    GetComputerNameA(buf,&sz); std::string hostname=buf;
    sz=sizeof(buf); GetUserNameA(buf,&sz); std::string username=buf;

    SYSTEM_INFO si{}; GetSystemInfo(&si);
    int ncpu=(int)si.dwNumberOfProcessors;

    MEMORYSTATUSEX ms{}; ms.dwLength=sizeof(ms); GlobalMemoryStatusEx(&ms);
    long long total_mb=(long long)(ms.ullTotalPhys/1048576);
    long long avail_mb=(long long)(ms.ullAvailPhys/1048576);

    OSVERSIONINFOEXW ov{}; ov.dwOSVersionInfoSize=sizeof(ov);
    // Use RtlGetVersion via pointer to avoid deprecation
    typedef LONG(WINAPI* RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll=GetModuleHandleW(L"ntdll.dll");
    std::string os_ver="Windows";
    if(ntdll){
        auto fn=(RtlGetVersionPtr)GetProcAddress(ntdll,"RtlGetVersion");
        if(fn&&fn((PRTL_OSVERSIONINFOW)&ov)==0)
            os_ver="Windows "+std::to_string(ov.dwMajorVersion)+"."+
                   std::to_string(ov.dwMinorVersion)+" build "+
                   std::to_string(ov.dwBuildNumber);
    }
    j<<"\"os\":\""<<os_ver<<"\","
     <<"\"hostname\":\""<<hostname<<"\","
     <<"\"username\":\""<<username<<"\","
     <<"\"cpu_count\":"<<ncpu<<","
     <<"\"total_memory_mb\":"<<total_mb<<","
     <<"\"avail_memory_mb\":"<<avail_mb;
#else
    struct utsname un{}; uname(&un);
    struct passwd* pw=getpwuid(getuid());
    std::string username=pw?pw->pw_name:std::getenv("USER")?std::getenv("USER"):"unknown";
    int ncpu=(int)sysconf(_SC_NPROCESSORS_ONLN);
    struct sysinfo si{}; sysinfo(&si);
    long long total_mb=(long long)(si.totalram*(unsigned long long)si.mem_unit/1048576);
    long long avail_mb=(long long)(si.freeram*(unsigned long long)si.mem_unit/1048576);
    j<<"\"os\":\""<<un.sysname<<" "<<un.release<<"\","
     <<"\"hostname\":\""<<un.nodename<<"\","
     <<"\"username\":\""<<username<<"\","
     <<"\"cpu_count\":"<<ncpu<<","
     <<"\"total_memory_mb\":"<<total_mb<<","
     <<"\"avail_memory_mb\":"<<avail_mb;
#endif
    j<<"}";
    return j.str();
}

std::string tool_get_process_list(const std::string& filter) {
    std::ostringstream out;
    int count=0;
#ifdef _WIN32
    // Use "tasklist"  --  avoids tlhelp32.h (incompatible with WIN32_LEAN_AND_MEAN)
    FILE* pipe = _popen("tasklist /fo csv /nh 2>nul", "r");
    if (pipe) {
        char buf[512];
        while (fgets(buf, sizeof(buf), pipe) && count < 200) {
            std::string line = buf;
            // CSV format: "name.exe","PID",...
            // Extract name and PID
            if (line.size() > 2 && line[0] == '"') {
                size_t end_name = line.find('"', 1);
                size_t start_pid = line.find('"', end_name + 2);
                size_t end_pid   = (start_pid != std::string::npos)
                                 ? line.find('"', start_pid + 1) : std::string::npos;
                std::string name = (end_name != std::string::npos)
                                 ? line.substr(1, end_name - 1) : "";
                std::string pid  = (start_pid != std::string::npos && end_pid != std::string::npos)
                                 ? line.substr(start_pid + 1, end_pid - start_pid - 1) : "";
                if (!name.empty() &&
                    (filter.empty() || name.find(filter) != std::string::npos)) {
                    out << pid << "  " << name << "\n";
                    ++count;
                }
            }
        }
        _pclose(pipe);
    }
#else
    DIR* dp=opendir("/proc");
    if(!dp) throw std::runtime_error("get_process_list: cannot read /proc");
    struct dirent* ep;
    while((ep=readdir(dp))!=nullptr&&count<200){
        // Check if the name is all digits (PID)
        bool is_pid=true;
        for(char c:std::string(ep->d_name)) if(!isdigit((unsigned char)c)){is_pid=false;break;}
        if(!is_pid)continue;
        std::string comm_path=std::string("/proc/")+ep->d_name+"/comm";
        std::ifstream cf(comm_path); std::string name;
        if(cf){std::getline(cf,name);}else continue;
        if(filter.empty()||name.find(filter)!=std::string::npos){
            out<<ep->d_name<<"  "<<name<<"\n"; ++count;
        }
    }
    closedir(dp);
#endif
    return count>0?out.str():"(no matching processes)";
}

std::string tool_get_current_dir(const std::string&) {
#ifdef _WIN32
    char buf[MAX_PATH]={}; GetCurrentDirectoryA(MAX_PATH,buf); return buf;
#else
    char buf[4096]={}; getcwd(buf,sizeof(buf)); return buf;
#endif
}

// -- Shell (sandboxed) --------------------------------------------------------



std::string tool_echo(const std::string& input) { return input; }

// -- Registration & documentation ---------------------------------------------

void register_all_tools(ToolRegistry& registry) {
    // Filesystem
    registry.register_tool("list_dir",       tool_list_dir);
    registry.register_tool("stat_file",      tool_stat_file);
    registry.register_tool("find_files",     tool_find_files);
    registry.register_tool("read_file",      tool_read_file);
    registry.register_tool("write_file",     tool_write_file);
    registry.register_tool("delete_file",    tool_delete_file);
    // System
    registry.register_tool("get_env",        tool_get_env);
    registry.register_tool("get_sysinfo",    tool_get_sysinfo);
    registry.register_tool("get_process_list", tool_get_process_list);
    registry.register_tool("get_current_dir", tool_get_current_dir);
    // Shell
    registry.register_tool("run_command",    tool_run_command);
    // Utility
    registry.register_tool("echo",           tool_echo);
    // Code Intelligence
    indexer::register_indexer_tools(registry);

    // Meta-tool: create new tools at runtime
    registry.register_tool("create_tool",
        [](const std::string& input) -> std::string {
            // Input format: "tool_name\ndescription\nshell_command_template"
            // {INPUT} in command is replaced with the tool's input at call time
            auto nl1 = input.find('\n');
            if (nl1 == std::string::npos)
                throw std::runtime_error("create_tool: expected 'name\ndescription\ncommand'");
            auto nl2 = input.find('\n', nl1+1);
            if (nl2 == std::string::npos)
                throw std::runtime_error("create_tool: expected 'name\ndescription\ncommand'");
            std::string name  = input.substr(0, nl1);
            std::string desc  = input.substr(nl1+1, nl2-nl1-1);
            std::string cmd_t = input.substr(nl2+1);
            // Validate name
            for (char c : name)
                if (!std::isalnum((unsigned char)c) && c!='_')
                    throw std::runtime_error("create_tool: name must be alphanumeric+underscore");
            // Persist tool definition to workspace
            std::string tools_dir = "./workspace/tools";
#ifdef _WIN32
            _mkdir(tools_dir.c_str());
#else
            ::mkdir(tools_dir.c_str(), 0755);
#endif
            std::string def_path = tools_dir + "/" + name + ".json";
            nlohmann::json def;
            def["name"] = name; def["description"] = desc; def["command"] = cmd_t;
            std::ofstream f(def_path);
            if (f.is_open()) f << def.dump(2) << "\n";
            return "Tool '" + name + "' created. Definition saved to " + def_path + "\n"
                   "Usage: call " + name + " with your input. "
                   "Command template: " + cmd_t.substr(0, 80);
        }
    );

    // Meta-tool: list dynamically created tools
    registry.register_tool("list_tools",
        [](const std::string&) -> std::string {
            std::ostringstream out;
            out << "Built-in tools:\n"
                   "  file: list_dir read_file write_file stat_file find_files delete_file\n"
                   "  sys:  get_env get_sysinfo get_process_list get_current_dir run_command\n"
                   "  meta: echo create_tool list_tools\n";
            out << "\nDynamic tools (./workspace/tools/):\n";
            // List JSON files in workspace/tools/
            std::string td = "./workspace/tools";
#ifdef _WIN32
            WIN32_FIND_DATAA ffd; HANDLE h=FindFirstFileA((td+"\\*.json").c_str(),&ffd);
            if (h!=INVALID_HANDLE_VALUE) {
                do { out<<"  "<<ffd.cFileName<<"\n"; } while(FindNextFileA(h,&ffd));
                FindClose(h);
            }
#else
            DIR* dp=opendir(td.c_str());
            if (dp) {
                struct dirent* ep;
                while ((ep=readdir(dp))) {
                    std::string n=ep->d_name;
                    if (n.size()>5 && n.substr(n.size()-5)==".json") out<<"  "<<n<<"\n";
                }
                closedir(dp);
            }
#endif
            return out.str();
        }
    );
}




// -- build_tool_doc -----------------------------------------------------------
// Generate LLM-facing documentation string for the given tool names

std::string build_tool_doc(const std::vector<std::string>& tool_names) {
    // Map of tool -> documentation line
    static const std::pair<const char*, const char*> DOCS[] = {
        {"list_dir",        "list_dir(path)         - list directory contents (use \".\" for current, \"..\" for parent)"},
        {"stat_file",       "stat_file(path)         - get file size, type, modification time"},
        {"find_files",      "find_files(dir\npattern) - search for files matching a pattern"},
        {"read_file",       "read_file(path)         - read file text content (up to 64KB)"},
        {"write_file",      "write_file(path\ncontent) - write content to file"},
        {"delete_file",     "delete_file(path)       - delete a file"},
        {"get_env",         "get_env(VAR_NAME)       - read environment variable"},
        {"get_sysinfo",     "get_sysinfo()           - get OS, CPU, memory information"},
        {"get_process_list","get_process_list()      - list running processes"},
        {"get_current_dir", "get_current_dir()       - get current working directory path"},
        {"run_command",     "run_command(cmd)        - execute shell command, capture output"},
        {"echo",            "echo(text)              - return text as-is (for testing)"},
        {nullptr, nullptr}
    };

    std::string doc = "\n\nAVAILABLE TOOLS:\n";
    for (const auto& name : tool_names) {
        for (int i = 0; DOCS[i].first; ++i) {
            if (name == DOCS[i].first) {
                doc += "  ";
                doc += DOCS[i].second;
                doc += "\n";
                break;
            }
        }
    }
    return doc;
}

// Helper: load dynamically created tools from workspace/tools/
void load_dynamic_tools(ToolRegistry& registry) {
    std::string td = "./workspace/tools";
#ifdef _WIN32
    // List *.json files using Wide API
    int wlen = MultiByteToWideChar(CP_UTF8, 0, (td + "\\*.json").c_str(), -1, nullptr, 0);
    std::wstring wpat(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, (td + "\\*.json").c_str(), -1, &wpat[0], wlen);
    WIN32_FIND_DATAW ffd;
    HANDLE h = FindFirstFileW(wpat.c_str(), &ffd);
    if (h == INVALID_HANDLE_VALUE) return;
    auto w2u = [](const WCHAR* ws) -> std::string {
        int n = WideCharToMultiByte(CP_UTF8,0,ws,-1,nullptr,0,nullptr,nullptr);
        if (n <= 0) return "";
        std::string s(n, '\0');
        WideCharToMultiByte(CP_UTF8,0,ws,-1,&s[0],n,nullptr,nullptr);
        while (!s.empty() && s.back()=='\0') s.pop_back(); return s;
    };
    do {
        std::string fname = w2u(ffd.cFileName);
        std::ifstream f(td + "\\" + fname);
#else
    DIR* dp = opendir(td.c_str());
    if (!dp) return;
    struct dirent* ep;
    while ((ep = readdir(dp))) {
        std::string fname = ep->d_name;
        if (fname.size() < 5 || fname.substr(fname.size()-5) != ".json") continue;
        std::ifstream f(td + "/" + fname);
#endif
        if (!f.is_open()) { continue; }
        try {
            std::string jcontent((std::istreambuf_iterator<char>(f)),
                                    std::istreambuf_iterator<char>());
            nlohmann::json def = nlohmann::json::parse(jcontent);
            std::string name  = def["name"].get<std::string>();
            std::string cmd_t = def["command"].get<std::string>();
            if (name.empty() || cmd_t.empty()) continue;
            if (registry.has_tool(name)) continue;  // already registered
            // Register a shell-backed tool: replaces {INPUT} with actual input
            registry.register_tool(name,
                [cmd_t](const std::string& in) -> std::string {
                    std::string cmd = cmd_t;
                    // Replace {INPUT} placeholder with escaped input
                    std::string escaped;
                    for (char c : in) {
                        if (c == '"' || c == '\\' || c == '\'' )
                            escaped += '\\';
                        escaped += c;
                    }
                    size_t pos = 0;
                    while ((pos = cmd.find("{INPUT}", pos)) != std::string::npos) {
                        cmd.replace(pos, 7, escaped);
                        pos += escaped.size();
                    }
                    return tool_run_command(cmd);
                });
        } catch (...) {}
#ifdef _WIN32
    } while (FindNextFileW(h, &ffd));
    FindClose(h);
#else
    }
    closedir(dp);
#endif
}
} // namespace agent
