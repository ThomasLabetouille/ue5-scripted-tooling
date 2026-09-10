@echo off
REM ===========================================================================
REM  Proprietes geometriques des panneaux de blockout, HORS MOTEUR.
REM
REM  Compile LE FICHIER DU PROJET (Source/BlockoutTools/Private/
REM  BlockoutPanelGeometry.cpp) contre des doublures d'Unreal, et l'exerce sur
REM  des dizaines de milliers de contours en une seconde -- sans ouvrir
REM  l'editeur, sans rebuild du plugin.
REM
REM  Equivalent de Tools\GeometryTests\run.bat du projet Unity LevelDesignTools.
REM
REM  Usage :  run.bat [facteur]     (facteur 1 par defaut, 20 = ~40 000 contours)
REM  Rend 0 si tout passe, 1 sinon -- utilisable dans un hook de commit.
REM ===========================================================================
setlocal
set HERE=%~dp0
set FACTOR=%1
if "%FACTOR%"=="" set FACTOR=20

for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if not defined VSPATH (
    echo [ERREUR] Visual Studio avec les outils C++ introuvable.
    exit /b 2
)

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo [ERREUR] vcvars64.bat a echoue. & exit /b 2 )

if not exist "%HERE%obj" mkdir "%HERE%obj"

cl /nologo /std:c++20 /O2 /EHsc /W3 ^
   /I "%HERE%shims" ^
   /I "%HERE%..\..\Source\BlockoutTools\Public" ^
   "%HERE%Tests.cpp" ^
   "%HERE%shims\Shims.cpp" ^
   "%HERE%..\..\Source\BlockoutTools\Private\BlockoutPanelGeometry.cpp" ^
   /Fe:"%HERE%GeometryTests.exe" /Fo:"%HERE%obj\\"
if errorlevel 1 (
    echo [ERREUR] Compilation echouee.
    exit /b 2
)

"%HERE%GeometryTests.exe" %FACTOR%
exit /b %ERRORLEVEL%
