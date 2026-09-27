@if not exist dist mkdir dist
cl main.c hidapi/windows/hid.c /Fe:dist/k400p-fn-lock.exe /I hidapi/include /I hidapi/windows setupapi.lib shell32.lib /MT /O2 /nologo
