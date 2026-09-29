@if not exist dist mkdir dist
rc /nologo /fo app.res app.rc
cl main.c hidapi/windows/hid.c app.res /Fe:dist/k400p-fn-lock.exe /I hidapi/include /I hidapi/windows user32.lib setupapi.lib shell32.lib advapi32.lib wtsapi32.lib comctl32.lib /MT /O2 /nologo
