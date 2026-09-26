// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "../common/caretgeometry.h"
#include <objbase.h>
#include <oleauto.h>
#include <uiautomation.h>
#include <QJsonDocument>
#include <cstdio>

// Runs in a disposable process: an unresponsive UIA provider cannot stall the
// stream, the application's UI thread, or display restoration.
inline int runTextCaretProbe(const QStringList& args) {
    // UIA can block in a provider RPC. This watchdog also bounds the probe if
    // its parent crashes before it can enforce the normal process deadline.
    HANDLE watchdog=CreateThread(nullptr,0,[](LPVOID) -> DWORD {Sleep(350); ExitProcess(1); return 0;},nullptr,0,nullptr);
    if(!watchdog) return 1;
    CloseHandle(watchdog);
    QJsonObject caret{{"valid",false}};
    DEVMODEW mode{}; mode.dmSize=sizeof(mode);
    if(args.size()!=3 || !EnumDisplaySettingsExW(reinterpret_cast<LPCWSTR>(args[2].utf16()),ENUM_CURRENT_SETTINGS,&mode,0)) return 1;
    const QRectF screen(mode.dmPosition.x,mode.dmPosition.y,mode.dmPelsWidth,mode.dmPelsHeight);
    const HWND foreground=GetForegroundWindow();
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    if(SUCCEEDED(initialized)) {
        IUIAutomation* automation=nullptr; IUIAutomationElement* element=nullptr;
        IUIAutomationTextPattern2* pattern=nullptr; IUIAutomationTextRange* range=nullptr;
        if(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&automation))) &&
           SUCCEEDED(automation->GetFocusedElement(&element)) && element) {
            BOOL password=TRUE, focused=FALSE;
            if(SUCCEEDED(element->get_CurrentIsPassword(&password)) && !password &&
               SUCCEEDED(element->get_CurrentHasKeyboardFocus(&focused)) && focused) {
                if(SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPattern2Id,IID_PPV_ARGS(&pattern))) && pattern) {
                    BOOL active=FALSE;
                    if(FAILED(pattern->GetCaretRange(&active,&range)) || !active) {
                        if(range) range->Release(); range=nullptr;
                    }
                }
                if(!range) {
                    IUIAutomationTextPattern* legacy=nullptr; IUIAutomationTextRangeArray* selection=nullptr;
                    if(SUCCEEDED(element->GetCurrentPatternAs(UIA_TextPatternId,IID_PPV_ARGS(&legacy))) && legacy &&
                       SUCCEEDED(legacy->GetSelection(&selection)) && selection) {
                        int count=0;
                        if(SUCCEEDED(selection->get_Length(&count)) && count==1 && SUCCEEDED(selection->GetElement(0,&range)) && range) {
                            int comparison=1;
                            if(FAILED(range->CompareEndpoints(TextPatternRangeEndpoint_Start,range,TextPatternRangeEndpoint_End,&comparison)) || comparison!=0) {
                                range->Release(); range=nullptr;
                            }
                        }
                    }
                    if(selection) selection->Release(); if(legacy) legacy->Release();
                }
                if(range) {
                    const auto rectangle=[&](IUIAutomationTextRange* value) {
                        SAFEARRAY* bounds=nullptr; QJsonObject result{{"valid",false}};
                        if(SUCCEEDED(value->GetBoundingRectangles(&bounds)) && bounds) {
                            LONG first=0,last=-1; double* values=nullptr;
                            if(SafeArrayGetDim(bounds)==1 && SUCCEEDED(SafeArrayGetLBound(bounds,1,&first)) &&
                               SUCCEEDED(SafeArrayGetUBound(bounds,1,&last)) && last-first+1==4 &&
                               SUCCEEDED(SafeArrayAccessData(bounds,reinterpret_cast<void**>(&values)))) {
                                result=caretGeometry(QRectF(values[0],values[1],values[2],values[3]),screen);
                                SafeArrayUnaccessData(bounds);
                            }
                            SafeArrayDestroy(bounds);
                        }
                        return result;
                    };
                    caret=rectangle(range);
                    // Some providers omit rectangles for empty ranges. The
                    // enclosing character gives the nearby insertion-line area
                    // without fetching any text or changing the selection.
                    if(!caret["valid"].toBool()) {
                        IUIAutomationTextRange* character=nullptr;
                        if(SUCCEEDED(range->Clone(&character)) && character) {
                            if(SUCCEEDED(character->ExpandToEnclosingUnit(TextUnit_Character))) caret=rectangle(character);
                            character->Release();
                        }
                    }
                    focused=FALSE;
                    if(FAILED(element->get_CurrentHasKeyboardFocus(&focused)) || !focused) caret={{"valid",false}};
                }
            }
        }
        if(range) range->Release(); if(pattern) pattern->Release();
        if(element) element->Release(); if(automation) automation->Release();
        CoUninitialize();
    }
    // Win32 controls often expose a native caret even without TextPattern2.
    if(!caret["valid"].toBool()) {
        GUITHREADINFO info{sizeof(info)};
        if(GetGUIThreadInfo(0,&info) && info.hwndCaret && info.rcCaret.bottom>info.rcCaret.top) {
            POINT points[2]={{info.rcCaret.left,info.rcCaret.top},{info.rcCaret.right,info.rcCaret.bottom}};
            if(ClientToScreen(info.hwndCaret,&points[0]) && ClientToScreen(info.hwndCaret,&points[1]))
                caret=caretGeometry(QRectF(points[0].x,points[0].y,points[1].x-points[0].x,points[1].y-points[0].y),screen);
        }
    }
    if(!foreground || foreground!=GetForegroundWindow()) caret={{"valid",false}};
    const auto bytes=QJsonDocument(QJsonObject{{"caret",caret}}).toJson(QJsonDocument::Compact);
    fwrite(bytes.constData(),1,size_t(bytes.size()),stdout);
    return 0;
}
