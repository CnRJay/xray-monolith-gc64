@echo off
setlocal EnableExtensions EnableDelayedExpansion
pushd "%~dp0"

REM Determine DynASM path and platform flags
echo [LuaJIT] Determine DynASM path and if platform is x64
set "DYNASM=%~dp0..\dynasm\dynasm.lua"
if /I "%~1"=="x64" (
  echo [LuaJIT] Platform is x64, set -D P64 ^(GC64^)
  set "DFLAGS=-D WIN -D JIT -D FFI -D P64"
  set "DASC=vm_x64.dasc"
  set "BUILDVM_DEFS=/DLUAJIT_ENABLE_GC64"
) else (
  echo [LuaJIT] Platform is not x64
  set "DFLAGS=-D WIN -D JIT -D FFI"
  set "DASC=vm_x86.dasc"
  set "BUILDVM_DEFS="
)

REM Build minilua if missing
if not exist "host\minilua.exe" (
  echo [LuaJIT] Building host\minilua.exe
  cl /nologo /c /O2 /Ob3 /Oi /Ot /Oy /GT /W3 /fp:precise /MT /GF /GS- /Zi /D_CRT_SECURE_NO_DEPRECATE host\minilua.c || exit /b 1
  link /nologo /OPT:REF /OPT:ICF /out:host\minilua.exe minilua.obj || exit /b 1
  del /q minilua.obj 2>nul
)

REM Generate buildvm_arch.h with DynASM
echo [LuaJIT] Running DynASM to generate host\buildvm_arch.h
"host\minilua.exe" "%DYNASM%" -LN %DFLAGS% -o "host\buildvm_arch.h" "%DASC%" || exit /b 1

REM Build buildvm if missing
if not exist "host\buildvm.exe" (
  echo [LuaJIT] Building host\buildvm.exe
  cl /nologo /c /O2 /Ob3 /Oi /Ot /Oy /GT /W3 /fp:precise /MT /GF /GS- /Zi %BUILDVM_DEFS% /I "." /I "..\dynasm" host\buildvm.c host\buildvm_peobj.c host\buildvm_lib.c host\buildvm_asm.c host\buildvm_fold.c || exit /b 1
  link /nologo /OPT:REF /OPT:ICF /out:host\buildvm.exe buildvm.obj buildvm_peobj.obj buildvm_lib.obj buildvm_asm.obj buildvm_fold.obj || exit /b 1
  del /q buildvm.obj buildvm_peobj.obj buildvm_lib.obj buildvm_asm.obj buildvm_fold.obj 2>nul
)

REM Generate VM object and header files
echo [LuaJIT] Generating lj_vm.obj and headers
host\buildvm.exe -m peobj -o lj_vm.obj || exit /b 1
host\buildvm.exe -m bcdef -o lj_bcdef.h lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c || exit /b 1
host\buildvm.exe -m ffdef -o lj_ffdef.h lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c || exit /b 1
host\buildvm.exe -m libdef -o lj_libdef.h lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c || exit /b 1
host\buildvm.exe -m recdef -o lj_recdef.h lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c || exit /b 1
host\buildvm.exe -m vmdef -o jit\vmdef.lua lib_base.c lib_math.c lib_bit.c lib_string.c lib_table.c lib_io.c lib_os.c lib_package.c lib_debug.c lib_jit.c lib_ffi.c || exit /b 1
host\buildvm.exe -m folddef -o lj_folddef.h lj_opt_fold.c || exit /b 1

echo [LuaJIT] Generation complete
popd
exit /b 0
