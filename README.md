# sak1to-shell
Multi-threaded, multi-os/platform (Linux/Windows) c2 server and Windows reverse TCP shell client both written in C.

Compiling reverse TCP shell client with mingw32:
```
i686-w64-mingw32-gcc sakito_revshell.c lib/sakito_core.c -o sakito_revshell.exe -s -Wno-write-strings -ffunction-sections -fdata-sections -fno-exceptions -fmerge-all-constants -static-libgcc -lws2_32
```

Compiling reverse TCP shell with cl.exe within Developers command prompt:
```
cl.exe sakito_revshell.c lib/sakito_core.c
```

Compiling Linux server with GCC:
```
gcc -pthread sakito_server.c lib/sakito_core.c lib/sakito_server_tools.c lib/linux/sakito_slin_utils.c -o sakito_server
```

Compiling Windows server with cl.exe within Developers command prompt:
```
cl.exe sakito_server.c lib/sakito_core.c lib/sakito_server_tools.c lib/windows/sakito_swin_utils.c
```


Command list:

- list: list available connections.

- interact [id]: interact with client.

- download [filename]: download a file from client.

- upload [filename]: upload a file to client.

- background: background client.

- exit: terminate client or host.

- cd [dir]: change directory on client or host.

![alt text](https://www.wallpaperbetter.com/wallpaper/156/434/483/cherry-blossom-flowers-painting-pink-1080P-wallpaper-middle-size.jpg)

