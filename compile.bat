@echo off

set "buildfolder=C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin"
set "execpath=%buildfolder%\MSBuild.exe"

echo %execpath%
"%execpath%" -nologo -m ./build/MP-InteractiveMap.sln /p:Configuration=Debug
