; SPDX-License-Identifier: GPL-3.0-or-later
; NSIS 3 installer for Lanczos Scale GIMP 3 plug-in

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "FileFunc.nsh"
!include "nsDialogs.nsh"

; General
Name "Lanczos Scale GIMP Plug-in"
OutFile "lanczos-scale-windows-x86_64-installer.exe"
InstallDir "$APPDATA\GIMP\3.2\plug-ins\lanczos-scale"
RequestExecutionLevel user

!ifndef BUILD_DIR
  !define BUILD_DIR "build-release"
!endif

; Version info
VIProductVersion "1.0.0.0"
VIAddVersionKey "ProductName" "Lanczos Scale GIMP Plug-in"
VIAddVersionKey "FileDescription" "Lanczos Scale GIMP 3 Plug-in Installer"
VIAddVersionKey "LegalCopyright" "GPL-3.0-or-later"
VIAddVersionKey "FileVersion" "1.0.0"

; Interface settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSE"
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_WELCOME
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

; Languages
!insertmacro MUI_LANGUAGE "English"

Function .onInit
    ; Try to detect GIMP 3 installation
    ReadRegStr $0 HKCU "Software\GIMP\3.2" "InstallPath"
    ${If} $0 == ""
        ReadRegStr $0 HKLM "Software\GIMP\3.2" "InstallPath"
    ${EndIf}
    ${If} $0 == ""
        ; Check common locations
        ${If} ${FileExists} "$LOCALAPPDATA\Programs\GIMP 3\bin\gimp-3.2.exe"
            StrCpy $0 "$LOCALAPPDATA\Programs\GIMP 3"
        ${ElseIf} ${FileExists} "$PROGRAMFILES64\GIMP 3\bin\gimp-3.2.exe"
            StrCpy $0 "$PROGRAMFILES64\GIMP 3"
        ${ElseIf} ${FileExists} "$PROGRAMFILES32\GIMP 3\bin\gimp-3.2.exe"
            StrCpy $0 "$PROGRAMFILES32\GIMP 3"
        ${EndIf}
    ${EndIf}
    
    ${If} $0 != ""
        StrCpy $INSTDIR "$APPDATA\GIMP\3.2\plug-ins\lanczos-scale"
    ${EndIf}
FunctionEnd

Section "Plug-in" SecPlugin
    SectionIn RO
    
    SetOutPath "$INSTDIR"
    File "..\${BUILD_DIR}\lanczos-scale.exe"
    
    ; Store install folder
    WriteRegStr HKCU "Software\LanczosScale" "" $INSTDIR
    
    ; Create uninstaller
    WriteUninstaller "$INSTDIR\uninstall.exe"
    
    ; Add to Add/Remove Programs
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "DisplayName" "Lanczos Scale GIMP Plug-in"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "UninstallString" "$\"$INSTDIR\uninstall.exe$\""
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "InstallLocation" "$\"$INSTDIR$\""
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "DisplayIcon" "$\"$INSTDIR\lanczos-scale.exe$\""
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "Publisher" "Lanczos Scale Project"
    WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "URLInfoAbout" "https://github.com/anomalyco/lanczos"
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "NoModify" 1
    WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale" \
        "NoRepair" 1
SectionEnd

Section "Documentation" SecDocs
    SetOutPath "$INSTDIR"
    File "..\README.md"
    File "..\LICENSE"
SectionEnd

; Uninstaller
Section "Uninstall"
    Delete "$INSTDIR\lanczos-scale.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\uninstall.exe"
    
    RMDir "$INSTDIR"
    
    DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\LanczosScale"
    DeleteRegKey HKCU "Software\LanczosScale"
SectionEnd

; Descriptions
!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecPlugin} "The Lanczos Scale GIMP 3 plug-in executable."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDocs} "README and license files."
!insertmacro MUI_FUNCTION_DESCRIPTION_END
