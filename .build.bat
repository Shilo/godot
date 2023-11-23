@REM Navigate to the script's directory
cd %~dp0

@REM Delete old build files
del /q bin\*.*

@REM Build Godot for .NET
scons

@REM Open the build
.\bin\godot.windows.editor.x86_64.console.exe

pause