; SPDX-License-Identifier: GPL-3.0-or-later
; NSIS 3 installer for Lanczos Scale GIMP 3 plug-in

!include "MUI2.nsh"
!include "LogicLib.nsh"

; General
Name "Lanczos Scale GIMP Plug-in"
OutFile "lanczos-scale-windows-x86_64-installer.exe"
InstallDir "$APPDATA\GIMP\3.2\plug-ins\lanczos-scale"
RequestExecutionLevel user

!ifndef BUILD_DIR
  !define BUILD_DIR "build-release"
!endif

!ifndef PRODUCT_VERSION
  !define PRODUCT_VERSION "1.4.1"
!endif

!ifndef PRODUCT_VERSION_QUAD
  !define PRODUCT_VERSION_QUAD "1.4.1.0"
!endif

; Version info
VIProductVersion "${PRODUCT_VERSION_QUAD}"
VIAddVersionKey "ProductName" "Lanczos Scale GIMP Plug-in"
VIAddVersionKey "FileDescription" "Lanczos Scale GIMP 3 Plug-in Installer"
VIAddVersionKey "LegalCopyright" "GPL-3.0-or-later"
VIAddVersionKey "FileVersion" "${PRODUCT_VERSION}"

; Interface settings
!define MUI_ABORTWARNING
!define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\modern-install.ico"
!define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\modern-uninstall.ico"

; Pages
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE "..\LICENSES\GPL-3.0-or-later"
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

Function KeepBestGimpVersion
    Push $1   ; candidate minor
    Push $2   ; current best minor

    ${If} $R0 != ""
        ${If} $0 == ""
            StrCpy $0 $R0
        ${Else}
            StrCpy $1 $R0 "" 2
            StrCpy $2 $0 "" 2
            ${If} $1 > $2
                StrCpy $0 $R0
            ${EndIf}
        ${EndIf}
    ${EndIf}

    Pop $2
    Pop $1
FunctionEnd

; Search existing per-user GIMP profile directories. This is the most direct
; signal because plug-ins install under the profile, not the application path.
; Output: $R0 = best version string found (empty if none)
Function SearchUserGimpProfiles
    Push $1   ; search handle
    Push $2   ; directory name
    Push $3   ; candidate minor
    Push $4   ; current best minor

    StrCpy $R0 ""

    FindFirst $1 $2 "$APPDATA\GIMP\3.*"

  loop_profiles:
    StrCmp $2 "" done_profiles

    ${If} ${FileExists} "$APPDATA\GIMP\$2\*.*"
        ${If} $R0 == ""
            StrCpy $R0 $2
        ${Else}
            StrCpy $3 $2 "" 2
            StrCpy $4 $R0 "" 2
            ${If} $3 > $4
                StrCpy $R0 $2
            ${EndIf}
        ${EndIf}
    ${EndIf}

    FindNext $1 $2
    Goto loop_profiles

  done_profiles:
    ${If} $1 != ""
        FindClose $1
    ${EndIf}

    Pop $4
    Pop $3
    Pop $2
    Pop $1
FunctionEnd

; Search a specific registry hive for the latest GIMP 3.x profile version.
; Output: $R0 = best version string found (empty if none)
!macro SearchHiveForGimp3 HIVE
    Push $1   ; enum index
    Push $2   ; current subkey
    Push $3   ; current best version
    Push $4   ; minor version

    StrCpy $3 ""
    StrCpy $1 0

  loop_${HIVE}:
    EnumRegKey $2 ${HIVE} "Software\GIMP" $1
    StrCmp $2 "" done_${HIVE}
    IntOp $1 $1 + 1

    ; Must start with "3."
    StrCpy $R0 $2 2
    StrCmp $R0 "3." is_3x_${HIVE}
    Goto loop_${HIVE}

  is_3x_${HIVE}:
    StrCmp $3 "" update_best_${HIVE}
    StrCpy $4 $2 "" 2
    StrCpy $R0 $3 "" 2
    ${If} $4 > $R0
        Goto update_best_${HIVE}
    ${EndIf}
    Goto loop_${HIVE}

  update_best_${HIVE}:
    StrCpy $3 $2
    Goto loop_${HIVE}

  done_${HIVE}:
    StrCpy $R0 $3
    Pop $4
    Pop $3
    Pop $2
    Pop $1
!macroend

; The official Windows installer registers GIMP as an Inno Setup application.
; Use VersionMajor/VersionMinor to derive the matching per-user profile path.
; Output: $R0 = profile version string such as "3.2" (empty if none)
!macro SearchUninstallForGimp3 HIVE
    Push $1   ; major
    Push $2   ; minor

    StrCpy $R0 ""

    ReadRegDWORD $1 ${HIVE} "Software\Microsoft\Windows\CurrentVersion\Uninstall\GIMP-3_is1" "VersionMajor"
    ReadRegDWORD $2 ${HIVE} "Software\Microsoft\Windows\CurrentVersion\Uninstall\GIMP-3_is1" "VersionMinor"

    ${If} $1 == 3
    ${AndIf} $2 != ""
        StrCpy $R0 "3.$2"
    ${EndIf}

    Pop $2
    Pop $1
!macroend

Function .onInit
    ; GIMP is 64-bit; ensure we see the 64-bit registry view
    SetRegView 64

    StrCpy $0 ""

    Call SearchUserGimpProfiles
    Call KeepBestGimpVersion

    !insertmacro SearchHiveForGimp3 HKCU
    Call KeepBestGimpVersion

    !insertmacro SearchHiveForGimp3 HKLM
    Call KeepBestGimpVersion

    !insertmacro SearchUninstallForGimp3 HKCU
    Call KeepBestGimpVersion

    !insertmacro SearchUninstallForGimp3 HKLM
    Call KeepBestGimpVersion

    ${If} $0 != ""
        StrCpy $INSTDIR "$APPDATA\GIMP\$0\plug-ins\lanczos-scale"
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
    File "/oname=LICENSE" "..\LICENSES\GPL-3.0-or-later"
SectionEnd

; Uninstaller
Section "Uninstall"
    Delete "$INSTDIR\lanczos-scale.exe"
    Delete "$INSTDIR\README.md"
    Delete "$INSTDIR\LICENSE"
    Delete "$INSTDIR\GPL-3.0-or-later"
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
