@echo off
rem Creates the versioned portable zip (run build.cmd first)
setlocal
for /f %%V in ('powershell -NoProfile -Command "$c=Get-Content CMakeLists.txt -Raw; if($c -match 'project\(DiskPro VERSION ([0-9.]+)'){ $Matches[1] } else { exit 1 }"') do set V=%%V
if not defined V exit /b 1
set D=dist\DiskPro-%V%-win64
rmdir /s /q %D% 2>nul
del /q dist\DiskPro-%V%-win64-portable.zip 2>nul
mkdir %D% || exit /b 1
for %%F in (gui cli inventory tools restore clone part vhd) do copy build\diskpro-%%F.exe %D% >nul || exit /b 1
copy .tools\slint\lib\slint_cpp.dll %D% >nul || exit /b 1
copy LICENSE %D% >nul
copy THIRD_PARTY_NOTICES.md %D% >nul
copy README.md %D% >nul
copy ROADMAP.md %D% >nul
xcopy docs %D%\docs\ /E /I /Q >nul || exit /b 1
copy .tools\slint\licenses\LICENSE.md %D%\SLINT-LICENSE.md >nul
powershell -NoProfile -Command "Compress-Archive -Path '%D%' -DestinationPath 'dist\DiskPro-%V%-win64-portable.zip' -Force" || exit /b 1
powershell -NoProfile -Command "(Get-FileHash 'dist\DiskPro-%V%-win64-portable.zip').Hash.ToLower() + '  DiskPro-%V%-win64-portable.zip' | Set-Content dist\SHA256SUMS.txt"
echo done
