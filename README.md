# sak1to-shell
Multi-threaded multi-os/platform c2 server and reverse TCP shell client written in pure C (Windows).

Compiling client with mingw32:
```
i686-w64-mingw32-gcc sakito_revshell.c -o sakito_revshell.exe -s -Wno-write-strings -ffunction-sections -fdata-sections -fno-exceptions -fmerge-all-constants -static-libgcc -lws2_32
```

Compiling server with GCC:
```
gcc sakito_server.c -pthread -o sakito_server
```

Command list:

- list: list available connections.

- interact [id]: interact with client.

- download [filename]: download a file from client.

- upload [filename]: upload a file to client.

- background: background client.

- exit: terminate client or server.

- cd [dir]: change directory on client.

![alt text](https://www.wallpaperbetter.com/wallpaper/156/434/483/cherry-blossom-flowers-painting-pink-1080P-wallpaper-middle-size.jpg)

