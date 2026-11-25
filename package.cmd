@echo off
rem Creates dist\DiskPro-0.2.0-win64-portable.zip (run build.cmd first)
setlocal
set D=dist\DiskPro-0.2.0-win64
rmdir /s /q dist 2>nul
mkdir %D% || exit /b 1
for %%F in (gui cli inventory tools restore) do copy build\diskpro-%%F.exe %D% >nul || exit /b 1
copy .tools\slint\lib\slint_cpp.dll %D% >nul || exit /b 1
copy LICENSE %D% >nul
copy THIRD_PARTY_NOTICES.md %D% >nul
copy README.md %D% >nul
copy .tools\slint\licenses\LICENSE.md %D%\SLINT-LICENSE.md >nul
powershell -NoProfile -Command "Compress-Archive -Path '%D%' -DestinationPath 'dist\DiskPro-0.2.0-win64-portable.zip' -Force" || exit /b 1
powershell -NoProfile -Command "(Get-FileHash 'dist\DiskPro-0.2.0-win64-portable.zip').Hash.ToLower() + '  DiskPro-0.2.0-win64-portable.zip' | Set-Content dist\SHA256SUMS.txt"
echo done
