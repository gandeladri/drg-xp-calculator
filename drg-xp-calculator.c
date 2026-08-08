// DRG XP Calculator - native Win32 GUI for tracking Deep Rock Galactic class XP.
// Copyright (c) 2026 Adrian Gandelman. Licensed under MIT (see LICENSE).
// https://github.com/gandeladri/drg-xp-calculator

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <string.h>

#define ID_EDIT_LEVEL       1001
#define ID_EDIT_XP          1002
#define ID_BUTTON_CALC      1003
#define ID_RESULT           1004
#define ID_TOTAL            1005

// --- Save file reader: reads level/XP per class from the real DRG save ---
#define ID_BUTTON_LOADSAVE  2001
#define ID_SAVE_RESULT      2002
#define ID_BUTTON_CLASS_ENGINEER 2003
#define ID_BUTTON_CLASS_SCOUT    2004
#define ID_BUTTON_CLASS_DRILLER  2005
#define ID_BUTTON_CLASS_GUNNER   2006
#define ID_EDIT_SAVEPATH     2007
#define ID_BUTTON_APPLYPATH  2008
#define ID_BUTTON_EDITPATH   2009
#define ID_BUTTON_CANCELPATH 2010
#define ID_PATH_LABEL        2011
#define ID_TIMER_AUTOSYNC   9001
#define IDI_APP_ICON         MAKEINTRESOURCEW(101)

#define RGBX(r,g,b) RGB(r,g,b)

// SetProcessDpiAwarenessContext is only present on Windows 10 1607+; resolve it the
// conventional way (GetModuleHandle + GetProcAddress on an already-loaded system DLL)
// instead of hard-linking, so the app still starts cleanly on older Windows.
static BOOL (WINAPI *pSetProcessDpiAwarenessContext)(HANDLE);

static HINSTANCE g_instance;
static HWND g_main, g_title, g_labelLevel, g_editLevel, g_labelXp, g_editXp, g_button, g_result, g_total;
static HWND g_resultColLevel, g_resultColXp;
static HWND g_loadSaveBtn, g_saveResult, g_classBtn[4], g_editSavePath, g_applyPathBtn;
static HWND g_savePathLabel, g_editPathBtn, g_cancelPathBtn;
static int g_pathEditMode=0;
static HBRUSH g_brushMain, g_brushInput, g_brushAccent, g_brushAccentPressed;
static HBRUSH g_brushField, g_brushCardBorder, g_brushChipBorder, g_brushChipSelected;
static HFONT g_font, g_fontTitle, g_fontTotal, g_fontSmall, g_fontColHeader;
static WCHAR g_totalText[128];
static DWORD g_classLevel[4], g_classXpInLevel[4], g_classTotalXp[4];
static int g_classFound[4];
static WCHAR g_classChipLine2[4][24], g_classChipLine3[4][48], g_classChipLine4[4][48];
static int g_lastClassSelected=-1; // -1 = last calc was manual entry, 0-3 = last calc came from that class button
static RECT g_cardCalcRect, g_cardSaveRect;

static const DWORD XP_TO_NEXT[26] = {
    0,
    3000, 4000, 5000, 6000, 7000, 8000, 9000, 10000, 11000, 12000,
    13000, 14000, 15000, 15500, 16000, 16500, 17000, 17500, 18000, 18500,
    19000, 19500, 20000, 20500,
    0
};

static unsigned int wstrlen16(const WCHAR* s){ unsigned int n=0; while(s[n]) n++; return n; }

// Resolves SetProcessDpiAwarenessContext conventionally (it may not exist on Windows < 10 1607).
static void init_optional_apis(void){
    HMODULE u=GetModuleHandleW(L"user32.dll");
    if(u) pSetProcessDpiAwarenessContext=(void*)GetProcAddress(u,"SetProcessDpiAwarenessContext");
}

static void set_font(HWND h, HFONT f){ if(h && f) SendMessageW(h,WM_SETFONT,(WPARAM)f,TRUE); }

// Clips a native control (EDIT/STATIC) to a rounded-corner region; must be reapplied whenever
// the control is resized, since the region is sized in local (0,0,w,h) coordinates.
static void round_control(HWND h, int w, int hgt, int radius){
    if(!h || w<=0 || hgt<=0) return;
    HANDLE rgn=CreateRoundRectRgn(0,0,w+1,hgt+1,radius,radius);
    SetWindowRgn(h,rgn,TRUE);
}

// Fills a rounded rect with `border` color, then an inset rounded rect with `fill` color on top,
// producing a rounded panel with a thin border ring using nothing but flat brush fills.
static void draw_round_panel(HDC dc, RECT rc, int radius, HBRUSH border, HBRUSH fill, int borderPx){
    SelectObject(dc,(HGDIOBJ)GetStockObject(NULL_PEN));
    SelectObject(dc,(HGDIOBJ)border);
    RoundRect(dc,rc.left,rc.top,rc.right,rc.bottom,radius,radius);
    if(fill){
        SelectObject(dc,(HGDIOBJ)fill);
        RoundRect(dc,rc.left+borderPx,rc.top+borderPx,rc.right-borderPx,rc.bottom-borderPx,radius>borderPx?radius-borderPx:1,radius>borderPx?radius-borderPx:1);
    }
}

static unsigned int append_w(WCHAR* dst,unsigned int pos,unsigned int cap,const WCHAR* s){
    unsigned int i=0; while(s[i] && pos+1<cap) dst[pos++]=s[i++]; dst[pos]=0; return pos;
}
static unsigned int append_ch(WCHAR* dst,unsigned int pos,unsigned int cap,WCHAR c){ if(pos+1<cap){dst[pos++]=c;dst[pos]=0;} return pos; }
static unsigned int append_uint(WCHAR* dst,unsigned int pos,unsigned int cap,DWORD v){
    WCHAR tmp[16]; int n=0; if(v==0) tmp[n++]='0'; else { while(v && n<15){tmp[n++]=(WCHAR)('0'+(v%10));v/=10;} }
    while(n--) pos=append_ch(dst,pos,cap,tmp[n]); return pos;
}
static unsigned int append_thousands(WCHAR* dst,unsigned int pos,unsigned int cap,DWORD v){
    WCHAR rev[24]; int n=0, digits=0;
    if(v==0) return append_ch(dst,pos,cap,'0');
    while(v && n<23){ if(digits==3){rev[n++]='.';digits=0;} rev[n++]=(WCHAR)('0'+v%10); v/=10; digits++; }
    while(n--) pos=append_ch(dst,pos,cap,rev[n]); return pos;
}

static int parse_uint_strict(HWND edit, DWORD* out){
    WCHAR buf[32]; int n=GetWindowTextW(edit,buf,31); if(n<=0) return 0;
    DWORD v=0; for(int i=0;i<n;i++){ if(buf[i]<'0'||buf[i]>'9') return 0; DWORD d=buf[i]-'0'; if(v>100000000) return 0; v=v*10+d; }
    *out=v; return 1;
}

static void show_error(const WCHAR* msg){
    static const WCHAR title[]={'I','n','v','a','l','i','d',' ','D','a','t','a',0};
    MessageBoxW(g_main,msg,title,MB_OK|MB_ICONWARNING);
}

static void calculate(void){
    static const WCHAR errLevel[]={'E','n','t','e','r',' ','a',' ','v','a','l','i','d',' ','l','e','v','e','l',' ','b','e','t','w','e','e','n',' ','1',' ','a','n','d',' ','2','5','.',0};
    static const WCHAR errXp[]={'E','n','t','e','r',' ','a',' ','v','a','l','i','d',' ','X','P',' ','v','a','l','u','e','.',0};
    static const WCHAR errXpHigh[]={'C','u','r','r','e','n','t',' ','X','P',' ','i','s',' ','t','o','o',' ','h','i','g','h',' ','f','o','r',' ','t','h','a','t',' ','l','e','v','e','l','.',0};
    static const WCHAR totalZero[]={'T','O','T','A','L',' ','T','O',' ','L','E','V','E','L',' ','2','5',':',' ','0',' ','X','P',0};
    static const WCHAR lvPrefix[]={'L','v','.',' ',0};
    static const WCHAR totalPrefix[]={'T','O','T','A','L',' ','T','O',' ','L','E','V','E','L',' ','2','5',':',' ',0};
    static const WCHAR xpOnly[]={' ','X','P',0};

    DWORD level,xp;
    if(!parse_uint_strict(g_editLevel,&level) || level<1 || level>25){ show_error(errLevel); SetFocus(g_editLevel); return; }
    if(!parse_uint_strict(g_editXp,&xp)){ show_error(errXp); SetFocus(g_editXp); return; }
    SendMessageW(g_result,LVM_DELETEALLITEMS,0,0);
    if(level==25){ SetWindowTextW(g_total,totalZero); return; }
    if(xp>=XP_TO_NEXT[level]){ show_error(errXpHigh); SetFocus(g_editXp); return; }

    DWORD cumulative=XP_TO_NEXT[level]-xp;
    int row=0;
    for(DWORD target=level+1; target<=25; target++){
        WCHAR lvBuf[16]; unsigned int lp=0;
        lp=append_w(lvBuf,lp,16,lvPrefix);
        if(target<10) lp=append_ch(lvBuf,lp,16,'0');
        lp=append_uint(lvBuf,lp,16,target);
        WCHAR xpBuf[24]; unsigned int xpp=0;
        xpp=append_thousands(xpBuf,xpp,24,cumulative);
        xpp=append_w(xpBuf,xpp,24,xpOnly);

        LVITEMW it; memset(&it,0,sizeof(it));
        it.mask=LVIF_TEXT; it.iItem=row; it.iSubItem=0; it.pszText=lvBuf;
        SendMessageW(g_result,LVM_INSERTITEMW,0,(LPARAM)&it);
        LVITEMW sub; memset(&sub,0,sizeof(sub));
        sub.iSubItem=1; sub.pszText=xpBuf;
        SendMessageW(g_result,LVM_SETITEMTEXTW,(WPARAM)row,(LPARAM)&sub);

        row++;
        if(target<25) cumulative+=XP_TO_NEXT[target];
    }

    unsigned int t=0; g_totalText[0]=0;
    t=append_w(g_totalText,t,128,totalPrefix);
    t=append_thousands(g_totalText,t,128,cumulative);
    t=append_w(g_totalText,t,128,xpOnly);
    SetWindowTextW(g_total,g_totalText);
}

// --- Save file reader: reads level/XP per class from a user-picked DRG save file ---
static WCHAR g_savePath[1024]={0};
static WCHAR g_cachePath[1024]={0};
static FILETIME g_lastWriteTime={0,0};
static int g_haveSavePath=0;

static int pick_save_file(void){
    static const WCHAR filter[]={'S','a','v','e',' ','F','i','l','e','s',' ','(','*','.','s','a','v',')',0,'*','.','s','a','v',0,'A','l','l',' ','F','i','l','e','s',' ','(','*','.','*',')',0,'*','.','*',0,0};
    WCHAR pickedPath[1024];
    OPENFILENAMEW ofn; memset(&ofn,0,sizeof(ofn));
    memset(pickedPath,0,sizeof(pickedPath));
    for(unsigned int i=0;i<1023 && g_savePath[i];i++) pickedPath[i]=g_savePath[i];
    ofn.lStructSize=sizeof(ofn);
    ofn.hwndOwner=g_main;
    ofn.lpstrFilter=filter;
    ofn.lpstrFile=pickedPath;
    ofn.nMaxFile=1024;
    ofn.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST;
    if(!GetOpenFileNameW(&ofn)) return 0;
    for(unsigned int i=0;i<1024;i++){
        g_savePath[i]=pickedPath[i];
        if(!pickedPath[i]) break;
    }
    return 1;
}

// Cache file: "%LOCALAPPDATA%\DrgXpCalculator\savepath.cfg", holds the last-picked save path as
// raw UTF-16. Stored per-user instead of next to the executable, so the app doesn't need write
// access to its own install directory.
static void build_cache_path(void){
    static const WCHAR appDirName[]={'D','r','g','X','p','C','a','l','c','u','l','a','t','o','r',0};
    static const WCHAR cacheName[]={'s','a','v','e','p','a','t','h','.','c','f','g',0};
    WCHAR base[900];
    DWORD n=GetEnvironmentVariableW(L"LOCALAPPDATA",base,900);
    if(n==0 || n>=900) return;
    unsigned int pos=0;
    for(unsigned int i=0;i<n;i++) g_cachePath[pos++]=base[i];
    g_cachePath[pos++]='\\';
    for(unsigned int i=0;appDirName[i];i++) g_cachePath[pos++]=appDirName[i];
    g_cachePath[pos]=0;
    CreateDirectoryW(g_cachePath,NULL);
    g_cachePath[pos++]='\\';
    for(unsigned int i=0;cacheName[i];i++) g_cachePath[pos++]=cacheName[i];
    g_cachePath[pos]=0;
}

static void save_cached_path(void){
    HANDLE h=CreateFileW(g_cachePath,0x40000000L /*GENERIC_WRITE*/,0,NULL,2 /*CREATE_ALWAYS*/,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE || h==NULL) return;
    DWORD len=wstrlen16(g_savePath)*2;
    DWORD written=0;
    WriteFile(h,g_savePath,len,&written,NULL);
    CloseHandle(h);
}

static int load_cached_path(void){
    HANDLE h=CreateFileW(g_cachePath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE || h==NULL) return 0;
    DWORD size=GetFileSize(h,NULL);
    if(size==0 || size>=sizeof(g_savePath)){ CloseHandle(h); return 0; }
    DWORD got=0;
    BOOL ok=ReadFile(h,g_savePath,size,&got,NULL);
    CloseHandle(h);
    if(!ok || got==0) return 0;
    g_savePath[got/2]=0;
    return 1;
}

// Tries the default Steam install location for DRG's SaveGames folder and picks the
// first "<steamid>_Player.sav" found inside it, since the steam_id subfolder name varies
// per user. Returns 1 and fills g_savePath on success.
static int try_default_save_path(void){
    static const WCHAR baseDir[]={
        'C',':','\\','P','r','o','g','r','a','m',' ','F','i','l','e','s',' ','(','x','8','6',')','\\',
        'S','t','e','a','m','\\','s','t','e','a','m','a','p','p','s','\\','c','o','m','m','o','n','\\',
        'D','e','e','p',' ','R','o','c','k',' ','G','a','l','a','c','t','i','c','\\',
        'F','S','D','\\','S','a','v','e','d','\\','S','a','v','e','G','a','m','e','s','\\',0};
    static const WCHAR searchAll[]={'*',0};
    WCHAR search[900];
    unsigned int pos=0;
    for(unsigned int i=0;baseDir[i];i++) search[pos++]=baseDir[i];
    for(unsigned int i=0;searchAll[i];i++) search[pos++]=searchAll[i];
    search[pos]=0;

    WIN32_FIND_DATAW fd;
    HANDLE h=FindFirstFileW(search,&fd);
    if(h==INVALID_HANDLE_VALUE) return 0;
    int found=0;
    do{
        if(!(fd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)) continue;
        if(fd.cFileName[0]=='.') continue;
        unsigned int p=0;
        for(unsigned int i=0;baseDir[i];i++) g_savePath[p++]=baseDir[i];
        for(unsigned int i=0;fd.cFileName[i];i++) g_savePath[p++]=fd.cFileName[i];
        static const WCHAR playerSav[]={'\\','P','l','a','y','e','r','.','s','a','v',0};
        for(unsigned int i=0;playerSav[i];i++) g_savePath[p++]=playerSav[i];
        g_savePath[p]=0;
        DWORD attrs=GetFileAttributesW(g_savePath);
        if(attrs!=INVALID_FILE_ATTRIBUTES && !(attrs&FILE_ATTRIBUTE_DIRECTORY)){ found=1; break; }
    } while(FindNextFileW(h,&fd));
    FindClose(h);
    if(!found) g_savePath[0]=0;
    return found;
}

static int get_save_write_time(FILETIME* out){
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!GetFileAttributesExW(g_savePath,GetFileExInfoStandard,&data)) return 0;
    *out=data.ftLastWriteTime;
    return 1;
}

static BYTE g_saveBuf[4*1024*1024];

static const BYTE GUID_ENGINEER[16]={0x85,0xEF,0x62,0x6C,0x65,0xF1,0x02,0x4A,0x8D,0xFE,0xB5,0xD0,0xF3,0x90,0x9D,0x2E};
static const BYTE GUID_SCOUT[16]   ={0x30,0xD8,0xEA,0x17,0xD8,0xFB,0xBA,0x4C,0x95,0x30,0x6D,0xE9,0x65,0x5C,0x2F,0x8C};
static const BYTE GUID_DRILLER[16] ={0x9E,0xDD,0x56,0xF1,0xEE,0xBC,0xC5,0x48,0x8D,0x5B,0x5E,0x5B,0x80,0xB6,0x2D,0xB4};
static const BYTE GUID_GUNNER[16]  ={0xAE,0x56,0xE1,0x80,0xFE,0xC0,0xC4,0x4D,0x96,0xFA,0x29,0xC2,0x83,0x66,0xB9,0x7B};
static const WCHAR NAME_ENGINEER[]={'E','n','g','i','n','e','e','r',0};
static const WCHAR NAME_SCOUT[]={'S','c','o','u','t',0};
static const WCHAR NAME_DRILLER[]={'D','r','i','l','l','e','r',0};
static const WCHAR NAME_GUNNER[]={'G','u','n','n','e','r',0};

// GVAS layout after a class GUID for the "XP" IntProperty:
// GUID(16) + len3 "XP\0"(7) + len12 "IntProperty\0"(16) + int64 valuelen=4(8) + bHasPropertyGuid flag(1)
// = 48 bytes, then the actual int32 value.
static const BYTE XP_TEMPLATE_TAIL[32]={
    0x03,0,0,0,'X','P',0,
    0x0c,0,0,0,'I','n','t','P','r','o','p','e','r','t','y',0,
    0x04,0,0,0,0,0,0,0,
    0
};

static int memcmpb(const BYTE* a,const BYTE* b,unsigned int n){
    for(unsigned int i=0;i<n;i++){ if(a[i]!=b[i]) return 1; }
    return 0;
}

static int find_class_xp(const BYTE* buf, unsigned int bufLen, const BYTE* guid, DWORD* outXp){
    unsigned int patLen=16+32;
    if(bufLen<patLen+4) return 0;
    for(unsigned int i=0;i+patLen+4<=bufLen;i++){
        if(memcmpb(buf+i,guid,16)!=0) continue;
        if(memcmpb(buf+i+16,XP_TEMPLATE_TAIL,32)!=0) continue;
        const BYTE* v=buf+i+patLen;
        *outXp=(DWORD)v[0]|((DWORD)v[1]<<8)|((DWORD)v[2]<<16)|((DWORD)v[3]<<24);
        return 1;
    }
    return 0;
}

static void total_xp_to_level(DWORD total, DWORD* outLevel, DWORD* outProgress){
    DWORD cumulative=0;
    for(DWORD lvl=1; lvl<25; lvl++){
        DWORD next=cumulative+XP_TO_NEXT[lvl];
        if(total<next){ *outLevel=lvl; *outProgress=total-cumulative; return; }
        cumulative=next;
    }
    *outLevel=25; *outProgress=0;
}

static void redraw_class_chips(void);

// Reads whatever file g_savePath currently points at and refreshes the display.
// Used both for the initial manual load and for silent auto-sync reloads.
static int read_and_display_save(void){
    static const WCHAR errOpen[]={'C','o','u','l','d',' ','n','o','t',' ','o','p','e','n',' ','s','e','l','e','c','t','e','d',' ','s','a','v','e',' ','f','i','l','e','.',0};
    static const WCHAR lvPrefix[]={'L','v','.',0};
    static const WCHAR totalSuffix[]={' ','t','o','t','a','l',0};
    static const WCHAR intoLvlSuffix[]={' ','i','n','t','o',' ','l','v','l',0};
    static const WCHAR notFoundLine[]={' ','n','o','t',' ','f','o','u','n','d',' ','i','n',' ','s','a','v','e',0};
    static const WCHAR sep[]={',',' ',0};

    HANDLE h=CreateFileW(g_savePath,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if(h==INVALID_HANDLE_VALUE || h==NULL){
        SetWindowTextW(g_saveResult,errOpen);
        return 0;
    }
    DWORD size=GetFileSize(h,NULL);
    DWORD toRead = size>sizeof(g_saveBuf) ? (DWORD)sizeof(g_saveBuf) : size;
    DWORD got=0;
    ReadFile(h,g_saveBuf,toRead,&got,NULL);
    CloseHandle(h);

    const BYTE* guids[4]={GUID_ENGINEER,GUID_SCOUT,GUID_DRILLER,GUID_GUNNER};
    const WCHAR* names[4]={NAME_ENGINEER,NAME_SCOUT,NAME_DRILLER,NAME_GUNNER};
    static WCHAR status[256]; unsigned int spos=0; status[0]=0; int anyMissing=0;
    for(int c=0;c<4;c++){
        DWORD total=0;
        if(find_class_xp(g_saveBuf,got,guids[c],&total)){
            DWORD lvl=0,prog=0; total_xp_to_level(total,&lvl,&prog);
            g_classLevel[c]=lvl; g_classXpInLevel[c]=prog; g_classTotalXp[c]=total; g_classFound[c]=1;
            unsigned int p=0;
            p=append_w(g_classChipLine2[c],p,24,lvPrefix);
            p=append_uint(g_classChipLine2[c],p,24,lvl);
            p=0;
            p=append_thousands(g_classChipLine3[c],p,48,total);
            p=append_w(g_classChipLine3[c],p,48,totalSuffix);
            p=0;
            p=append_thousands(g_classChipLine4[c],p,48,prog);
            p=append_w(g_classChipLine4[c],p,48,intoLvlSuffix);
        } else {
            g_classFound[c]=0;
            g_classChipLine2[c][0]=0; g_classChipLine3[c][0]=0; g_classChipLine4[c][0]=0;
            if(anyMissing) spos=append_w(status,spos,256,sep);
            spos=append_w(status,spos,256,names[c]);
            spos=append_w(status,spos,256,notFoundLine);
            anyMissing=1;
        }
    }
    static const WCHAR okStatus[]={'S','a','v','e',' ','f','i','l','e',' ','l','o','a','d','e','d',' ','-',' ','a','l','l',' ','c','l','a','s','s','e','s',' ','f','o','u','n','d','.',0};
    SetWindowTextW(g_saveResult, anyMissing?status:okStatus);
    redraw_class_chips();
    InvalidateRect(g_saveResult,NULL,TRUE);
    return 1;
}

static void layout_controls(HWND hwnd);

// Mirrors g_savePath into the read-only path label, so the user always sees the route currently in use.
static void set_savepath_display(void){
    SetWindowTextW(g_savePathLabel,g_savePath);
}

// Switches the path row between the read-only label+pencil view and the editable box+check/cancel view.
static void set_path_edit_mode(int on){
    g_pathEditMode=on;
    if(on){
        SetWindowTextW(g_editSavePath,g_savePath);
    }
    layout_controls(g_main);
    if(on){
        SetFocus(g_editSavePath);
        SendMessageW(g_editSavePath,EM_SETSEL,0,(LPARAM)-1);
    }
}

static void set_edit_uint(HWND edit, DWORD v);
static void calculate(void);

// Only re-run the calculation if the current fields were populated from a class button;
// a manually-typed level/XP must never be overwritten by a save reload.
static void resync_selected_class(void){
    if(g_lastClassSelected>=0 && g_classFound[g_lastClassSelected]){
        int cls=g_lastClassSelected;
        set_edit_uint(g_editLevel,g_classLevel[cls]);
        set_edit_uint(g_editXp,g_classXpInLevel[cls]);
        calculate();
        g_lastClassSelected=cls;
    }
}

// Lets the user pick a .sav file, remembers it, and displays it. Bound to the Browse button.
static void load_and_show_save(void){
    static const WCHAR noPick[]={'N','o',' ','s','a','v','e',' ','f','i','l','e',' ','s','e','l','e','c','t','e','d','.',0};
    if(!pick_save_file()){
        SetWindowTextW(g_saveResult,noPick);
        return;
    }
    if(read_and_display_save()){
        g_haveSavePath=1;
        save_cached_path();
        get_save_write_time(&g_lastWriteTime);
        set_savepath_display();
        resync_selected_class();
    }
}

// Lets the user type/paste a path directly into the box and confirm with the checkmark button.
static void apply_manual_path(void){
    WCHAR buf[1024];
    WCHAR prevPath[1024];
    int n=GetWindowTextW(g_editSavePath,buf,1024);
    if(n<=0) return;
    for(unsigned int i=0;i<1024;i++){
        prevPath[i]=g_savePath[i];
        if(!g_savePath[i]) break;
    }
    unsigned int i=0; for(;i<(unsigned int)n && i<1023;i++) g_savePath[i]=buf[i];
    g_savePath[i]=0;
    if(read_and_display_save()){
        g_haveSavePath=1;
        save_cached_path();
        get_save_write_time(&g_lastWriteTime);
        set_savepath_display();
        set_path_edit_mode(0);
        resync_selected_class();
    } else {
        for(unsigned int j=0;j<1024;j++){
            g_savePath[j]=prevPath[j];
            if(!prevPath[j]) break;
        }
    }
}

// Discards whatever was typed and returns to the read-only label without touching g_savePath.
static void cancel_path_edit(void){
    set_path_edit_mode(0);
}

// Polls the currently loaded save file every ID_TIMER_AUTOSYNC tick; reloads only if its
// last-write-time changed, so a stationary save file costs one attribute lookup per tick.
static void auto_sync_check(void){
    if(!g_haveSavePath) return;
    FILETIME now;
    if(!get_save_write_time(&now)) return;
    if(now.dwLowDateTime==g_lastWriteTime.dwLowDateTime && now.dwHighDateTime==g_lastWriteTime.dwHighDateTime) return;
    if(read_and_display_save()){
        g_lastWriteTime=now;
        resync_selected_class();
    }
}

static void set_edit_uint(HWND edit, DWORD v){
    WCHAR buf[16]; append_uint(buf,0,16,v); SetWindowTextW(edit,buf);
}

// Fills the calculator's level/XP fields from a save-derived class and runs the calculation.
static void redraw_class_chips(void){
    for(int c=0;c<4;c++) InvalidateRect(g_classBtn[c],NULL,TRUE);
}

static void use_class_in_calculator(int classIndex){
    static const WCHAR notLoaded[]={'L','o','a','d',' ','y','o','u','r',' ','s','a','v','e',' ','f','i','r','s','t','.',0};
    if(!g_classFound[classIndex]){ show_error(notLoaded); return; }
    g_lastClassSelected=classIndex;
    set_edit_uint(g_editLevel,g_classLevel[classIndex]);
    set_edit_uint(g_editXp,g_classXpInLevel[classIndex]);
    calculate();
    redraw_class_chips();
}

static void layout_controls(HWND hwnd){
    RECT r; GetClientRect(hwnd,&r); int w=r.right-r.left, h=r.bottom-r.top;
    int margin=20, inner=w-40;
    int pad=16, fieldX=margin+pad, fieldW=inner-2*pad;
    int fieldRadius=6;
    MoveWindow(g_title,margin,18,inner,34,TRUE);

    // Card 1 (calculator): two-column level/XP, Calculate, result list, total row.
    int cardCalcY=66;
    int colGap=12, colW=(fieldW-colGap)/2;
    int levelLabelY=cardCalcY+pad, levelInputY=levelLabelY+22, inputH=34;
    MoveWindow(g_labelLevel,fieldX,levelLabelY,colW,20,TRUE);
    MoveWindow(g_editLevel,fieldX,levelInputY,colW,inputH,TRUE);
    MoveWindow(g_labelXp,fieldX+colW+colGap,levelLabelY,colW,20,TRUE);
    MoveWindow(g_editXp,fieldX+colW+colGap,levelInputY,colW,inputH,TRUE);
    round_control(g_editLevel,colW,inputH,fieldRadius);
    round_control(g_editXp,colW,inputH,fieldRadius);
    int calcBtnY=levelInputY+inputH+14, calcBtnH=42;
    MoveWindow(g_button,fieldX,calcBtnY,fieldW,calcBtnH,TRUE);

    int testH=178, bottom=20;
    int testY=h-bottom-testH;
    int totalH=40;
    int totalY=testY-14-pad-totalH;
    int colHeaderH=26;
    int col0=90, scrollbarReserve=20, col1=fieldW-col0-scrollbarReserve;
    int colHeaderY=calcBtnY+calcBtnH+14;
    MoveWindow(g_resultColLevel,fieldX+4,colHeaderY,col0-4,colHeaderH,TRUE);
    MoveWindow(g_resultColXp,fieldX+col0,colHeaderY,col1-4,colHeaderH,TRUE);
    int resultY=colHeaderY+colHeaderH+2;
    int resultH=totalY-10-resultY; if(resultH<80) resultH=80;
    MoveWindow(g_result,fieldX,resultY,fieldW,resultH,TRUE);
    MoveWindow(g_total,fieldX,totalY,fieldW,totalH,TRUE);
    round_control(g_result,fieldW,resultH,fieldRadius);
    round_control(g_total,fieldW,totalH,fieldRadius);
    {
        SendMessageW(g_result,LVM_SETCOLUMNWIDTH,0,(LPARAM)col0);
        SendMessageW(g_result,LVM_SETCOLUMNWIDTH,1,(LPARAM)col1);
    }
    int cardCalcBottom=totalY+totalH+pad;
    g_cardCalcRect.left=margin; g_cardCalcRect.top=cardCalcY; g_cardCalcRect.right=margin+inner; g_cardCalcRect.bottom=cardCalcBottom;

    // Card 2 (save file): path row, per-class chips (level + XP detail), status line.
    g_cardSaveRect.left=margin; g_cardSaveRect.top=testY; g_cardSaveRect.right=margin+inner; g_cardSaveRect.bottom=testY+testH;
    int rowY=testY+pad, rowH=28, rowGap=8, browseW=80, iconW=28;
    if(!g_pathEditMode){
        int browseX=fieldX+fieldW-browseW;
        int pencilX=browseX-rowGap-iconW;
        int labelW=pencilX-rowGap-fieldX;
        MoveWindow(g_savePathLabel,fieldX,rowY,labelW,rowH,TRUE);
        MoveWindow(g_editPathBtn,pencilX,rowY,iconW,rowH,TRUE);
        MoveWindow(g_loadSaveBtn,browseX,rowY,browseW,rowH,TRUE);
        round_control(g_savePathLabel,labelW,rowH,fieldRadius);
        ShowWindow(g_savePathLabel,SW_SHOW); ShowWindow(g_editPathBtn,SW_SHOW); ShowWindow(g_loadSaveBtn,SW_SHOW);
        ShowWindow(g_editSavePath,SW_HIDE); ShowWindow(g_applyPathBtn,SW_HIDE); ShowWindow(g_cancelPathBtn,SW_HIDE);
    } else {
        int cancelX=fieldX+fieldW-iconW;
        int checkX=cancelX-rowGap-iconW;
        int editW=checkX-rowGap-fieldX;
        MoveWindow(g_editSavePath,fieldX,rowY,editW,rowH,TRUE);
        MoveWindow(g_applyPathBtn,checkX,rowY,iconW,rowH,TRUE);
        MoveWindow(g_cancelPathBtn,cancelX,rowY,iconW,rowH,TRUE);
        round_control(g_editSavePath,editW,rowH,fieldRadius);
        ShowWindow(g_editSavePath,SW_SHOW); ShowWindow(g_applyPathBtn,SW_SHOW); ShowWindow(g_cancelPathBtn,SW_SHOW);
        ShowWindow(g_savePathLabel,SW_HIDE); ShowWindow(g_editPathBtn,SW_HIDE); ShowWindow(g_loadSaveBtn,SW_HIDE);
    }
    int classBtnY=rowY+rowH+10, classBtnH=76, classBtnGap=8;
    int classBtnW=(fieldW-classBtnGap*3)/4;
    for(int c=0;c<4;c++){
        MoveWindow(g_classBtn[c],fieldX+c*(classBtnW+classBtnGap),classBtnY,classBtnW,classBtnH,TRUE);
    }
    int saveResultY=classBtnY+classBtnH+10;
    int saveResultH=22;
    MoveWindow(g_saveResult,fieldX,saveResultY,fieldW,saveResultH,TRUE);
    round_control(g_saveResult,fieldW,saveResultH,fieldRadius);
}

static const WCHAR g_colLevelTxt[]={'L','e','v','e','l',0};
static const WCHAR g_colXpTxt[]={'X','P',' ','n','e','e','d','e','d',0};

static LRESULT wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp){
    static const WCHAR clsStatic[]={'S','T','A','T','I','C',0};
    static const WCHAR clsEdit[]={'E','D','I','T',0};
    static const WCHAR clsButton[]={'B','U','T','T','O','N',0};
    static const WCHAR titleTxt[]={'D','R','G',' ','X','P',' ','C','a','l','c','u','l','a','t','o','r',0};
    static const WCHAR levelTxt[]={'C','u','r','r','e','n','t',' ','l','e','v','e','l',0};
    static const WCHAR xpTxt[]={'C','u','r','r','e','n','t',' ','X','P',' ','a','t',' ','t','h','a','t',' ','l','e','v','e','l',0};
    static const WCHAR calcTxt[]={'C','a','l','c','u','l','a','t','e',0};
    static const WCHAR totalTxt[]={'T','O','T','A','L',' ','T','O',' ','L','E','V','E','L',' ','2','5',':',' ','-',0};
    static const WCHAR loadSaveTxt[]={'B','r','o','w','s','e',0};
    static const WCHAR saveHintTxt[]={'S','h','o','w','s',' ','L','e','v','e','l',' ','+',' ','X','P',' ','p','e','r',' ','c','l','a','s','s',' ','f','r','o','m',' ','y','o','u','r',' ','s','a','v','e','.',' ','C','l','i','c','k',' ','a',' ','c','l','a','s','s',' ','b','u','t','t','o','n',' ','t','o',' ','u','s','e',' ','i','t',' ','a','b','o','v','e','.',0};
    static const WCHAR classEngTxt[]={'E','n','g','i','n','e','e','r',0};
    static const WCHAR classScoTxt[]={'S','c','o','u','t',0};
    static const WCHAR classDriTxt[]={'D','r','i','l','l','e','r',0};
    static const WCHAR classGunTxt[]={'G','u','n','n','e','r',0};
    static const WCHAR checkTxt[]={0x2714,0};
    static const WCHAR pencilTxt[]={0x270E,0};
    static const WCHAR cancelTxt[]={'X',0};

    if(msg==WM_CREATE){
        g_title=CreateWindowExW(0,clsStatic,titleTxt,WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,0,0,0,0,hwnd,(HANDLE)0,NULL,NULL);
        g_labelLevel=CreateWindowExW(0,clsStatic,levelTxt,WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,0,0,0,0,hwnd,(HANDLE)0,NULL,NULL);
        g_editLevel=CreateWindowExW(0,clsEdit,(LPCWSTR)(const WCHAR[]){0},WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_LEFT|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_EDIT_LEVEL,NULL,NULL);
        g_labelXp=CreateWindowExW(0,clsStatic,xpTxt,WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,0,0,0,0,hwnd,(HANDLE)0,NULL,NULL);
        g_editXp=CreateWindowExW(0,clsEdit,(LPCWSTR)(const WCHAR[]){0},WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_BORDER|ES_LEFT|ES_NUMBER|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_EDIT_XP,NULL,NULL);
        g_button=CreateWindowExW(0,clsButton,calcTxt,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_CALC,NULL,NULL);
        g_resultColLevel=CreateWindowExW(0,clsStatic,g_colLevelTxt,WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,0,0,0,0,hwnd,(HANDLE)0,NULL,NULL);
        g_resultColXp=CreateWindowExW(0,clsStatic,g_colXpTxt,WS_CHILD|WS_VISIBLE|SS_RIGHT|SS_CENTERIMAGE,0,0,0,0,hwnd,(HANDLE)0,NULL,NULL);
        static const WCHAR clsListView[]={'S','y','s','L','i','s','t','V','i','e','w','3','2',0};
        g_result=CreateWindowExW(0,clsListView,(LPCWSTR)(const WCHAR[]){0},WS_CHILD|WS_VISIBLE|WS_BORDER|LVS_REPORT|LVS_SINGLESEL|LVS_SHOWSELALWAYS|LVS_NOCOLUMNHEADER,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_RESULT,NULL,NULL);
        static const WCHAR darkThemeName[]={'D','a','r','k','M','o','d','e','_','E','x','p','l','o','r','e','r',0};
        SetWindowTheme(g_result,darkThemeName,NULL);
        SendMessageW(g_result,LVM_SETEXTENDEDLISTVIEWSTYLE,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER,LVS_EX_FULLROWSELECT|LVS_EX_DOUBLEBUFFER);
        SendMessageW(g_result,LVM_SETBKCOLOR,0,(LPARAM)RGBX(13,22,36));
        SendMessageW(g_result,LVM_SETTEXTBKCOLOR,0,(LPARAM)RGBX(13,22,36));
        SendMessageW(g_result,LVM_SETTEXTCOLOR,0,(LPARAM)RGBX(230,236,246));
        {
            LVCOLUMNW col; memset(&col,0,sizeof(col));
            col.mask=LVCF_WIDTH|LVCF_SUBITEM|LVCF_FMT;
            col.fmt=LVCFMT_LEFT; col.cx=90; col.iSubItem=0;
            SendMessageW(g_result,LVM_INSERTCOLUMNW,0,(LPARAM)&col);
            col.fmt=LVCFMT_RIGHT; col.cx=300; col.iSubItem=1;
            SendMessageW(g_result,LVM_INSERTCOLUMNW,1,(LPARAM)&col);
        }
        g_total=CreateWindowExW(0,clsStatic,totalTxt,WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_TOTAL,NULL,NULL);
        g_savePathLabel=CreateWindowExW(0,clsStatic,(LPCWSTR)(const WCHAR[]){0},WS_CHILD|WS_VISIBLE|WS_BORDER|SS_LEFT|SS_CENTERIMAGE|SS_PATHELLIPSIS,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_PATH_LABEL,NULL,NULL);
        g_editSavePath=CreateWindowExW(0,clsEdit,(LPCWSTR)(const WCHAR[]){0},WS_CHILD|WS_TABSTOP|WS_BORDER|ES_LEFT|ES_AUTOHSCROLL,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_EDIT_SAVEPATH,NULL,NULL);
        g_editPathBtn=CreateWindowExW(0,clsButton,pencilTxt,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_EDITPATH,NULL,NULL);
        g_applyPathBtn=CreateWindowExW(0,clsButton,checkTxt,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_APPLYPATH,NULL,NULL);
        g_cancelPathBtn=CreateWindowExW(0,clsButton,cancelTxt,WS_CHILD|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_CANCELPATH,NULL,NULL);
        g_loadSaveBtn=CreateWindowExW(0,clsButton,loadSaveTxt,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_LOADSAVE,NULL,NULL);
        g_classBtn[0]=CreateWindowExW(0,clsButton,classEngTxt,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_CLASS_ENGINEER,NULL,NULL);
        g_classBtn[1]=CreateWindowExW(0,clsButton,classScoTxt,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_CLASS_SCOUT,NULL,NULL);
        g_classBtn[2]=CreateWindowExW(0,clsButton,classDriTxt,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_CLASS_DRILLER,NULL,NULL);
        g_classBtn[3]=CreateWindowExW(0,clsButton,classGunTxt,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_BUTTON_CLASS_GUNNER,NULL,NULL);
        g_saveResult=CreateWindowExW(0,clsStatic,(LPCWSTR)(const WCHAR[]){0},WS_CHILD|WS_VISIBLE|SS_LEFT|SS_CENTERIMAGE,0,0,0,0,hwnd,(HANDLE)(ULONG_PTR)ID_SAVE_RESULT,NULL,NULL);
        set_font(g_title,g_fontTitle); set_font(g_labelLevel,g_font); set_font(g_editLevel,g_font); set_font(g_labelXp,g_font); set_font(g_editXp,g_font); set_font(g_button,g_font); set_font(g_result,g_font); set_font(g_total,g_fontTotal);
        set_font(g_resultColLevel,g_fontColHeader); set_font(g_resultColXp,g_fontColHeader);
        set_font(g_loadSaveBtn,g_font); set_font(g_saveResult,g_font);
        set_font(g_savePathLabel,g_font); set_font(g_editSavePath,g_font); set_font(g_applyPathBtn,g_font);
        set_font(g_editPathBtn,g_font); set_font(g_cancelPathBtn,g_font);
        for(int c=0;c<4;c++) set_font(g_classBtn[c],g_font);
        layout_controls(hwnd);

        build_cache_path();
        if(load_cached_path() && read_and_display_save()){
            g_haveSavePath=1;
            get_save_write_time(&g_lastWriteTime);
        } else if(try_default_save_path() && read_and_display_save()){
            g_haveSavePath=1;
            get_save_write_time(&g_lastWriteTime);
            save_cached_path();
        } else {
            static const WCHAR noSaveTxt[]={'N','o',' ','s','a','v','e',' ','f','i','l','e',' ','l','o','a','d','e','d',' ','y','e','t','.',' ','C','l','i','c','k',' ','B','r','o','w','s','e','.',0};
            SetWindowTextW(g_saveResult,noSaveTxt);
        }
        set_savepath_display();
        SetTimer(hwnd,ID_TIMER_AUTOSYNC,10000,NULL);
        return 0;
    }
    if(msg==WM_SIZE){ layout_controls(hwnd); InvalidateRect(hwnd,NULL,TRUE); return 0; }
    if(msg==WM_GETMINMAXINFO){ MINMAXINFO* m=(MINMAXINFO*)lp; m->ptMinTrackSize.x=560; m->ptMinTrackSize.y=760; return 0; }
    if(msg==WM_TIMER){
        if(wp==ID_TIMER_AUTOSYNC) auto_sync_check();
        return 0;
    }
    if(msg==WM_COMMAND){
        UINT ctlId=(UINT)wp & 0xFFFF;
        if(ctlId==ID_BUTTON_CALC){ g_lastClassSelected=-1; calculate(); redraw_class_chips(); return 0; }
        if(ctlId==ID_BUTTON_LOADSAVE){ load_and_show_save(); return 0; }
        if(ctlId==ID_BUTTON_APPLYPATH){ apply_manual_path(); return 0; }
        if(ctlId==ID_BUTTON_EDITPATH){ set_path_edit_mode(1); return 0; }
        if(ctlId==ID_BUTTON_CANCELPATH){ cancel_path_edit(); return 0; }
        if(ctlId==ID_BUTTON_CLASS_ENGINEER){ use_class_in_calculator(0); return 0; }
        if(ctlId==ID_BUTTON_CLASS_SCOUT){ use_class_in_calculator(1); return 0; }
        if(ctlId==ID_BUTTON_CLASS_DRILLER){ use_class_in_calculator(2); return 0; }
        if(ctlId==ID_BUTTON_CLASS_GUNNER){ use_class_in_calculator(3); return 0; }
    }
    if(msg==WM_DRAWITEM){
        DRAWITEMSTRUCT* d=(DRAWITEMSTRUCT*)lp;
        const WCHAR* label=NULL;
        switch(d->CtlID){
            case ID_BUTTON_CALC: label=calcTxt; break;
            case ID_BUTTON_LOADSAVE: label=loadSaveTxt; break;
            case ID_BUTTON_APPLYPATH: label=checkTxt; break;
            case ID_BUTTON_EDITPATH: label=pencilTxt; break;
            case ID_BUTTON_CANCELPATH: label=cancelTxt; break;
        }
        if(label){
            HBRUSH bg=(d->itemState & ODS_SELECTED)?g_brushAccentPressed:g_brushAccent;
            FillRect(d->hDC,&d->rcItem,g_brushInput);
            SelectObject(d->hDC,(HGDIOBJ)GetStockObject(NULL_PEN));
            SelectObject(d->hDC,(HGDIOBJ)bg);
            RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,8,8);
            SetBkMode(d->hDC,TRANSPARENT); SetTextColor(d->hDC,RGBX(242,247,255));
            RECT rr=d->rcItem; DrawTextW(d->hDC,label,-1,&rr,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            return TRUE;
        }
        int classIdx=-1;
        switch(d->CtlID){
            case ID_BUTTON_CLASS_ENGINEER: classIdx=0; break;
            case ID_BUTTON_CLASS_SCOUT: classIdx=1; break;
            case ID_BUTTON_CLASS_DRILLER: classIdx=2; break;
            case ID_BUTTON_CLASS_GUNNER: classIdx=3; break;
        }
        if(classIdx>=0){
            const WCHAR* names[4]={classEngTxt,classScoTxt,classDriTxt,classGunTxt};
            int selected=(g_lastClassSelected==classIdx);
            HBRUSH border=selected?g_brushChipSelected:g_brushChipBorder;
            FillRect(d->hDC,&d->rcItem,g_brushInput);
            SelectObject(d->hDC,(HGDIOBJ)GetStockObject(NULL_PEN));
            SelectObject(d->hDC,(HGDIOBJ)border);
            RoundRect(d->hDC,d->rcItem.left,d->rcItem.top,d->rcItem.right,d->rcItem.bottom,8,8);
            SelectObject(d->hDC,(HGDIOBJ)g_brushField);
            RoundRect(d->hDC,d->rcItem.left+1,d->rcItem.top+1,d->rcItem.right-1,d->rcItem.bottom-1,7,7);
            SetBkMode(d->hDC,TRANSPARENT);
            RECT nameRect=d->rcItem; nameRect.top+=8; nameRect.bottom=nameRect.top+18;
            SetTextColor(d->hDC,selected?RGBX(207,226,251):RGBX(199,211,226));
            DrawTextW(d->hDC,names[classIdx],-1,&nameRect,DT_CENTER|DT_SINGLELINE|DT_NOCLIP);
            HFONT oldFont=(HFONT)SelectObject(d->hDC,(HGDIOBJ)g_fontSmall);
            RECT lvlRect=d->rcItem; lvlRect.top=nameRect.bottom+2; lvlRect.bottom=lvlRect.top+15;
            SetTextColor(d->hDC,RGBX(159,196,247));
            DrawTextW(d->hDC,g_classChipLine2[classIdx],-1,&lvlRect,DT_CENTER|DT_SINGLELINE|DT_NOCLIP);
            RECT totRect=lvlRect; totRect.top=lvlRect.bottom+2; totRect.bottom=totRect.top+14;
            SetTextColor(d->hDC,RGBX(111,155,209));
            DrawTextW(d->hDC,g_classChipLine3[classIdx],-1,&totRect,DT_CENTER|DT_SINGLELINE|DT_NOCLIP);
            RECT progRect=totRect; progRect.top=totRect.bottom; progRect.bottom=progRect.top+14;
            DrawTextW(d->hDC,g_classChipLine4[classIdx],-1,&progRect,DT_CENTER|DT_SINGLELINE|DT_NOCLIP);
            SelectObject(d->hDC,(HGDIOBJ)oldFont);
            return TRUE;
        }
    }
    if(msg==WM_CTLCOLORSTATIC){
        HDC dc=(HDC)wp; HWND child=(HWND)lp;
        SetTextColor(dc,RGBX(230,236,246)); SetBkMode(dc,TRANSPARENT);
        if(child==g_total){ SetBkColor(dc,RGBX(15,44,80)); return (LRESULT)g_brushAccentPressed; }
        if(child==g_saveResult){ SetBkColor(dc,RGBX(13,22,36)); return (LRESULT)g_brushInput; }
        if(child==g_savePathLabel){ SetBkColor(dc,RGBX(10,18,30)); return (LRESULT)g_brushField; }
        if(child==g_labelLevel || child==g_labelXp){ SetBkColor(dc,RGBX(13,22,36)); return (LRESULT)g_brushInput; }
        if(child==g_resultColLevel || child==g_resultColXp){ SetTextColor(dc,RGBX(159,196,247)); SetBkColor(dc,RGBX(13,22,36)); return (LRESULT)g_brushInput; }
        return (LRESULT)g_brushMain;
    }
    if(msg==WM_CTLCOLOREDIT){
        HDC dc=(HDC)wp; SetTextColor(dc,RGBX(238,243,250)); SetBkColor(dc,RGBX(10,18,30)); return (LRESULT)g_brushField;
    }
    if(msg==WM_PAINT){
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);
        FillRect(dc,&ps.rcPaint,g_brushMain);
        draw_round_panel(dc,g_cardCalcRect,10,g_brushCardBorder,g_brushInput,1);
        draw_round_panel(dc,g_cardSaveRect,10,g_brushCardBorder,g_brushInput,1);
        EndPaint(hwnd,&ps);
        return 0;
    }
    if(msg==WM_DESTROY){ KillTimer(hwnd,ID_TIMER_AUTOSYNC); PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,wp,lp);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PWSTR pCmdLine, int nCmdShow){
    (void)hPrevInstance; (void)pCmdLine; (void)nCmdShow;
    g_instance=hInstance;
    init_optional_apis();

    if(pSetProcessDpiAwarenessContext) pSetProcessDpiAwarenessContext((HANDLE)(LONG_PTR)-4);

    g_brushMain=CreateSolidBrush(RGBX(7,11,18));
    g_brushInput=CreateSolidBrush(RGBX(13,22,36));
    g_brushAccent=CreateSolidBrush(RGBX(22,67,125));
    g_brushAccentPressed=CreateSolidBrush(RGBX(15,44,80));
    g_brushField=CreateSolidBrush(RGBX(10,18,30));
    g_brushCardBorder=CreateSolidBrush(RGBX(28,42,61));
    g_brushChipBorder=CreateSolidBrush(RGBX(34,51,73));
    g_brushChipSelected=CreateSolidBrush(RGBX(58,134,224));

    static const WCHAR segoe[]={'S','e','g','o','e',' ','U','I',0};
    g_font=CreateFontW(-18,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,segoe);
    g_fontTitle=CreateFontW(-26,0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,segoe);
    g_fontTotal=CreateFontW(-19,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,segoe);
    g_fontSmall=CreateFontW(-12,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,segoe);
    g_fontColHeader=CreateFontW(-18,0,0,0,FW_BOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,segoe);
    if(!g_font) g_font=(HFONT)GetStockObject(DEFAULT_GUI_FONT);
    if(!g_fontTitle) g_fontTitle=g_font;
    if(!g_fontTotal) g_fontTotal=g_font;
    if(!g_fontSmall) g_fontSmall=g_font;
    if(!g_fontColHeader) g_fontColHeader=g_font;

    static const WCHAR className[]={'D','R','G','X','P','C','a','l','c','W','i','n','d','o','w',0};
    static const WCHAR windowTitle[]={'D','R','G',' ','X','P',' ','C','a','l','c','u','l','a','t','o','r',0};
    WNDCLASSEXW wc; memset(&wc,0,sizeof(wc));
    wc.cbSize=sizeof(wc); wc.lpfnWndProc=wndproc; wc.hInstance=g_instance; wc.hCursor=LoadCursorW(NULL,IDC_ARROW); wc.hIcon=LoadIconW(g_instance,IDI_APP_ICON); wc.hIconSm=wc.hIcon; wc.hbrBackground=g_brushMain; wc.lpszClassName=className;
    if(!RegisterClassExW(&wc)) return 1;

    g_main=CreateWindowExW(WS_EX_CONTROLPARENT,className,windowTitle,WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,700,900,NULL,NULL,g_instance,NULL);
    if(!g_main) return 2;

    { BOOL yes=TRUE; if(DwmSetWindowAttribute(g_main,20,&yes,sizeof(yes))<0) DwmSetWindowAttribute(g_main,19,&yes,sizeof(yes)); }
    ShowWindow(g_main,SW_SHOW); UpdateWindow(g_main); SetFocus(g_editLevel);

    MSG m;
    while(GetMessageW(&m,NULL,0,0)>0){
        if(m.message==WM_KEYDOWN && m.wParam==VK_RETURN){ g_lastClassSelected=-1; calculate(); redraw_class_chips(); continue; }
        TranslateMessage(&m); DispatchMessageW(&m);
    }

    if(g_font && g_font!=(HFONT)GetStockObject(DEFAULT_GUI_FONT)) DeleteObject(g_font);
    if(g_fontTitle && g_fontTitle!=g_font) DeleteObject(g_fontTitle);
    if(g_fontTotal && g_fontTotal!=g_font) DeleteObject(g_fontTotal);
    if(g_fontSmall && g_fontSmall!=g_font) DeleteObject(g_fontSmall);
    if(g_brushMain) DeleteObject(g_brushMain); if(g_brushInput) DeleteObject(g_brushInput); if(g_brushAccent) DeleteObject(g_brushAccent); if(g_brushAccentPressed) DeleteObject(g_brushAccentPressed);
    if(g_brushField) DeleteObject(g_brushField); if(g_brushCardBorder) DeleteObject(g_brushCardBorder);
    if(g_brushChipBorder) DeleteObject(g_brushChipBorder); if(g_brushChipSelected) DeleteObject(g_brushChipSelected);
    return (int)m.wParam;
}
