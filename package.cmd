@echo off
rem Creates dist\DiskPro-0.1.0-win64-portable.zip (run build.cmd first)
setlocal
set D=dist\DiskPro-0.1.0-win64
rmdir /s /q dist 2>nul
mkdir %D% || exit /b 1
copy build\diskpro-gui.exe %D% >nul || exit /b 1
copy build\diskpro-cli.exe %D% >nul
copy build\diskpro-inventory.exe %D% >nul`r`ncopy build\diskpro-tools.exe %D% >nul
copy .tools\slint\lib\slint_cpp.dll %D% >nul || exit /b 1
copy LICENSE %D% >nul
copy THIRD_PARTY_NOTICES.md %D% >nul
copy README.md %D% >nul
copy .tools\slint\licenses\LICENSE.md %D%\SLINT-LICENSE.md >nul
powershell -NoProfile -Command "Compress-Archive -Path '%D%' -DestinationPath 'dist\DiskPro-0.1.0-win64-portable.zip' -Force" || exit /b 1
powershell -NoProfile -Command "(Get-FileHash 'dist\DiskPro-0.1.0-win64-portable.zip').Hash + '  DiskPro-0.1.0-win64-portable.zip' | Set-Content dist\SHA256SUMS.txt"
echo done

