@REM Navigate to the script's directory
cd %~dp0

@REM Delete old build files
del /q bin\*.*

@REM Build Godot for .NET
scons module_mono_enabled=yes

@REM Glue the C# code to Godot
.\bin\godot.windows.editor.x86_64.mono.exe --headless --generate-mono-glue modules/mono/glue
.\modules\mono\build_scripts\build_assemblies.py --godot-output-dir=./bin

@REM Open the build
.\bin\godot.windows.editor.x86_64.mono.console.exe

pause